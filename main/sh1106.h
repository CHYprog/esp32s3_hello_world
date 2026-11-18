
/**
 * @file sh1106.h
 * @brief 128×64 单色 SH1106 OLED 的 I2C 显示接口。
 *
 * 坐标以屏幕左上角为原点，x 向右增大，y 向下增大，单位均为像素。
 * 有效像素坐标为 x = 0～127、y = 0～63。
 *
 * 清屏、填充和绘图函数操作同一个内存缓冲区，调用 SH1106_Update()
 * 后才会将这些修改发送到屏幕。可以先完成多次绘图，再统一刷新。
 * 多个任务使用本驱动时，需要由调用方协调缓冲区和 I2C 操作的访问。
 */
#ifndef SH1106_H
#define SH1106_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

// OLED 的 I2C 引脚，修改后需要重新编译程序。
#define OLED_SDA  8
#define OLED_SCL  9

#define SH1106_WIDTH   128
#define SH1106_HEIGHT  64

/**
 * @brief 初始化 I2C 总线和 OLED，并清屏、开启显示。
 *
 * 使用 OLED_SDA 和 OLED_SCL 指定的 GPIO，在 I2C_NUM_0 上创建主机总线，
 * 开启内部上拉，并探测 OLED。设备地址和通信速率由 sh1106.c 中的
 * OLED_I2C_ADDR 和 OLED_I2C_FREQ 配置，当前分别为 0x3C 和 100 kHz。
 * 初始化成功时，内存缓冲区和屏幕内容均已清空，可以开始绘图。
 *
 * @retval ESP_OK 初始化成功。
 * @retval ESP_ERR_INVALID_STATE 本驱动已经创建了总线，不能重复初始化。
 * @return 其他失败情况返回底层 I2C 接口的错误码。
 *
 * @note 应在屏幕供电稳定后调用。创建总线后的初始化步骤失败时，
 *       会尝试释放已创建的设备和总线资源。
 */
esp_err_t SH1106_Init(void);

/**
 * @brief 将整个显示缓冲区清零，使所有像素处于熄灭状态。
 *
 * 通常在绘制新的一帧内容前调用，以清除上一帧留下的文字和图形。
 * 调用 SH1106_Update() 后，屏幕才会显示清屏或重新绘制的结果。
 */
void SH1106_Clear(void);

/**
 * @brief 将整个显示缓冲区填充为统一的像素状态。
 *
 * @param on true 将全部像素设为亮，false 将全部像素设为灭。
 *
 * @note 调用 SH1106_Update() 后生效，可用于全屏点亮检查或清屏。
 */
void SH1106_Fill(bool on);

/**
 * @brief 在显示缓冲区中设置一个像素。
 *
 * @param x 像素的横坐标，有效范围为 0～SH1106_WIDTH - 1。
 * @param y 像素的纵坐标，有效范围为 0～SH1106_HEIGHT - 1。
 * @param on true 点亮该像素，false 熄灭该像素。
 *
 * @note 越界坐标会被直接忽略。修改会保留同一字节中其他像素的状态，
 *       调用 SH1106_Update() 后才会显示到屏幕。
 */
void SH1106_DrawPixel(int x, int y, bool on);

/**
 * @brief 绘制一个可打印 ASCII 字符。
 *
 * @param x 字符区域左上角的横坐标。
 * @param y 字符区域左上角的纵坐标，完整显示时取 0～48。
 * @param ch 字符，支持空格、大小写英文字母、数字和 ASCII 标点。
 *
 * 字符区域高 16 像素，宽度由字模决定。超出屏幕的像素会被裁剪，
 * 不可打印的字符按空格处理。绘制结束后调用 SH1106_Update()。
 */
void SH1106_DrawChar(int x, int y, char ch);

/**
 * @brief 从指定位置绘制 UTF-8 字符串，支持中英文和数字混排。
 *
 * @param x 首行左上角的横坐标，完整显示汉字时建议取 0～112。
 * @param y 首行左上角的纵坐标，完整显示一行时取 0～48。
 * @param str 以 '\0' 结尾的 UTF-8 字符串，传入 NULL 时直接返回。
 *
 * 所有字符使用 16 像素行高。ASCII 字符按字模的宽度推进，GB2312 汉字和
 * 常用中文标点占 16×16 像素。缺失的非 ASCII 字符或无效编码显示为空心方框。
 * 遇到 '\n' 或当前行放不下下一个字符时，回到起始 x 并下移一行。
 * 单个字符在起始位置超出右边界时会被裁剪。底部不足一行时停止绘制。
 *
 * @note 重画文字时可先清屏，以免残留上一帧。绘制结束后调用 SH1106_Update()。
 */
void SH1106_DrawString(int x, int y, const char *str);

/**
 * @brief 通过 I2C 将整个显示缓冲区同步到 OLED。
 *
 * 将 128×64 像素对应的 1024 字节数据分成 8 页，每页发送 128 字节，
 * 并使用 OLED_COL_OFFSET 指定的列偏移对齐 SH1106 的可见显示区域。
 * 函数同步等待各次 I2C 传输完成，适合在一帧绘制结束后调用。
 *
 * @retval ESP_OK 所有页均已发送成功。
 * @retval ESP_ERR_INVALID_STATE 尚未添加 OLED 的 I2C 设备，通常应先初始化。
 * @return 传输失败时，立即返回对应 I2C 接口的错误码。
 *
 * @note OLED_TIMEOUT_MS 是单次 I2C 传输的超时时间。中途失败时，屏幕
 *       可能已更新部分页面，内存缓冲区仍保留，可再次调用本函数重试。
 */
esp_err_t SH1106_Update(void);

#endif
