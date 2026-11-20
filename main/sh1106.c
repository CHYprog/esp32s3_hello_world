
#include "sh1106.h"

#include <string.h>

#include "driver/i2c_master.h"

#define OLED_I2C_ADDR    0x3C
#define OLED_I2C_FREQ    100000
#define OLED_TIMEOUT_MS  1000
#define OLED_COL_OFFSET  2

#define OLED_PAGES       (SH1106_HEIGHT / 8)
#define FONT_HEIGHT      16
#define ZH16_WIDTH       16

// I2C总线和设备句柄
static i2c_master_bus_handle_t s_bus = NULL;
static i2c_master_dev_handle_t s_dev = NULL;

// 每页包含纵向 8 个像素，共 8 页，每页 128 字节。
// 每个字节对应同一列的 8 个像素，bit0 对应该页顶部，bit7 对应底部。
static uint8_t s_buffer[SH1106_WIDTH * OLED_PAGES];

/**
 * @brief 向 OLED 同步发送一个命令字节或命令参数字节。
 *
 * @param cmd 要发送的字节，由调用方按 SH1106 的命令顺序传入。
 * @return 返回 i2c_master_transmit() 的结果，ESP_OK 表示传输成功。
 *
 * 控制字节 0x00 表示后续内容属于命令流，第二个字节为实际内容。
 * 调用前需要先创建 s_dev，单次传输最多等待 OLED_TIMEOUT_MS 毫秒。
 */
static esp_err_t sh1106_cmd(uint8_t cmd)
{
    uint8_t tx[2] = {0x00, cmd};

    return i2c_master_transmit(
        s_dev, tx, sizeof(tx), OLED_TIMEOUT_MS
    );
}

/**
 * @brief 使用宏配置的引脚初始化 OLED，成功后屏幕为空白且显示已开启。
 *
 * 依次创建 I2C 总线、探测地址、添加设备，再设置显示时钟、扫描方向、
 * 对比度等参数。先在关闭显示的状态下清空显存，最后开启显示。
 *
 * @return 成功返回 ESP_OK，重复初始化返回 ESP_ERR_INVALID_STATE，
 *         其他错误沿用底层 I2C 接口的返回值。
 * @note 引脚由 sh1106.h 的 OLED_SDA 和 OLED_SCL 决定。创建总线后发生
 *       错误会进入 fail 分支，尝试释放资源并将本驱动的句柄复位。
 */
esp_err_t SH1106_Init(void)
{
    if (s_bus != NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = (gpio_num_t)OLED_SDA,
        .scl_io_num = (gpio_num_t)OLED_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };

    esp_err_t err = i2c_new_master_bus(
        &bus_cfg, &s_bus
    );

    if (err != ESP_OK) {
        return err;
    }

    // 检查屏幕是否响应I2C地址
    err = i2c_master_probe(
        s_bus, OLED_I2C_ADDR, OLED_TIMEOUT_MS
    );

    if (err != ESP_OK) {
        goto fail;
    }

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = OLED_I2C_ADDR,
        .scl_speed_hz = OLED_I2C_FREQ,
    };

    err = i2c_master_bus_add_device(
        s_bus, &dev_cfg, &s_dev
    );

    if (err != ESP_OK) {
        goto fail;
    }

    // SH1106初始化命令序列
    const uint8_t init_cmds[] = {
        0xAE,        // 关闭显示
        0xD5, 0x80,  // 显示时钟
        0xA8, 0x3F,  // 64行
        0xD3, 0x00,  // 显示偏移
        0x40,        // 起始行0
        0xAD, 0x8B,  // 内部DC-DC
        0xA1,        // SEG映射反转
        0xC8,        // COM扫描方向
        0xDA, 0x12,  // COM配置
        0x81, 0x7F,  // 对比度
        0xD9, 0x22,  // 预充电周期
        0xDB, 0x35,  // VCOM设置
        0xA4,        // 根据RAM内容显示
        0xA6,        // 正常显示
    };

    for (size_t i = 0; i < sizeof(init_cmds); i++) {
        err = sh1106_cmd(init_cmds[i]);

        if (err != ESP_OK) {
            goto fail;
        }
    }

    SH1106_Clear();

    err = SH1106_Update();
    if (err != ESP_OK) {
        goto fail;
    }

    err = sh1106_cmd(0xAF); // 开启显示
    if (err != ESP_OK) {
        goto fail;
    }

    return ESP_OK;

