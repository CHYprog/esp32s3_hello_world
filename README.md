# ESP32-S3 SH1106 OLED 显示与动画演示

使用 ESP32-S3，通过 I²C 驱动 128 × 64 SH1106 单色 OLED。当前示例使用 FreeRTOS 任务循环播放 5 帧猫咪动画，同时提供文字绘制、位图显示和滚动显示接口，包含硬件接线与编译烧录说明。

## 动画预览

![128×64 SH1106 OLED 猫咪动画预览](docs/assets/cat-animation.gif)

预览由项目中的 5 帧位图数据渲染生成，按示例代码的帧停留时间播放，并放大 4 倍以便查看。

## 主要功能

- 猫咪动画：内置 5 帧位图，可分别设置每帧停留时间。
- 中英文显示：支持 UTF-8 字符串、ASCII 字符和 GB2312 汉字字库。
- 图形绘制：支持像素绘制、清屏、填充和全屏位图显示。
- 滚动显示：支持垂直显示偏移和指定区域水平循环滚动。
- 使用说明：提供接线表、驱动接口说明及编译烧录步骤。

## 开发环境

- 开发板：ESP32-S3
- 显示屏：128 × 64 SH1106 OLED，I²C 接口
- 开发框架：ESP-IDF 6.1
- 开发语言：C

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

在已加载 ESP-IDF 6.1 环境的终端中，获取源码并执行以下命令。

```sh
git clone https://github.com/CHYprog/esp32s3_hello_world.git
cd esp32s3_hello_world
idf.py set-target esp32s3
idf.py build
idf.py -p COM5 flash monitor
```

将 `COM5` 替换为开发板对应的串口。串口日志默认使用 115200 波特率。

## 动画播放

上电后，`app_main()` 初始化 OLED，再创建 `Cat_Task`。任务依次将位图复制到显示缓冲区并刷新整屏，5 帧的停留时间依次为 2500、80、120、80、100 毫秒。初始化失败时，程序通过 `ESP_ERROR_CHECK` 报错并重启。

## 驱动接口

| 接口 | 功能 |
| --- | --- |
| `SH1106_Init()` | 创建 I²C 总线、探测设备、初始化 OLED 并清屏。 |
| `SH1106_Clear()` / `SH1106_Fill()` | 清空缓冲区，或将所有像素设为亮或灭。 |
| `SH1106_DrawPixel()` | 修改一个像素，忽略越界坐标。 |
| `SH1106_DrawChar()` | 用 16 像素高的字框绘制单个可打印 ASCII 字符。 |
| `SH1106_DrawString()` | 绘制 UTF-8 字符串，支持中英文、数字混排和换行。 |
| `SH1106_DrawBitmap()` | 将一帧 1024 字节的全屏页式位图复制到显示缓冲区。 |
| `SH1106_Update()` | 通过 I²C 将 1024 字节的显示缓冲区分 8 页同步发送到 OLED。 |
| `SH1106_SetScrollOffset()` | 设置垂直显示起始行，偏移范围为 0～63，不修改缓冲区。 |
| `SH1106_ScrollHorizontalArea()` | 对指定纵向区域内的像素进行水平循环滚动，正数向右、负数向左。 |

绘制操作只修改缓冲区，调用 `SH1106_Update()` 后才会更新屏幕。英文大小写、数字、ASCII 符号和汉字统一使用 16 像素行高。ASCII 字符按字模宽度排版，GB2312 的 6,763 个汉字和常用中文标点使用 16 × 16 点阵。字库中缺失的非 ASCII 字符显示为空心方框。

构建时会从 [文泉驿 BDF](fonts/wenquanyi/wenquanyi_12pt.bdf) 提取字模，字体来源和授权见 [字库说明](fonts/wenquanyi/NOTICE.txt)。

演示逻辑在 [`main/main.c`](main/main.c)，动画数据在 [`main/cat_bitmap.c`](main/cat_bitmap.c)，显示驱动及接口在 [`main/sh1106.c`](main/sh1106.c) 和 [`main/sh1106.h`](main/sh1106.h)，默认芯片目标在 [`sdkconfig.defaults`](sdkconfig.defaults)。

## 更新记录

### 2026-11-20

- 新增 5 帧猫咪动画数据，每帧为 128 × 64 像素、1024 字节，使用 SH1106 页式格式。
- 新增 `SH1106_DrawBitmap()` 全屏位图接口，并将默认演示改为 `Cat_Task` 循环播放动画。
- 增加垂直显示偏移、指定区域水平循环滚动，以及英文、数字与汉字的显示尺寸调整。
- 完善 `.clangd`：读取 `build/compile_commands.json`、指定 ESP32-S3 解析目标、开启参数名和推导类型的行内提示，并屏蔽头文件未使用警告。
- 将动画数据纳入构建，移除不存在的 `oled_image.c` 构建条目。
- 补充项目介绍、主要功能、开发环境和猫咪动画预览。
