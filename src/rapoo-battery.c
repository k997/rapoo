#include <stdio.h>
#include "hidapi.h"

// 雷柏 VT 系列游戏鼠标（VT9 PRO V2 实测）
// 配置接口：usagePage 0xFF00 / usage 0x02（Configuration Interface）
// 接收器 PID 0x14a9；有线直连时为整机 PID 0x46a9。按 VID + usage 匹配以兼容两者。
#define RAPOO_VID 0x24ae
#define CONFIG_USAGE_PAGE 0xff00
#define CONFIG_USAGE 0x02

// 状态帧 reportId，鼠标每约 3 秒自主推送一帧，无需发送任何查询命令
#define STATUS_REPORT_ID 7

// batStatus 字段取值（网页驱动源码）：1=正常，2=充电中
#define BAT_VALID 1
#define BAT_CHARGING 2

// 返回电量百分比（0-100），失败返回 -1；charging 非空时写入是否充电中
int get_battery(int *charging) {
    struct hid_device_info *devs, *cur_dev;
    int battery_level = -1;

    if (hid_init() != 0) return -1;

    devs = hid_enumerate(RAPOO_VID, 0x0);
    cur_dev = devs;

    while (cur_dev) {
        if (cur_dev->usage_page == CONFIG_USAGE_PAGE && cur_dev->usage == CONFIG_USAGE) {
            hid_device *handle = hid_open_path(cur_dev->path);
            if (handle) {
                unsigned char read_buf[64] = {0};
                // 帧周期约 3 秒，最多等 12 秒
                for (int i = 0; i < 12 && battery_level < 0; i++) {
                    int res = hid_read_timeout(handle, read_buf, sizeof(read_buf), 1000);
                    // hidapi 读取首字节为 reportId，其后为 18 字节状态帧：
                    // [1]=sensor/协议 [2]=DPI档位 [3..6]=DPI值 [7]=充电状态 [8]=电量百分比
                    // [13..14]=有线PID [18]=rfConnected<<7|rssi
                    if (res >= 9 && read_buf[0] == STATUS_REPORT_ID &&
                        read_buf[7] <= BAT_CHARGING && read_buf[8] <= 100) {
                        battery_level = read_buf[8];
                        if (charging) *charging = (read_buf[7] == BAT_CHARGING);
                    }
                }
                hid_close(handle);
            }
        }
        cur_dev = cur_dev->next;
    }

    hid_free_enumeration(devs);
    hid_exit();
    return battery_level;
}

int main() {
    int val = get_battery(NULL);

    if (val != -1) {
        // 纯数字输出给 AHK，不换行
        printf("%d", val);
    } else {
        printf("ERROR");
    }

    return 0;
}
