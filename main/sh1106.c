
#include "sh1106.h"

#include <string.h>

#include "driver/i2c_master.h"

#define OLED_I2C_ADDR    0x3C
#define OLED_I2C_FREQ    100000
#define OLED_TIMEOUT_MS  1000
#define OLED_COL_OFFSET  2

#define OLED_PAGES       (SH1106_HEIGHT / 8)

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

static const uint8_t font_digits[10][5] = {
    {0x3E,0x51,0x49,0x45,0x3E}, // 0
    {0x00,0x42,0x7F,0x40,0x00}, // 1
    {0x42,0x61,0x51,0x49,0x46}, // 2
    {0x21,0x41,0x45,0x4B,0x31}, // 3
    {0x18,0x14,0x12,0x7F,0x10}, // 4
    {0x27,0x45,0x45,0x45,0x39}, // 5
    {0x3C,0x4A,0x49,0x49,0x30}, // 6
    {0x01,0x71,0x09,0x05,0x03}, // 7
    {0x36,0x49,0x49,0x49,0x36}, // 8
    {0x06,0x49,0x49,0x29,0x1E}  // 9
};

static const uint8_t font_upper[26][5] = {
    {0x7E,0x11,0x11,0x11,0x7E}, // A
    {0x7F,0x49,0x49,0x49,0x36}, // B
    {0x3E,0x41,0x41,0x41,0x22}, // C
    {0x7F,0x41,0x41,0x22,0x1C}, // D
    {0x7F,0x49,0x49,0x49,0x41}, // E
    {0x7F,0x09,0x09,0x09,0x01}, // F
    {0x3E,0x41,0x49,0x49,0x7A}, // G
    {0x7F,0x08,0x08,0x08,0x7F}, // H
    {0x00,0x41,0x7F,0x41,0x00}, // I
    {0x20,0x40,0x41,0x3F,0x01}, // J
    {0x7F,0x08,0x14,0x22,0x41}, // K
    {0x7F,0x40,0x40,0x40,0x40}, // L
    {0x7F,0x02,0x0C,0x02,0x7F}, // M
    {0x7F,0x04,0x08,0x10,0x7F}, // N
    {0x3E,0x41,0x41,0x41,0x3E}, // O
    {0x7F,0x09,0x09,0x09,0x06}, // P
    {0x3E,0x41,0x51,0x21,0x5E}, // Q
    {0x7F,0x09,0x19,0x29,0x46}, // R
    {0x46,0x49,0x49,0x49,0x31}, // S
    {0x01,0x01,0x7F,0x01,0x01}, // T
    {0x3F,0x40,0x40,0x40,0x3F}, // U
    {0x1F,0x20,0x40,0x20,0x1F}, // V
    {0x3F,0x40,0x38,0x40,0x3F}, // W
    {0x63,0x14,0x08,0x14,0x63}, // X
    {0x07,0x08,0x70,0x08,0x07}, // Y
    {0x61,0x51,0x49,0x45,0x43}  // Z
};

/**
 * @brief 将一个字符转换为包含 5 列像素数据的字模。
 *
 * @param ch 待查找的字符，小写英文字母会先转换为大写。
 * @return 指向静态只读字模的指针，每个字节表示一列，低位表示上方像素。
 *
 * 支持数字、英文字母、减号、冒号和句点，其余字符均返回空白字模。
 * 返回的数据在整个程序运行期间有效，由本文件持有。
 */
static const uint8_t *sh1106_get_glyph(char ch)
{
    static const uint8_t space[5] =
        {0,0,0,0,0};

    static const uint8_t dash[5] =
        {0x08,0x08,0x08,0x08,0x08};

    static const uint8_t colon[5] =
        {0x00,0x36,0x36,0x00,0x00};

    static const uint8_t dot[5] =
        {0x00,0x60,0x60,0x00,0x00};

    if (ch >= '0' && ch <= '9')
        return font_digits[ch - '0'];

    if (ch >= 'a' && ch <= 'z')
        ch = ch - 'a' + 'A';

    if (ch >= 'A' && ch <= 'Z')
        return font_upper[ch - 'A'];

    if (ch == '-') return dash;
    if (ch == ':') return colon;
    if (ch == '.') return dot;

    return space;
}

/**
 * @brief 将一个字模展开到缓冲区中，覆盖一个 6×8 像素区域。
 *
 * @param x 字符区域左上角横坐标。
 * @param y 字符区域左上角纵坐标。
 * @param ch 要显示的字符，字符集由 sh1106_get_glyph() 决定。
 *
 * 前 5 列按字模逐位绘制，第 6 列写入空白作为字符间隔。
 * 字模中的 0 也会写入，因此区域内原有的像素会被覆盖。
 * 超出屏幕的部分由 SH1106_DrawPixel() 裁剪，绘制后需要统一刷新。
 */
void SH1106_DrawChar(int x, int y, char ch)
{
    const uint8_t *glyph = sh1106_get_glyph(ch);

    for (int col = 0; col < 6; col++) {
        uint8_t bits = (col < 5) ? glyph[col] : 0;

        for (int row = 0; row < 8; row++) {
            bool on = ((bits >> row) & 1U) != 0;
            SH1106_DrawPixel(x + col, y + row, on);
        }
    }
}

/**
 * @brief 逐字节绘制字符串，处理显式换行和右边界自动换行。
 *
 * @param x 首字符左上角横坐标，同时作为每次换行后的起始横坐标。
 * @param y 首行左上角纵坐标。
 * @param str 以 '\0' 结尾的字符串，NULL 会被忽略。
 *
 * 普通字符绘制后向右移动 6 像素。遇到 '\n'，或右侧剩余空间不足
 * 6 像素时，回到初始 x 并向下移动 8 像素。距底部不足 8 像素时，
 * 停止绘制后续字符。字模的覆盖行为与 SH1106_DrawChar() 一致。
 * @note 原有文字的尾部需要由调用方清除，绘制完成后调用 SH1106_Update()。
 */
void SH1106_DrawString(int x, int y, const char *str)
{
    if (str == NULL) return;

    int start_x = x;

    while (*str != '\0') {
        if (*str == '\n') {
            x = start_x;
            y += 8;
            str++;
            continue;
        }

        if (x + 6 > SH1106_WIDTH) {
            x = start_x;
            y += 8;
        }

        if (y + 8 > SH1106_HEIGHT) {
            break;
        }

        SH1106_DrawChar(x, y, *str);

        x += 6;
        str++;
    }
}
