#include <windows.h>
#include <shellapi.h>
#include <stdio.h>
#include <time.h>
#include <hidapi.h>

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

#define WM_TRAYICON (WM_USER + 1)
#define ID_TRAY_EXIT 1001
#define TIMER_ID 1

NOTIFYICONDATA nid;
time_t last_alert_time = 0;

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
                for (int i = 0; i < 12 && battery_level < 0; i++) {
                    int res = hid_read_timeout(handle, read_buf, sizeof(read_buf), 1000);
                    // 首字节为 reportId，其后 18 字节状态帧：[7]=充电状态 [8]=电量百分比
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

// 核心：生成纯数字图标
HICON CreateNumberIcon(int percentage) {
    // 获取任务栏图标标准尺寸 (通常 16x16 或基于 DPI 缩放)
    int width = GetSystemMetrics(SM_CXSMICON);
    int height = GetSystemMetrics(SM_CYSMICON);

    HDC hdcScreen = GetDC(NULL);
    HDC hdc = CreateCompatibleDC(hdcScreen);
    HBITMAP hbmp = CreateCompatibleBitmap(hdcScreen, width, height);
    HBITMAP hbmpMask = CreateCompatibleBitmap(hdcScreen, width, height);

    RECT rectFull = {0, 0, width, height};

    // 1. 设置掩码 (Mask)：全黑表示整个方块不透明
    SelectObject(hdc, hbmpMask);
    HBRUSH hBlackBrush = CreateSolidBrush(RGB(0, 0, 0));
    FillRect(hdc, &rectFull, hBlackBrush);

    // 2. 设置颜色本体和文字
    SelectObject(hdc, hbmp);

    COLORREF bgColor;
    COLORREF textColor;

    // 根据电量设定背景色和文字颜色（充电中按满电绿色显示）
    if (percentage < 30) {
        bgColor = RGB(237, 28, 36);      // 红底
        textColor = RGB(255, 255, 255);  // 白字
    } else if (percentage < 50) {
        bgColor = RGB(255, 201, 14);     // 黄底
        textColor = RGB(0, 0, 0);        // 黑字
    } else {
        bgColor = RGB(34, 177, 76);      // 绿底
        textColor = RGB(255, 255, 255);  // 白字
    }

    HBRUSH hBgBrush = CreateSolidBrush(bgColor);
    FillRect(hdc, &rectFull, hBgBrush);

    // 3. 绘制文字
    char text[4];
    snprintf(text, sizeof(text), "%d", percentage);

    // 动态调整字体大小 (100有3个字符，需要稍微缩小一点)
    int fontSize = (percentage == 100) ? (height - 4) : (height - 2);

    HFONT hFont = CreateFont(
        -fontSize, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_OUTLINE_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, VARIABLE_PITCH, "Arial"
    );

    HFONT hOldFont = (HFONT)SelectObject(hdc, hFont);
    SetTextColor(hdc, textColor);
    SetBkMode(hdc, TRANSPARENT); // 文字背景透明，露出底色

    // 将文字居中绘制
    DrawText(hdc, text, -1, &rectFull, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    // 4. 合成图标并清理资源
    SelectObject(hdc, hOldFont);
    DeleteObject(hFont);
    DeleteObject(hBgBrush);
    DeleteObject(hBlackBrush);

    ICONINFO ii = {0};
    ii.fIcon = TRUE;
    ii.hbmMask = hbmpMask;
    ii.hbmColor = hbmp;
    HICON hIcon = CreateIconIndirect(&ii);

    DeleteObject(hbmp);
    DeleteObject(hbmpMask);
    DeleteDC(hdc);
    ReleaseDC(NULL, hdcScreen);

    return hIcon;
}

// 检查电量并更新图标/弹窗
void check_battery_and_alert(HWND hwnd) {
    int charging = 0;
    int val = get_battery(&charging);
    if (val == -1) {
        snprintf(nid.szTip, sizeof(nid.szTip), "Rapoo 电池: 获取失败");
        Shell_NotifyIcon(NIM_MODIFY, &nid);
        return;
    }

    if (charging) {
        snprintf(nid.szTip, sizeof(nid.szTip), "Rapoo 电池: %d%%（充电中）", val);
    } else {
        snprintf(nid.szTip, sizeof(nid.szTip), "Rapoo 电池: %d%%", val);
    }

    // 生成新数字图标并替换
    HICON hOldIcon = nid.hIcon;
    nid.hIcon = CreateNumberIcon(val);
    Shell_NotifyIcon(NIM_MODIFY, &nid);

    if (hOldIcon && hOldIcon != LoadIcon(NULL, IDI_APPLICATION)) {
        DestroyIcon(hOldIcon);
    }

    // 低电量判定 (<30)，充电中不提醒
    if (val < 30 && !charging) {
        time_t now = time(NULL);
        if (last_alert_time == 0 || difftime(now, last_alert_time) >= 3600) {
            last_alert_time = now;
            NOTIFYICONDATA alert_nid = nid;
            alert_nid.uFlags = NIF_INFO;
            snprintf(alert_nid.szInfoTitle, sizeof(alert_nid.szInfoTitle), "鼠标低电量警告");
            snprintf(alert_nid.szInfo, sizeof(alert_nid.szInfo), "当前电量仅剩 %d%%，请及时充电！", val);
            alert_nid.dwInfoFlags = NIIF_WARNING;
            Shell_NotifyIcon(NIM_MODIFY, &alert_nid);
        }
    }
}

// 窗口消息处理
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_TRAYICON) {
        if (lp == WM_RBUTTONUP) {
            HMENU hMenu = CreatePopupMenu();
            AppendMenu(hMenu, MF_STRING, ID_TRAY_EXIT, "退出程序");
            POINT pt;
            GetCursorPos(&pt);
            SetForegroundWindow(hwnd);
            TrackPopupMenu(hMenu, TPM_LEFTALIGN | TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, NULL);
            DestroyMenu(hMenu);
        }
        return 0;
    }
    if (msg == WM_COMMAND && LOWORD(wp) == ID_TRAY_EXIT) {
        DestroyWindow(hwnd);
        return 0;
    }
    if (msg == WM_TIMER && wp == TIMER_ID) {
        check_battery_and_alert(hwnd);
        return 0;
    }
    if (msg == WM_DESTROY) {
        Shell_NotifyIcon(NIM_DELETE, &nid);
        if (nid.hIcon) DestroyIcon(nid.hIcon);
        KillTimer(hwnd, TIMER_ID);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hwnd, msg, wp, lp);
}

// 入口函数
int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR cmd, int show) {
    WNDCLASS wc = {0};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.lpszClassName = "RapooBatteryTrayClass";
    RegisterClass(&wc);

    HWND hwnd = CreateWindow("RapooBatteryTrayClass", "", 0, 0, 0, 0, 0, NULL, NULL, hInst, NULL);

    nid.cbSize = sizeof(NOTIFYICONDATA);
    nid.hWnd = hwnd;
    nid.uID = 1;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_TRAYICON;
    nid.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    snprintf(nid.szTip, sizeof(nid.szTip), "Rapoo 电池初始化...");
    Shell_NotifyIcon(NIM_ADD, &nid);

    check_battery_and_alert(hwnd);
    SetTimer(hwnd, TIMER_ID, 300000, NULL); // 5分钟

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return 0;
}
