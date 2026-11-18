# ESP32-S3 SH1106 OLED 显示演示

本项目使用 ESP-IDF，通过 I²C 驱动一块 128 × 64 像素的 SH1106 单色 OLED。屏幕显示两行固定文字和一个从 `0` 开始的计数器，计数约每秒加一。

```text
你好 ESP32-S3
显示正常
计数: 0
```

上电后，程序等待约 100 毫秒，再探测并初始化 OLED。初始化成功后，串口会输出 `OLED Init OK`。每轮循环会在内存里清除上一帧，绘制三行文字，再通过 I²C 刷新整屏。初始化或刷新出错时，程序通过 `ESP_ERROR_CHECK` 报错并重启。

## 硬件接线

使用 ESP32-S3 开发板和 I²C 接口的 SH1106 OLED 模块。接线前请确认模块支持 3.3 V 供电。

| OLED 模块引脚 | ESP32-S3 开发板 |
| --- | --- |
| VCC | 3V3 |
| GND | GND |
| SDA | GPIO8 |
| SCL | GPIO9 |

驱动使用 `I2C_NUM_0`，启用 GPIO 内部上拉，通信速率为 100 kHz，OLED 的 7 位 I²C 地址为 `0x3C`。引脚在 [`main/sh1106.h`](main/sh1106.h) 中定义，设备地址和速率在 [`main/sh1106.c`](main/sh1106.c) 中定义。

## 编译、烧录与查看日志

在已加载 ESP-IDF 6.1 环境的终端中，进入项目根目录执行以下命令。

```sh
idf.py set-target esp32s3
idf.py build
idf.py -p COM5 flash monitor
```

将 `COM5` 替换为开发板对应的串口。串口日志默认使用 115200 波特率。

## 驱动接口

| 接口 | 功能 |
| --- | --- |
| `SH1106_Init()` | 创建 I²C 总线、探测设备、初始化 OLED 并清屏。 |
| `SH1106_Clear()` / `SH1106_Fill()` | 清空缓冲区，或将所有像素设为亮或灭。 |
| `SH1106_DrawPixel()` | 修改一个像素，忽略越界坐标。 |
| `SH1106_DrawChar()` | 用 6 × 8 像素的字模绘制单个 ASCII 字符。 |
| `SH1106_DrawString()` | 绘制 UTF-8 字符串，支持中英文、数字混排和换行。 |
| `SH1106_Update()` | 通过 I²C 将 1024 字节的显示缓冲区分 8 页同步发送到 OLED。 |

绘制操作只修改缓冲区，调用 `SH1106_Update()` 后才会更新屏幕。ASCII 字模支持英文字母、数字、空格、减号、冒号和句点，小写字母按大写显示。UTF-8 字符串可以同时包含 GB2312 的 6,763 个汉字和常用中文标点。汉字使用 16 × 16 点阵，混排行高为 16 像素，纯 ASCII 字符串行高为 8 像素。字库中缺失的非 ASCII 字符显示为空心方框。

构建时会从 [文泉驿 BDF](fonts/wenquanyi/wenquanyi_12pt.bdf) 提取字模，字体来源和授权见 [字库说明](fonts/wenquanyi/NOTICE.txt)。

演示逻辑在 [`main/main.c`](main/main.c)，显示驱动及接口在 [`main/sh1106.c`](main/sh1106.c) 和 [`main/sh1106.h`](main/sh1106.h)，默认芯片目标在 [`sdkconfig.defaults`](sdkconfig.defaults)。
