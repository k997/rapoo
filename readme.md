# Rapoo 鼠标电量工具

读取雷柏 VT 系列无线鼠标（实测 VT9 PRO V2，接收器 VID `0x24ae` / PID `0x14a9`，有线直连 PID `0x46a9`）的电量。

## 目录结构

```
rapoo/
├── src/                      源代码
│   ├── rapoo-battery.c       控制台版：输出纯数字电量，供 AHK 等脚本调用
│   └── rapoo-battery-gui.c   托盘版：图标直接显示电量数字，低电量弹窗提醒
├── third_party/hidapi/       第三方库 hidapi（Windows 后端最小子集 + 完整许可文本）
│   ├── include/hidapi.h      公共头文件
│   ├── src/                  库实现及其私有头文件（未做任何修改）
│   └── LICENSE*.txt          上游许可文本，随库保留
├── bin/                      编译产物
├── Makefile
└── readme.md
```

## 编译

需要 MinGW-w64 的 gcc。在项目根目录直接：

```
make
```

或手动执行等价命令：

```
gcc src/rapoo-battery.c third_party/hidapi/src/hid.c -o bin/rapoo-battery.exe -I third_party/hidapi/include -static -lsetupapi
gcc src/rapoo-battery-gui.c third_party/hidapi/src/hid.c -o bin/rapoo-battery-gui.exe -I third_party/hidapi/include -static -lsetupapi -lgdi32 -mwindows -fexec-charset=GBK
```

说明：`-fexec-charset=GBK` 用于解决托盘气泡中文乱码；hidapi 的 Windows 实现需要链接 `-lsetupapi`。

## 使用

- `bin/rapoo-battery.exe` — 输出纯数字电量（如 `93`），失败输出 `ERROR`，方便 AHK 等脚本直接取值。单次读取约 1 秒。
- `bin/rapoo-battery-gui.exe` — 常驻托盘，每 5 分钟刷新一次图标，电量低于 30% 时弹气泡提醒（充电中不提醒），右键托盘图标可退出；悬停提示含充电状态。

注意：查询前请关闭网页驱动（hub.rapoo.cn），避免网页独占 HID 通道导致读取失败。

## 协议说明（逆向自官方网页驱动 hub.rapoo.cn）

设备为复合 HID 设备，配置通道是厂商自定义集合 `usagePage 0xFF00 / usage 0x02`
（"Configuration Interface (Secondary)"），收发均使用 Report ID `0x07`。

**电量不需要查询命令**：只要打开该接口，鼠标/接收器每约 3 秒自动推送一帧 18 字节
状态报文（reportId 7），格式如下（偏移从 reportId 后算起）：

| 偏移 | 含义 | 实测示例 |
| ---- | ---- | ---- |
| 0 | 高 4 位 sensor，低 4 位连接协议 | `0x40` |
| 1 | DPI 档位序号 | `0x02` |
| 2-3 | X 轴 DPI（小端 16 位） | `e8 03` = 1000 |
| 4-5 | Y 轴 DPI（小端 16 位） | 同上 |
| 6 | 充电状态：1=正常，2=充电中 | `0x01` |
| 7 | **电量百分比（0-100）** | `0x5d` = 93 |
| 8 | 灯效模式 | `0x00` |
| 9 | 回报率相关（kRate） | `0x03` |
| 10 | 回报率相关（rpt） | `0x01` |
| 11 | 配置档位序号 | `0x00` |
| 12-13 | 有线模式 USB PID（小端） | `a9 46` = 0x46a9 |
| 14 | 低 4 位按键类型，高 4 位滚轮类型 | `0x10` |
| 15 | 射频类型 | `0x02` |
| 16 | 传感器模式 | `0x02` |
| 17 | 最高位已连接标志，低 7 位 RSSI | `0xcc` |

本工具只做被动解析：枚举 VID `0x24ae` 下 usagePage/usage 匹配的接口，打开后循环
`hid_read_timeout` 等待 reportId 7 的状态帧，取偏移 7 为电量、偏移 6 判断充电。

网页驱动其余协议（供参考）：输出 reportId 7 发命令，`a5 a3 00/01` 进入/退出配置模式，
`a5 a4 <len> <addr lo> <addr hi> 00 00` 读 EEPROM，`a5 a5 <len> <addr> 00 00 <data>` 写
EEPROM（如 `a5 a5 02 8c 08 00 00 ba 25` 把 9658 写入地址 0x088c 设置 DPI），
`b0 00/01` 读鼠标/接收器固件版本。

## 第三方代码与许可（引用声明）

本项目使用开源库 [hidapi](https://github.com/libusb/hidapi)（libusb/hidapi Team，原作者 Alan Ott / Signal 11 Software）的 Windows 后端，仅保留编译所需的最小文件集，**未修改库源码**。

hidapi 允许使用者从以下三种许可中任选其一（见 `third_party/hidapi/LICENSE.txt`）：

1. GNU General Public License v3.0（`LICENSE-gpl3.txt`）
2. BSD-Style License，即 BSD-3-Clause（`LICENSE-bsd.txt`）
3. 原始 HIDAPI 许可（`LICENSE-orig.txt`）

**本项目选用 BSD-3-Clause 条款使用该库。** 依其要求，若分发本项目的二进制程序或源码，请随附保留上述版权与许可声明（保留 `third_party/hidapi/` 目录即可满足）。