fail:
    if (s_dev != NULL) {
        i2c_master_bus_rm_device(s_dev);
        s_dev = NULL;
    }

    if (s_bus != NULL) {
        i2c_del_master_bus(s_bus);
        s_bus = NULL;
    }

    return err;
}

/**
 * @brief 将内存缓冲区全部置零，为绘制新的一帧准备空白背景。
 *
 * 每个 bit 表示一个像素，置零后所有像素为灭。
 * 随后可继续绘图，并由 SH1106_Update() 统一提交到屏幕。
 */
void SH1106_Clear(void)
{
    memset(s_buffer, 0, sizeof(s_buffer));
}

/**
 * @brief 用同一种亮灭状态填满显示缓冲区。
 *
 * @param on true 使用 0xFF 点亮所有像素，false 使用 0x00 熄灭所有像素。
 * @note 此操作覆盖缓冲区中的全部图像，刷新屏幕需调用 SH1106_Update()。
 */
void SH1106_Fill(bool on)
{
    memset(s_buffer, on ? 0xFF : 0x00,
           sizeof(s_buffer));
}

/**
 * @brief 修改指定坐标对应的一个像素位。
 *
 * @param x 横坐标，范围为 0～127。
 * @param y 纵坐标，范围为 0～63。
 * @param on true 点亮，false 熄灭。
 *
 * y / 8 确定页号，x 确定页内字节，y % 8 确定该字节中的 bit。
 * 越界时直接返回，有效坐标通过位操作修改，保留周围像素的状态。
 * @note 此函数只修改内存，调用 SH1106_Update() 后显示结果。
 */
void SH1106_DrawPixel(int x, int y, bool on)
{
    if (x < 0 || x >= SH1106_WIDTH ||
        y < 0 || y >= SH1106_HEIGHT) {
        return;
    }

    int index = x + (y / 8) * SH1106_WIDTH;
    uint8_t mask = (uint8_t)(1U << (y % 8));

    if (on) {
        s_buffer[index] |= mask;
    } else {
        s_buffer[index] &= (uint8_t)~mask;
    }
}

/**
 * @brief 按页将整帧缓冲区同步写入 OLED 显存。
 *
 * 每页先设置页地址和起始列，再以 0x40 数据控制字节开头发送 128 字节。
 * OLED_COL_OFFSET 将逻辑横坐标 0 对齐到屏幕可见区域的起始列。
 *
 * @return 全部发送成功返回 ESP_OK，设备句柄为空返回 ESP_ERR_INVALID_STATE，
 *         任一命令或数据传输失败时立即返回底层错误码。
 * @note 此函数同步等待 I2C 传输，缓冲区中的内容会被保留。
 *       中途失败可能导致屏幕只更新部分页面，再次调用会从第 0 页重发。
 */
esp_err_t SH1106_Update(void)
{
    if (s_dev == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t tx[SH1106_WIDTH + 1];
    tx[0] = 0x40; // 后续字节为显示数据

    for (int page = 0; page < OLED_PAGES; page++) {

        int col = OLED_COL_OFFSET;

        // 设置页地址
        esp_err_t err = sh1106_cmd(0xB0 | page);
        if (err != ESP_OK) return err;

        // 设置列地址低4位
        err = sh1106_cmd(0x00 | (col & 0x0F));
        if (err != ESP_OK) return err;

        // 设置列地址高4位
        err = sh1106_cmd(0x10 | (col >> 4));
        if (err != ESP_OK) return err;

        memcpy(
            tx + 1,
            &s_buffer[page * SH1106_WIDTH],
            SH1106_WIDTH
        );

        err = i2c_master_transmit(
            s_dev, tx, sizeof(tx), OLED_TIMEOUT_MS
        );

        if (err != ESP_OK) {
            return err;
        }
    }

    return ESP_OK;
}

typedef struct {
    uint8_t width;
    uint16_t rows[FONT_HEIGHT];
} sh1106_ascii_glyph_t;

typedef struct {
    uint16_t codepoint;
    uint16_t rows[FONT_HEIGHT];
} sh1106_zh16_glyph_t;

#include "font_zh16.inc"

static const sh1106_ascii_glyph_t *sh1106_get_ascii_glyph(uint32_t codepoint)
{
    if (codepoint < ' ' || codepoint > '~') {
        codepoint = ' ';
    }
    return &font_ascii[codepoint - ' '];
}

// 字模表按 Unicode 码点排序，缺字时绘制空心方框。
static const uint16_t *sh1106_get_zh16_glyph(uint32_t codepoint)
{
    static const uint16_t placeholder[FONT_HEIGHT] = {
        0, 0, 0x3FFC, 0x2004, 0x2004, 0x2004, 0x2004, 0x2004,
        0x2004, 0x2004, 0x2004, 0x2004, 0x2004, 0x3FFC, 0, 0,
    };
    size_t first = 0;
    size_t last = sizeof(font_zh16) / sizeof(font_zh16[0]);

    while (first < last) {
        size_t middle = first + (last - first) / 2;
        const sh1106_zh16_glyph_t *glyph = &font_zh16[middle];
        if (codepoint == glyph->codepoint) {
            return glyph->rows;
        }
        if (codepoint < glyph->codepoint) {
            last = middle;
        } else {
            first = middle + 1;
        }
    }

    return placeholder;
}

static void sh1106_draw_glyph(int x, int y, const uint16_t *rows, int width)
{
    if (x >= SH1106_WIDTH || y >= SH1106_HEIGHT) {
        return;
    }

    for (int row = 0; row < FONT_HEIGHT; row++) {
        for (int col = 0; col < width; col++) {
            bool on = (rows[row] & (0x8000U >> col)) != 0;
            SH1106_DrawPixel(x + col, y + row, on);
        }
    }
}

void SH1106_DrawChar(int x, int y, char ch)
{
    const sh1106_ascii_glyph_t *glyph = sh1106_get_ascii_glyph((unsigned char)ch);
    sh1106_draw_glyph(x, y, glyph->rows, glyph->width);
}

// 逐字节读取续字节，遇到无效字节时留给下次解码。
static uint32_t sh1106_next_codepoint(const unsigned char **cursor)
{
    const uint32_t replacement = 0xFFFD;
    uint32_t lead = **cursor;
    (*cursor)++;
    if (lead < 0x80) {
        return lead;
    }

    uint32_t codepoint;
    uint32_t minimum;
    int remaining;
    if (lead >= 0xC2 && lead <= 0xDF) {
        codepoint = lead & 0x1F;
        minimum = 0x80;
        remaining = 1;
    } else if (lead >= 0xE0 && lead <= 0xEF) {
        codepoint = lead & 0x0F;
        minimum = 0x800;
        remaining = 2;
    } else if (lead >= 0xF0 && lead <= 0xF4) {
        codepoint = lead & 0x07;
        minimum = 0x10000;
        remaining = 3;
    } else {
        return replacement;
    }

    for (int i = 0; i < remaining; i++) {
        uint32_t next = **cursor;
        if ((next & 0xC0) != 0x80) {
            return replacement;
        }
        codepoint = (codepoint << 6) | (next & 0x3F);
        (*cursor)++;
    }

    if (codepoint < minimum || codepoint > 0x10FFFF ||
        (codepoint >= 0xD800 && codepoint <= 0xDFFF)) {
        return replacement;
    }
    return codepoint;
}

void SH1106_DrawString(int x, int y, const char *str)
{
    if (str == NULL || x >= SH1106_WIDTH || y >= SH1106_HEIGHT) {
        return;
    }

    const unsigned char *cursor = (const unsigned char *)str;
    int start_x = x;

    while (*cursor != '\0' && y <= SH1106_HEIGHT - FONT_HEIGHT) {
        uint32_t codepoint = sh1106_next_codepoint(&cursor);
        if (codepoint == '\n') {
            x = start_x;
            y += FONT_HEIGHT;
            continue;
        }

        const uint16_t *rows;
        int width;
        if (codepoint < 0x80) {
            const sh1106_ascii_glyph_t *glyph = sh1106_get_ascii_glyph(codepoint);
            rows = glyph->rows;
            width = glyph->width;
        } else {
            rows = sh1106_get_zh16_glyph(codepoint);
            width = ZH16_WIDTH;
        }

        if (x != start_x && x > SH1106_WIDTH - width) {
            x = start_x;
            y += FONT_HEIGHT;
            if (y > SH1106_HEIGHT - FONT_HEIGHT) {
                break;
            }
        }

        sh1106_draw_glyph(x, y, rows, width);
        x += width;
    }
}


/**
 * @brief 设置 OLED 垂直滚动位置
 */
esp_err_t SH1106_SetScrollOffset(uint8_t offset)
{
    // 检查 OLED 是否初始化
    if (s_dev == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    // 检查偏移量是否合法
    if (offset >= SH1106_HEIGHT) {
        return ESP_ERR_INVALID_ARG;
    }

    // 设置显示起始行
    return sh1106_cmd(0x40 | offset);
}


/**
 * @brief 指定像素区域水平循环滚动
 */
void SH1106_ScrollHorizontalArea(
    int y_start,
    int y_end,
    int pixels
)
{
    // 1. 检查区域是否合法
    if (y_start < 0 ||
        y_end > SH1106_HEIGHT ||
        y_start >= y_end)
    {
        return;
    }

    // 2. 计算滚动距离
    int shift = pixels % SH1106_WIDTH;

    if (shift < 0)
    {
        shift += SH1106_WIDTH;
    }

    if (shift == 0)
    {
        return;
    }

    // 3. 临时保存一页数据
    uint8_t temp[SH1106_WIDTH];

    // 4. 遍历指定区域涉及的页
    int first_page = y_start / 8;
    int last_page = (y_end - 1) / 8;

    for (int page = first_page;
         page <= last_page;
         page++)
    {
        uint8_t *line =
            &s_buffer[page * SH1106_WIDTH];

        // 保存原始数据
        memcpy(temp, line, SH1106_WIDTH);

        // 计算本页哪些像素行需要移动
        uint8_t mask = 0;

        for (int bit = 0; bit < 8; bit++)
        {
            int y = page * 8 + bit;

            if (y >= y_start && y < y_end)
            {
                mask |= (uint8_t)(1U << bit);
            }
        }

        // 5. 水平移动指定像素位
        for (int x = 0; x < SH1106_WIDTH; x++)
        {
            int src =
                (x - shift + SH1106_WIDTH)
                % SH1106_WIDTH;

            // 保留区域外像素，只替换区域内像素
            line[x] =
                (temp[x] & (uint8_t)~mask) |
                (temp[src] & mask);
        }
    }
}

/**
 * @brief 绘制128×64全屏单色位图
 */
void SH1106_DrawBitmap(const uint8_t *bitmap)
{
    if (bitmap == NULL)
    {
        return;
    }

    memcpy(
        s_buffer,
        bitmap,
        sizeof(s_buffer)
    );
}
