/*
 * simple_launcher.c - Simple Launcher v1.9
 *
 * Features:
 *   - INI-configured buttons with optional icons, separators, admin elevation
 *   - Dark mode with custom admin border color (via color picker)
 *   - Configurable font size and window width
 *   - Always on top, minimize to system tray
 *   - Right-click buttons to Edit / Delete / Move Up / Move Down
 *   - Separator (divider) lines between buttons
 *   - Icon extracted from target .exe (optional per button)
 *   - Remembers last window position
 *   - Version 1.9
 *
 * Compile:
 *
 *   cl simple_launcher.c simple_launcher.res /link user32.lib shell32.lib comdlg32.lib gdi32.lib dwmapi.lib /subsystem:windows /out:launcher.exe
 */

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <commdlg.h>
#include <dwmapi.h>
#include <shellapi.h>

/* ── Forward declarations ────────────────────────────────────────────── */
static void RefreshMainWindow(void);
static void RebuildMenu(void);
static void ApplyDarkBackground(void);
static void SaveAll(void);
static void LoadButtonIcons(void);
static void SetTitleBarDark(HWND hwnd, int dark);

/* ── IDs ─────────────────────────────────────────────────────────────── */
#define ID_ADD_BTN            1
#define ID_BUTTON_BASE        100
#define MAX_BUTTONS           64
#define IDI_APPICON           200

/* Dialog controls */
#define IDC_NAME_EDIT         10
#define IDC_PATH_EDIT         11
#define IDC_ARGS_EDIT         12
#define IDC_BROWSE            13
#define IDC_OK                14
#define IDC_CANCEL            15
#define IDC_ADMIN_CHECK       16
#define IDC_DARK_CHECK        17
#define IDC_TOPMOST_CHECK     18
#define IDC_TRAY_CHECK        19
#define IDC_INFO_TEXT         30
#define IDC_INFO_OK           31
#define IDC_FONT_EDIT         32
#define IDC_WIDTH_EDIT        33
#define IDC_COLOR_BTN         34
#define IDC_SEP_CHECK         35
#define IDC_ICON_CHECK        36
#define IDC_ICON_PATH_EDIT    37
#define IDC_ICON_BROWSE       38
#define IDC_OPACITY_EDIT      39
#define IDC_TITLE_EDIT        40

/* Menu IDs */
#define ID_HELP_INSTRUCTIONS  20
#define ID_HELP_ABOUT         21
#define ID_SETTINGS           22

/* Right-click context menu */
#define IDM_MOVE_UP           300
#define IDM_MOVE_DOWN         301
#define IDM_EDIT_BTN          302
#define IDM_DELETE_BTN        303

/* System tray */
#define WM_TRAYICON           (WM_APP + 1)
#define ID_TRAY_ICON          1
#define IDM_TRAY_RESTORE      400
#define IDM_TRAY_EXIT         401

/* ── Dark mode colors ───────────────────────────────────────────────── */
#define DK_BG       RGB( 28,  28,  28)
#define DK_BTN      RGB( 48,  48,  48)
#define DK_BTN_PRE  RGB( 68,  68,  68)
#define DK_BORDER   RGB( 85,  85,  85)
#define DK_TEXT     RGB(220, 220, 220)
#define DK_MENU_BG  RGB( 32,  32,  32)
#define DK_MENU_HOT RGB( 55,  55,  55)
#define DK_SEP      RGB( 70,  70,  70)
#define LT_SEP      RGB(160, 160, 160)

static const char *g_menuLabels[] = { "Instructions", "Settings", "About" };
static const UINT  g_menuIDs[]    = { ID_HELP_INSTRUCTIONS, ID_SETTINGS, ID_HELP_ABOUT };

/* ── Data ────────────────────────────────────────────────────────────── */
typedef struct {
    char name[256];
    char path[MAX_PATH];
    char args[512];
    char iconPath[MAX_PATH];  /* custom icon file, or empty = use target .exe */
    int  admin;
    int  isSeparator;
    int  showIcon;
} ButtonConfig;

static ButtonConfig g_buttons[MAX_BUTTONS];
static HICON        g_icons[MAX_BUTTONS];
static int          g_count        = 0;

/* Settings */
static int          g_darkMode     = 0;
static int          g_alwaysOnTop  = 0;
static int          g_minToTray    = 0;
static int          g_fontSize     = 9;
static int          g_winWidth     = 300;
static COLORREF     g_adminColor   = RGB(200, 0, 0);
static int          g_winX         = -1;
static int          g_winY         = -1;
static int          g_opacity      = 100;          /* 10–100 % */
static char         g_winTitle[256] = "Simple Launcher";

/* Runtime */
static HWND         g_hwndMain;
static HWND         g_hwndBtns[MAX_BUTTONS];
static HINSTANCE    g_hInst;
static char         g_iniPath[MAX_PATH];
static HWND         g_hwndDlg      = NULL;
static HBRUSH       g_hbrDkBg      = NULL;
static HFONT        g_hFont        = NULL;
static int          g_trayAdded    = 0;
static int          g_editIndex    = -1;
static int          g_ctxIndex     = -1;
static COLORREF     g_settingColor;
static COLORREF     g_customColors[16];

static const char  *g_infoDlgTitle   = NULL;
static const char  *g_infoDlgContent = NULL;

/* ── DWM dark title bar ──────────────────────────────────────────────── */
static void SetTitleBarDark(HWND hwnd, int dark)
{
    BOOL val = dark ? TRUE : FALSE;
    if (FAILED(DwmSetWindowAttribute(hwnd, 20, &val, sizeof(val))))
        DwmSetWindowAttribute(hwnd, 19, &val, sizeof(val));
}

/* ── Opacity ─────────────────────────────────────────────────────────── */
static void ApplyOpacity(void)
{
    LONG ex = GetWindowLong(g_hwndMain, GWL_EXSTYLE);
    if (g_opacity < 100) {
        SetWindowLong(g_hwndMain, GWL_EXSTYLE, ex | WS_EX_LAYERED);
        SetLayeredWindowAttributes(g_hwndMain, 0,
                                   (BYTE)(g_opacity * 255 / 100), LWA_ALPHA);
    } else {
        /* fully opaque — remove layered flag so rendering stays crisp */
        SetWindowLong(g_hwndMain, GWL_EXSTYLE, ex & ~WS_EX_LAYERED);
    }
}
static void RecreateFont(void)
{
    if (g_hFont) { DeleteObject(g_hFont); g_hFont = NULL; }
    HDC hdc = GetDC(NULL);
    int h = -MulDiv(g_fontSize, GetDeviceCaps(hdc, LOGPIXELSY), 72);
    ReleaseDC(NULL, hdc);
    g_hFont = CreateFont(h, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                         DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                         CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Segoe UI");
}

/* ── Tray ────────────────────────────────────────────────────────────── */
static void AddTrayIcon(void)
{
    if (g_trayAdded) return;
    NOTIFYICONDATA nid = {0};
    nid.cbSize           = sizeof(nid);
    nid.hWnd             = g_hwndMain;
    nid.uID              = ID_TRAY_ICON;
    nid.uFlags           = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.hIcon            = LoadIcon(g_hInst, MAKEINTRESOURCE(IDI_APPICON));
    nid.uCallbackMessage = WM_TRAYICON;
    strcpy(nid.szTip, "Simple Launcher");
    Shell_NotifyIcon(NIM_ADD, &nid);
    g_trayAdded = 1;
}

static void RemoveTrayIcon(void)
{
    if (!g_trayAdded) return;
    NOTIFYICONDATA nid = {0};
    nid.cbSize = sizeof(nid);
    nid.hWnd   = g_hwndMain;
    nid.uID    = ID_TRAY_ICON;
    Shell_NotifyIcon(NIM_DELETE, &nid);
    g_trayAdded = 0;
}

/* ── INI ─────────────────────────────────────────────────────────────── */
static void GetIniPath(void)
{
    GetModuleFileName(NULL, g_iniPath, MAX_PATH);
    char *dot = strrchr(g_iniPath, '.');
    if (dot) {
        /* Replace extension in-place — safe because ".ini" <= any extension */
        strcpy(dot, ".ini");
    } else {
        /* No extension: append only if there is room */
        size_t len = strlen(g_iniPath);
        if (len + 4 < MAX_PATH)
            strcat(g_iniPath, ".ini");
    }
}

static void LoadSettings(void)
{
    g_darkMode    = GetPrivateProfileInt("Settings", "DarkMode",         0,              g_iniPath);
    g_alwaysOnTop = GetPrivateProfileInt("Settings", "AlwaysOnTop",      0,              g_iniPath);
    g_minToTray   = GetPrivateProfileInt("Settings", "MinToTray",        0,              g_iniPath);
    g_fontSize    = GetPrivateProfileInt("Settings", "FontSize",         9,              g_iniPath);
    g_winWidth    = GetPrivateProfileInt("Settings", "WindowWidth",      300,            g_iniPath);
    g_adminColor  = (COLORREF)GetPrivateProfileInt("Settings", "AdminBorderColor",
                                                   (int)RGB(200,0,0),   g_iniPath);
    g_winX        = GetPrivateProfileInt("Settings", "WindowX",          -1,             g_iniPath);
    g_winY        = GetPrivateProfileInt("Settings", "WindowY",          -1,             g_iniPath);
    g_opacity     = GetPrivateProfileInt("Settings", "Opacity",          100,            g_iniPath);
    GetPrivateProfileString("Settings", "WindowTitle", "Simple Launcher",
                            g_winTitle, sizeof(g_winTitle), g_iniPath);
    if (g_fontSize < 6)   g_fontSize = 6;
    if (g_fontSize > 72)  g_fontSize = 72;
    if (g_winWidth < 150) g_winWidth = 150;
    if (g_winWidth > 800) g_winWidth = 800;
    if (g_opacity < 10)   g_opacity  = 10;
    if (g_opacity > 100)  g_opacity  = 100;

    /* Clamp saved position to the current virtual screen so the window
       never appears off-screen (e.g. after a monitor is disconnected). */
    if (g_winX != -1 && g_winY != -1) {
        int vx = GetSystemMetrics(SM_XVIRTUALSCREEN);
        int vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
        int vw = GetSystemMetrics(SM_CXVIRTUALSCREEN);
        int vh = GetSystemMetrics(SM_CYVIRTUALSCREEN);
        if (g_winX < vx) g_winX = vx;
        if (g_winY < vy) g_winY = vy;
        if (g_winX > vx + vw - 50) g_winX = vx;   /* 50px minimum visible strip */
        if (g_winY > vy + vh - 50) g_winY = vy;
    }
}

static void LoadButtons(void)
{
    g_count = GetPrivateProfileInt("Buttons", "Count", 0, g_iniPath);
    if (g_count > MAX_BUTTONS) g_count = MAX_BUTTONS;
    for (int i = 0; i < g_count; i++) {
        char sec[32];
        sprintf(sec, "Button%d", i + 1);
        GetPrivateProfileString(sec, "Name", "",  g_buttons[i].name, 256,      g_iniPath);
        GetPrivateProfileString(sec, "Path", "",  g_buttons[i].path, MAX_PATH, g_iniPath);
        GetPrivateProfileString(sec, "Args",     "", g_buttons[i].args,     512,      g_iniPath);
        GetPrivateProfileString(sec, "IconPath", "", g_buttons[i].iconPath, MAX_PATH, g_iniPath);
        g_buttons[i].admin       = GetPrivateProfileInt(sec, "Admin",     0, g_iniPath);
        g_buttons[i].isSeparator = GetPrivateProfileInt(sec, "Separator", 0, g_iniPath);
        g_buttons[i].showIcon    = GetPrivateProfileInt(sec, "ShowIcon",  0, g_iniPath);
    }
}

static void SaveAll(void)
{
    if (g_hwndMain) {
        RECT rc;
        GetWindowRect(g_hwndMain, &rc);
        g_winX = rc.left;
        g_winY = rc.top;
    }
    FILE *f = fopen(g_iniPath, "w");
    if (!f) return;
    fprintf(f, "[Settings]\r\n");
    fprintf(f, "DarkMode=%d\r\n",         g_darkMode);
    fprintf(f, "AlwaysOnTop=%d\r\n",      g_alwaysOnTop);
    fprintf(f, "MinToTray=%d\r\n",        g_minToTray);
    fprintf(f, "FontSize=%d\r\n",         g_fontSize);
    fprintf(f, "WindowWidth=%d\r\n",      g_winWidth);
    fprintf(f, "AdminBorderColor=%d\r\n", (int)g_adminColor);
    fprintf(f, "WindowX=%d\r\n",          g_winX);
    fprintf(f, "WindowY=%d\r\n",          g_winY);
    fprintf(f, "Opacity=%d\r\n",          g_opacity);
    fprintf(f, "WindowTitle=%s\r\n",      g_winTitle);
    fprintf(f, "\r\n");
    fprintf(f, "[Buttons]\r\n");
    fprintf(f, "Count=%d\r\n", g_count);
    for (int i = 0; i < g_count; i++) {
        fprintf(f, "\r\n");
        fprintf(f, "[Button%d]\r\n",   i + 1);
        fprintf(f, "Name=%s\r\n",      g_buttons[i].name);
        fprintf(f, "Path=%s\r\n",      g_buttons[i].path);
        fprintf(f, "Args=%s\r\n",      g_buttons[i].args);
        fprintf(f, "IconPath=%s\r\n",  g_buttons[i].iconPath);
        fprintf(f, "Admin=%d\r\n",     g_buttons[i].admin);
        fprintf(f, "Separator=%d\r\n", g_buttons[i].isSeparator);
        fprintf(f, "ShowIcon=%d\r\n",  g_buttons[i].showIcon);
    }
    fclose(f);
}

/* ── Icons ───────────────────────────────────────────────────────────── */
static void FreeIcons(void)
{
    for (int i = 0; i < MAX_BUTTONS; i++) {
        if (g_icons[i]) { DestroyIcon(g_icons[i]); g_icons[i] = NULL; }
    }
}

static void LoadButtonIcons(void)
{
    FreeIcons();
    for (int i = 0; i < g_count; i++) {
        if (!g_buttons[i].showIcon || g_buttons[i].isSeparator) continue;

        const char *src = g_buttons[i].iconPath[0] ? g_buttons[i].iconPath
                                                    : g_buttons[i].path;
        if (!src[0]) continue;

        /* Try loading as a standalone .ico / .png first */
        HICON hIco = (HICON)LoadImage(NULL, src, IMAGE_ICON,
                                      16, 16, LR_LOADFROMFILE);
        if (hIco) { g_icons[i] = hIco; continue; }

        /* Fall back to extracting from .exe */
        HICON hLg = NULL, hSm = NULL;
        ExtractIconEx(src, 0, &hLg, &hSm, 1);
        if (hSm) { g_icons[i] = hSm; if (hLg) DestroyIcon(hLg); }
        else if (hLg) { g_icons[i] = hLg; }
    }
}

/* ── Owner-draw ──────────────────────────────────────────────────────── */
static void DrawButton(LPDRAWITEMSTRUCT dis, int idx)
{
    /* ── separator ── */
    if (idx >= 0 && idx < g_count && g_buttons[idx].isSeparator) {
        RECT rc = dis->rcItem;
        COLORREF bgCol = g_darkMode ? DK_BG : GetSysColor(COLOR_BTNFACE);
        HBRUSH hbr = CreateSolidBrush(bgCol);
        FillRect(dis->hDC, &rc, hbr);
        DeleteObject(hbr);
        int midY = (rc.top + rc.bottom) / 2;
        HPEN hPen = CreatePen(PS_SOLID, 1, g_darkMode ? DK_SEP : LT_SEP);
        HPEN hOld = (HPEN)SelectObject(dis->hDC, hPen);
        MoveToEx(dis->hDC, rc.left + 6, midY, NULL);
        LineTo(dis->hDC,   rc.right - 6, midY);
        SelectObject(dis->hDC, hOld);
        DeleteObject(hPen);
        return;
    }

    /* ── normal button ── */
    RECT rc      = dis->rcItem;
    BOOL pressed = (dis->itemState & ODS_SELECTED);
    int  isAdmin = (idx >= 0 && idx < g_count) ? g_buttons[idx].admin : 0;

    /* background */
    if (g_darkMode) {
        HBRUSH hbr = CreateSolidBrush(pressed ? DK_BTN_PRE : DK_BTN);
        FillRect(dis->hDC, &rc, hbr);
        DeleteObject(hbr);
        HPEN   hPen  = CreatePen(PS_SOLID, 1, DK_BORDER);
        HPEN   hOldP = (HPEN)SelectObject(dis->hDC, hPen);
        HBRUSH hNull = (HBRUSH)SelectObject(dis->hDC, GetStockObject(NULL_BRUSH));
        Rectangle(dis->hDC, rc.left, rc.top, rc.right, rc.bottom);
        SelectObject(dis->hDC, hOldP);
        SelectObject(dis->hDC, hNull);
        DeleteObject(hPen);
    } else {
        FillRect(dis->hDC, &rc, (HBRUSH)(COLOR_BTNFACE + 1));
        DrawEdge(dis->hDC, &rc, pressed ? EDGE_SUNKEN : EDGE_RAISED, BF_RECT);
    }

    /* admin/custom color border */
    if (isAdmin) {
        HPEN   hPen  = CreatePen(PS_SOLID, 2, g_adminColor);
        HPEN   hOldP = (HPEN)SelectObject(dis->hDC, hPen);
        HBRUSH hNull = (HBRUSH)SelectObject(dis->hDC, GetStockObject(NULL_BRUSH));
        RECT   inner = { rc.left+3, rc.top+3, rc.right-3, rc.bottom-3 };
        Rectangle(dis->hDC, inner.left, inner.top, inner.right, inner.bottom);
        SelectObject(dis->hDC, hOldP);
        SelectObject(dis->hDC, hNull);
        DeleteObject(hPen);
    }

    /* icon */
    int textLeft = rc.left;
    if (idx >= 0 && idx < g_count && g_icons[idx]) {
        int sz = 16, ix = rc.left + 6;
        int iy = rc.top + (rc.bottom - rc.top - sz) / 2;
        DrawIconEx(dis->hDC, ix, iy, g_icons[idx], sz, sz, 0, NULL, DI_NORMAL);
        textLeft = ix + sz + 4;
    }

    /* label */
    SetBkMode(dis->hDC, TRANSPARENT);
    SetTextColor(dis->hDC, g_darkMode ? DK_TEXT : GetSysColor(COLOR_BTNTEXT));
    HFONT hOldFont = NULL;
    if (g_hFont) hOldFont = (HFONT)SelectObject(dis->hDC, g_hFont);
    RECT textRc = { textLeft, rc.top, rc.right, rc.bottom };
    if (pressed) OffsetRect(&textRc, 1, 1);
    const char *label = (idx == -1) ? "+ Add Button" :
                        (idx >= 0 && idx < g_count) ? g_buttons[idx].name : "";
    DrawText(dis->hDC, label, -1, &textRc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    if (hOldFont) SelectObject(dis->hDC, hOldFont);
    if (dis->itemState & ODS_FOCUS) DrawFocusRect(dis->hDC, &rc);
}

/* ── Dark background ─────────────────────────────────────────────────── */
static void ApplyDarkBackground(void)
{
    if (g_hbrDkBg) { DeleteObject(g_hbrDkBg); g_hbrDkBg = NULL; }
    if (g_darkMode) g_hbrDkBg = CreateSolidBrush(DK_BG);
}

/* ── Menu ────────────────────────────────────────────────────────────── */
static void RebuildMenu(void)
{
    HMENU hOld = GetMenu(g_hwndMain);
    if (hOld) { SetMenu(g_hwndMain, NULL); DestroyMenu(hOld); }
    HMENU hBar = CreateMenu();
    int n = sizeof(g_menuIDs) / sizeof(g_menuIDs[0]);
    for (int i = 0; i < n; i++) {
        if (g_darkMode)
            AppendMenu(hBar, MF_OWNERDRAW, g_menuIDs[i], (LPCSTR)g_menuLabels[i]);
        else
            AppendMenu(hBar, MF_STRING,    g_menuIDs[i], g_menuLabels[i]);
    }
    SetMenu(g_hwndMain, hBar);
    DrawMenuBar(g_hwndMain);
}

/* ── Layout ──────────────────────────────────────────────────────────── */
static void RefreshMainWindow(void)
{
    for (int i = 0; i < MAX_BUTTONS; i++) {
        if (g_hwndBtns[i]) { DestroyWindow(g_hwndBtns[i]); g_hwndBtns[i] = NULL; }
    }
    int btnW = g_winWidth - 20;
    int y    = 10;
    for (int i = 0; i < g_count; i++) {
        int h = g_buttons[i].isSeparator ? 14 : 26;
        DWORD style = WS_VISIBLE | WS_CHILD | BS_OWNERDRAW;
        /* separators: NOT disabled so right-click still fires WM_CONTEXTMENU */
        g_hwndBtns[i] = CreateWindow("BUTTON", g_buttons[i].name, style,
                                     10, y, btnW, h, g_hwndMain,
                                     (HMENU)(UINT_PTR)(ID_BUTTON_BASE + i), g_hInst, NULL);
        if (g_hFont) SendMessage(g_hwndBtns[i], WM_SETFONT, (WPARAM)g_hFont, FALSE);
        y += h + 5;
    }
    HWND hAdd = GetDlgItem(g_hwndMain, ID_ADD_BTN);
    SetWindowLongPtr(hAdd, GWL_STYLE, WS_VISIBLE | WS_CHILD | BS_OWNERDRAW);
    SetWindowPos(hAdd, HWND_TOP, 10, y, btnW, 26, SWP_SHOWWINDOW | SWP_FRAMECHANGED);
    if (g_hFont) SendMessage(hAdd, WM_SETFONT, (WPARAM)g_hFont, FALSE);
    int clientH = y + 31 + 10;
    if (clientH < 80) clientH = 80;
    RECT rc = { 0, 0, g_winWidth, clientH };
    AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, TRUE);
    SetWindowPos(g_hwndMain, NULL, 0, 0,
                 rc.right - rc.left, rc.bottom - rc.top, SWP_NOMOVE | SWP_NOZORDER);
    InvalidateRect(g_hwndMain, NULL, TRUE);
}

/* ── Right-click context menu ────────────────────────────────────────── */
static void ShowButtonContextMenu(HWND hwnd, int idx, POINT pt)
{
    HMENU hMenu = CreatePopupMenu();
    AppendMenu(hMenu, MF_STRING | (idx == 0 ? MF_GRAYED : 0),
               IDM_MOVE_UP, "Move Up");
    AppendMenu(hMenu, MF_STRING | (idx == g_count - 1 ? MF_GRAYED : 0),
               IDM_MOVE_DOWN, "Move Down");
    AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);
    if (!g_buttons[idx].isSeparator)
        AppendMenu(hMenu, MF_STRING, IDM_EDIT_BTN, "Edit...");
    AppendMenu(hMenu, MF_STRING, IDM_DELETE_BTN, "Delete");
    SetForegroundWindow(hwnd);
    TrackPopupMenu(hMenu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, NULL);
    DestroyMenu(hMenu);
}

/* ── Shared dark coloring helper for dialogs ───────────────────────── */
static LRESULT HandleDlgDarkColor(HWND hwnd, UINT msg, WPARAM wParam, HBRUSH *phBr)
{
    if (!g_darkMode) return -1;  /* -1 = not handled */
    if (msg == WM_ERASEBKGND) {
        HDC hdc = (HDC)wParam; RECT rc;
        GetClientRect(hwnd, &rc);
        if (!*phBr) *phBr = CreateSolidBrush(DK_BG);
        FillRect(hdc, &rc, *phBr);
        return 1;
    }
    if (msg == WM_CTLCOLORSTATIC || msg == WM_CTLCOLORBTN || msg == WM_CTLCOLOREDIT) {
        HDC hdc = (HDC)wParam;
        SetTextColor(hdc, DK_TEXT);
        SetBkColor(hdc, DK_BG);
        if (!*phBr) *phBr = CreateSolidBrush(DK_BG);
        return (LRESULT)*phBr;
    }
    return -1;
}

/* ── Settings dialog ─────────────────────────────────────────────────── */
static HBRUSH g_hbrSettingsBg = NULL;

static LRESULT CALLBACK SettingsDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    LRESULT dark = HandleDlgDarkColor(hwnd, msg, wParam, &g_hbrSettingsBg);
    if (dark != -1) return dark;

    switch (msg) {
    case WM_DRAWITEM: {
        LPDRAWITEMSTRUCT dis = (LPDRAWITEMSTRUCT)lParam;
        if ((int)dis->CtlID == IDC_COLOR_BTN) {
            HBRUSH hbr = CreateSolidBrush(g_settingColor);
            FillRect(dis->hDC, &dis->rcItem, hbr);
            DeleteObject(hbr);
            FrameRect(dis->hDC, &dis->rcItem, (HBRUSH)GetStockObject(BLACK_BRUSH));
            return TRUE;
        }
        return DefWindowProc(hwnd, msg, wParam, lParam);
    }
    case WM_DESTROY:
        if (g_hbrSettingsBg) { DeleteObject(g_hbrSettingsBg); g_hbrSettingsBg = NULL; }
        g_hwndDlg = NULL;
        return 0;

    case WM_CREATE:
        g_settingColor = g_adminColor;
        /* ── Appearance ── */
        CreateWindow("STATIC", "Appearance", WS_VISIBLE|WS_CHILD,
                     10, 10, 200, 16, hwnd, NULL, g_hInst, NULL);
        CreateWindow("BUTTON", "Dark mode", WS_VISIBLE|WS_CHILD|BS_AUTOCHECKBOX,
                     10, 30, 200, 20, hwnd, (HMENU)IDC_DARK_CHECK, g_hInst, NULL);
        SendDlgItemMessage(hwnd, IDC_DARK_CHECK, BM_SETCHECK,
                           g_darkMode ? BST_CHECKED : BST_UNCHECKED, 0);
        CreateWindow("STATIC", "Admin border color:", WS_VISIBLE|WS_CHILD,
                     10, 57, 138, 18, hwnd, NULL, g_hInst, NULL);
        CreateWindow("BUTTON", "", WS_VISIBLE|WS_CHILD|BS_OWNERDRAW,
                     152, 54, 36, 22, hwnd, (HMENU)IDC_COLOR_BTN, g_hInst, NULL);
        /* ── Font ── */
        CreateWindow("STATIC", "Font size:", WS_VISIBLE|WS_CHILD,
                     10, 84, 80, 18, hwnd, NULL, g_hInst, NULL);
        { char buf[8]; sprintf(buf, "%d", g_fontSize);
          CreateWindow("EDIT", buf, WS_VISIBLE|WS_CHILD|WS_BORDER|ES_NUMBER,
                       95, 82, 40, 20, hwnd, (HMENU)IDC_FONT_EDIT, g_hInst, NULL); }
        CreateWindow("STATIC", "pt", WS_VISIBLE|WS_CHILD,
                     140, 84, 20, 18, hwnd, NULL, g_hInst, NULL);
        /* ── Layout ── */
        CreateWindow("STATIC", "Layout", WS_VISIBLE|WS_CHILD,
                     10, 113, 200, 16, hwnd, NULL, g_hInst, NULL);
        CreateWindow("STATIC", "Window width:", WS_VISIBLE|WS_CHILD,
                     10, 133, 100, 18, hwnd, NULL, g_hInst, NULL);
        { char buf[8]; sprintf(buf, "%d", g_winWidth);
          CreateWindow("EDIT", buf, WS_VISIBLE|WS_CHILD|WS_BORDER|ES_NUMBER,
                       115, 131, 55, 20, hwnd, (HMENU)IDC_WIDTH_EDIT, g_hInst, NULL); }
        CreateWindow("STATIC", "px", WS_VISIBLE|WS_CHILD,
                     175, 133, 20, 18, hwnd, NULL, g_hInst, NULL);
        /* ── Behaviour ── */
        CreateWindow("STATIC", "Behaviour", WS_VISIBLE|WS_CHILD,
                     10, 162, 200, 16, hwnd, NULL, g_hInst, NULL);
        CreateWindow("BUTTON", "Always on top", WS_VISIBLE|WS_CHILD|BS_AUTOCHECKBOX,
                     10, 182, 200, 20, hwnd, (HMENU)IDC_TOPMOST_CHECK, g_hInst, NULL);
        SendDlgItemMessage(hwnd, IDC_TOPMOST_CHECK, BM_SETCHECK,
                           g_alwaysOnTop ? BST_CHECKED : BST_UNCHECKED, 0);
        CreateWindow("BUTTON", "Minimize to system tray", WS_VISIBLE|WS_CHILD|BS_AUTOCHECKBOX,
                     10, 206, 200, 20, hwnd, (HMENU)IDC_TRAY_CHECK, g_hInst, NULL);
        SendDlgItemMessage(hwnd, IDC_TRAY_CHECK, BM_SETCHECK,
                           g_minToTray ? BST_CHECKED : BST_UNCHECKED, 0);
        /* ── Window ── */
        CreateWindow("STATIC", "Window", WS_VISIBLE|WS_CHILD,
                     10, 237, 200, 16, hwnd, NULL, g_hInst, NULL);
        CreateWindow("STATIC", "Title:", WS_VISIBLE|WS_CHILD,
                     10, 257, 40, 18, hwnd, NULL, g_hInst, NULL);
        CreateWindow("EDIT", g_winTitle, WS_VISIBLE|WS_CHILD|WS_BORDER|ES_AUTOHSCROLL,
                     55, 255, 220, 20, hwnd, (HMENU)IDC_TITLE_EDIT, g_hInst, NULL);
        CreateWindow("STATIC", "Opacity:", WS_VISIBLE|WS_CHILD,
                     10, 283, 55, 18, hwnd, NULL, g_hInst, NULL);
        { char buf[8]; sprintf(buf, "%d", g_opacity);
          CreateWindow("EDIT", buf, WS_VISIBLE|WS_CHILD|WS_BORDER|ES_NUMBER,
                       70, 281, 40, 20, hwnd, (HMENU)IDC_OPACITY_EDIT, g_hInst, NULL); }
        CreateWindow("STATIC", "% (10-100)", WS_VISIBLE|WS_CHILD,
                     115, 283, 75, 18, hwnd, NULL, g_hInst, NULL);
        /* ── Buttons ── */
        CreateWindow("BUTTON", "Save",   WS_VISIBLE|WS_CHILD|BS_DEFPUSHBUTTON,
                     108, 314, 80, 26, hwnd, (HMENU)IDC_OK,     g_hInst, NULL);
        CreateWindow("BUTTON", "Cancel", WS_VISIBLE|WS_CHILD|BS_PUSHBUTTON,
                     198, 314, 80, 26, hwnd, (HMENU)IDC_CANCEL, g_hInst, NULL);
        return 0;

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDC_COLOR_BTN: {
            CHOOSECOLOR cc = {0};
            cc.lStructSize  = sizeof(cc);
            cc.hwndOwner    = hwnd;
            cc.lpCustColors = g_customColors;
            cc.rgbResult    = g_settingColor;
            cc.Flags        = CC_FULLOPEN | CC_RGBINIT;
            if (ChooseColor(&cc)) {
                g_settingColor = cc.rgbResult;
                InvalidateRect(GetDlgItem(hwnd, IDC_COLOR_BTN), NULL, TRUE);
            }
            break;
        }
        case IDC_OK: {
            int newDark    = (SendDlgItemMessage(hwnd, IDC_DARK_CHECK,    BM_GETCHECK, 0, 0) == BST_CHECKED) ? 1 : 0;
            int newTopmost = (SendDlgItemMessage(hwnd, IDC_TOPMOST_CHECK, BM_GETCHECK, 0, 0) == BST_CHECKED) ? 1 : 0;
            int newTray    = (SendDlgItemMessage(hwnd, IDC_TRAY_CHECK,    BM_GETCHECK, 0, 0) == BST_CHECKED) ? 1 : 0;
            char buf[256];
            GetDlgItemText(hwnd, IDC_FONT_EDIT,  buf, 16);
            int newFont = atoi(buf); if (newFont < 6) newFont = 6; if (newFont > 72) newFont = 72;
            GetDlgItemText(hwnd, IDC_WIDTH_EDIT, buf, 16);
            int newWidth = atoi(buf); if (newWidth < 150) newWidth = 150; if (newWidth > 800) newWidth = 800;
            GetDlgItemText(hwnd, IDC_OPACITY_EDIT, buf, 16);
            int newOpacity = atoi(buf); if (newOpacity < 10) newOpacity = 10; if (newOpacity > 100) newOpacity = 100;
            char newTitle[256];
            GetDlgItemText(hwnd, IDC_TITLE_EDIT, newTitle, 256);
            if (!newTitle[0]) strcpy(newTitle, "Simple Launcher");

            int needRefresh = 0;
            if (newDark != g_darkMode) {
                g_darkMode = newDark;
                ApplyDarkBackground();
                SetTitleBarDark(g_hwndMain, g_darkMode);
                RebuildMenu();
                needRefresh = 1;
            }
            if (newTopmost != g_alwaysOnTop) {
                g_alwaysOnTop = newTopmost;
                SetWindowPos(g_hwndMain, g_alwaysOnTop ? HWND_TOPMOST : HWND_NOTOPMOST,
                             0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
            }
            g_minToTray = newTray;
            if (g_settingColor != g_adminColor) { g_adminColor = g_settingColor; needRefresh = 1; }
            if (newFont    != g_fontSize)  { g_fontSize  = newFont;  RecreateFont(); needRefresh = 1; }
            if (newWidth   != g_winWidth)  { g_winWidth  = newWidth; needRefresh = 1; }
            if (newOpacity != g_opacity)   { g_opacity   = newOpacity; ApplyOpacity(); }
            if (strcmp(newTitle, g_winTitle) != 0) {
                strcpy(g_winTitle, newTitle);
                SetWindowText(g_hwndMain, g_winTitle);
            }
            SaveAll();
            if (needRefresh) RefreshMainWindow();
            DestroyWindow(hwnd);
            g_hwndDlg = NULL;
            break;
        }
        case IDC_CANCEL:
            DestroyWindow(hwnd);
            g_hwndDlg = NULL;
            break;
        }
        return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

/* ── Add / Edit dialog ───────────────────────────────────────────────── */
static HBRUSH g_hbrAddBg = NULL;

static LRESULT CALLBACK AddDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    LRESULT dark = HandleDlgDarkColor(hwnd, msg, wParam, &g_hbrAddBg);
    if (dark != -1) return dark;

    switch (msg) {
    case WM_CREATE: {
        int edit = (g_editIndex >= 0);
        ButtonConfig *bc = edit ? &g_buttons[g_editIndex] : NULL;

        CreateWindow("STATIC", "Button display name:", WS_VISIBLE|WS_CHILD,
                     10, 10, 200, 18, hwnd, NULL, g_hInst, NULL);
        CreateWindow("EDIT", edit ? bc->name : "",
                     WS_VISIBLE|WS_CHILD|WS_BORDER|ES_AUTOHSCROLL,
                     10, 30, 370, 24, hwnd, (HMENU)IDC_NAME_EDIT, g_hInst, NULL);

        CreateWindow("STATIC", "Path to launch:", WS_VISIBLE|WS_CHILD,
                     10, 65, 200, 18, hwnd, NULL, g_hInst, NULL);
        CreateWindow("EDIT", edit ? bc->path : "",
                     WS_VISIBLE|WS_CHILD|WS_BORDER|ES_AUTOHSCROLL,
                     10, 85, 285, 24, hwnd, (HMENU)IDC_PATH_EDIT, g_hInst, NULL);
        CreateWindow("BUTTON", "Browse...", WS_VISIBLE|WS_CHILD|BS_PUSHBUTTON,
                     305, 85, 75, 24, hwnd, (HMENU)IDC_BROWSE, g_hInst, NULL);

        CreateWindow("STATIC", "Arguments / options (optional):", WS_VISIBLE|WS_CHILD,
                     10, 120, 260, 18, hwnd, NULL, g_hInst, NULL);
        CreateWindow("EDIT", edit ? bc->args : "",
                     WS_VISIBLE|WS_CHILD|WS_BORDER|ES_AUTOHSCROLL,
                     10, 140, 370, 24, hwnd, (HMENU)IDC_ARGS_EDIT, g_hInst, NULL);

        CreateWindow("BUTTON", "Run as administrator",
                     WS_VISIBLE|WS_CHILD|BS_AUTOCHECKBOX,
                     10, 175, 220, 22, hwnd, (HMENU)IDC_ADMIN_CHECK, g_hInst, NULL);
        if (edit && bc->admin) SendDlgItemMessage(hwnd, IDC_ADMIN_CHECK, BM_SETCHECK, BST_CHECKED, 0);

        CreateWindow("BUTTON", "Show icon from program",
                     WS_VISIBLE|WS_CHILD|BS_AUTOCHECKBOX,
                     10, 200, 220, 20, hwnd, (HMENU)IDC_ICON_CHECK, g_hInst, NULL);
        if (edit && bc->showIcon) SendDlgItemMessage(hwnd, IDC_ICON_CHECK, BM_SETCHECK, BST_CHECKED, 0);

        CreateWindow("STATIC", "Custom icon (ICO/PNG, optional):",
                     WS_VISIBLE|WS_CHILD, 10, 228, 220, 18, hwnd, NULL, g_hInst, NULL);
        CreateWindow("EDIT", edit ? bc->iconPath : "",
                     WS_VISIBLE|WS_CHILD|WS_BORDER|ES_AUTOHSCROLL,
                     10, 248, 285, 22, hwnd, (HMENU)IDC_ICON_PATH_EDIT, g_hInst, NULL);
        CreateWindow("BUTTON", "Browse...", WS_VISIBLE|WS_CHILD|BS_PUSHBUTTON,
                     305, 248, 75, 22, hwnd, (HMENU)IDC_ICON_BROWSE, g_hInst, NULL);

        CreateWindow("BUTTON", "Separator (divider line)",
                     WS_VISIBLE|WS_CHILD|BS_AUTOCHECKBOX,
                     10, 278, 220, 20, hwnd, (HMENU)IDC_SEP_CHECK, g_hInst, NULL);
        if (edit && bc->isSeparator) {
            SendDlgItemMessage(hwnd, IDC_SEP_CHECK, BM_SETCHECK, BST_CHECKED, 0);
            EnableWindow(GetDlgItem(hwnd, IDC_NAME_EDIT),       FALSE);
            EnableWindow(GetDlgItem(hwnd, IDC_PATH_EDIT),       FALSE);
            EnableWindow(GetDlgItem(hwnd, IDC_ARGS_EDIT),       FALSE);
            EnableWindow(GetDlgItem(hwnd, IDC_BROWSE),          FALSE);
            EnableWindow(GetDlgItem(hwnd, IDC_ADMIN_CHECK),     FALSE);
            EnableWindow(GetDlgItem(hwnd, IDC_ICON_CHECK),      FALSE);
            EnableWindow(GetDlgItem(hwnd, IDC_ICON_PATH_EDIT),  FALSE);
            EnableWindow(GetDlgItem(hwnd, IDC_ICON_BROWSE),     FALSE);
        }

        CreateWindow("BUTTON", edit ? "Save" : "Add",
                     WS_VISIBLE|WS_CHILD|BS_DEFPUSHBUTTON,
                     210, 312, 80, 28, hwnd, (HMENU)IDC_OK, g_hInst, NULL);
        CreateWindow("BUTTON", "Cancel",
                     WS_VISIBLE|WS_CHILD|BS_PUSHBUTTON,
                     300, 312, 80, 28, hwnd, (HMENU)IDC_CANCEL, g_hInst, NULL);
        return 0;
    }

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDC_SEP_CHECK: {
            BOOL sep = (SendDlgItemMessage(hwnd, IDC_SEP_CHECK, BM_GETCHECK, 0, 0) == BST_CHECKED);
            EnableWindow(GetDlgItem(hwnd, IDC_NAME_EDIT),      !sep);
            EnableWindow(GetDlgItem(hwnd, IDC_PATH_EDIT),      !sep);
            EnableWindow(GetDlgItem(hwnd, IDC_ARGS_EDIT),      !sep);
            EnableWindow(GetDlgItem(hwnd, IDC_BROWSE),         !sep);
            EnableWindow(GetDlgItem(hwnd, IDC_ADMIN_CHECK),    !sep);
            EnableWindow(GetDlgItem(hwnd, IDC_ICON_CHECK),     !sep);
            EnableWindow(GetDlgItem(hwnd, IDC_ICON_PATH_EDIT), !sep);
            EnableWindow(GetDlgItem(hwnd, IDC_ICON_BROWSE),    !sep);
            break;
        }
        case IDC_ICON_BROWSE: {
            OPENFILENAME ofn;
            char file[MAX_PATH] = "";
            ZeroMemory(&ofn, sizeof(ofn));
            ofn.lStructSize = sizeof(OPENFILENAME);
            ofn.hwndOwner   = hwnd;
            ofn.lpstrFile   = file;
            ofn.nMaxFile    = MAX_PATH;
            ofn.lpstrFilter = "Icon files\0*.ico;*.png\0All Files\0*.*\0";
            ofn.Flags       = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
            if (GetOpenFileName(&ofn)) SetDlgItemText(hwnd, IDC_ICON_PATH_EDIT, file);
            break;
        }
        case IDC_BROWSE: {
            OPENFILENAME ofn;
            char file[MAX_PATH] = "";
            ZeroMemory(&ofn, sizeof(ofn));
            ofn.lStructSize = sizeof(OPENFILENAME);
            ofn.hwndOwner   = hwnd;
            ofn.lpstrFile   = file;
            ofn.nMaxFile    = MAX_PATH;
            ofn.lpstrFilter = "Programs\0*.exe\0All Files\0*.*\0";
            ofn.Flags       = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
            if (GetOpenFileName(&ofn)) SetDlgItemText(hwnd, IDC_PATH_EDIT, file);
            break;
        }
        case IDC_OK: {
            int isSep = (SendDlgItemMessage(hwnd, IDC_SEP_CHECK, BM_GETCHECK, 0, 0) == BST_CHECKED) ? 1 : 0;
            char name[256], path[MAX_PATH], args[512];
            GetDlgItemText(hwnd, IDC_NAME_EDIT, name, 256);
            GetDlgItemText(hwnd, IDC_PATH_EDIT, path, MAX_PATH);
            GetDlgItemText(hwnd, IDC_ARGS_EDIT, args, 512);
            if (!isSep && (!name[0] || !path[0])) {
                MessageBox(hwnd, "Name and Path are required.", "Missing info", MB_OK | MB_ICONWARNING);
                break;
            }
            int ti = (g_editIndex >= 0) ? g_editIndex : g_count;
            if (ti >= MAX_BUTTONS) {
                MessageBox(hwnd, "Maximum number of buttons reached.", "Error", MB_OK | MB_ICONWARNING);
                break;
            }
            if (isSep) {
                strcpy(g_buttons[ti].name, "---");
                g_buttons[ti].path[0]     = 0;
                g_buttons[ti].args[0]     = 0;
                g_buttons[ti].iconPath[0] = 0;
                g_buttons[ti].admin       = 0;
                g_buttons[ti].showIcon    = 0;
            } else {
                char iconPath[MAX_PATH];
                GetDlgItemText(hwnd, IDC_ICON_PATH_EDIT, iconPath, MAX_PATH);
                strcpy(g_buttons[ti].name,     name);
                strcpy(g_buttons[ti].path,     path);
                strcpy(g_buttons[ti].args,     args);
                strcpy(g_buttons[ti].iconPath, iconPath);
                g_buttons[ti].admin    = (SendDlgItemMessage(hwnd, IDC_ADMIN_CHECK, BM_GETCHECK, 0, 0) == BST_CHECKED) ? 1 : 0;
                g_buttons[ti].showIcon = (SendDlgItemMessage(hwnd, IDC_ICON_CHECK,  BM_GETCHECK, 0, 0) == BST_CHECKED) ? 1 : 0;
            }
            g_buttons[ti].isSeparator = isSep;
            if (g_editIndex < 0) g_count++;
            LoadButtonIcons();
            SaveAll();
            RefreshMainWindow();
            DestroyWindow(hwnd);
            g_hwndDlg  = NULL;
            g_editIndex = -1;
            break;
        }
        case IDC_CANCEL:
            DestroyWindow(hwnd);
            g_hwndDlg  = NULL;
            g_editIndex = -1;
            break;
        }
        return 0;

    case WM_DESTROY:
        if (g_hbrAddBg) { DeleteObject(g_hbrAddBg); g_hbrAddBg = NULL; }
        g_hwndDlg  = NULL;
        g_editIndex = -1;
        return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

/* ── Info dialog (Instructions / About) ─────────────────────────────── */
static HBRUSH g_hbrInfoBg = NULL;

static LRESULT CALLBACK InfoDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    LRESULT dark = HandleDlgDarkColor(hwnd, msg, wParam, &g_hbrInfoBg);
    if (dark != -1) return dark;

    switch (msg) {
    case WM_CREATE: {
        HWND hEdit = CreateWindowEx(WS_EX_CLIENTEDGE, "EDIT", g_infoDlgContent,
            WS_VISIBLE|WS_CHILD|WS_VSCROLL|ES_MULTILINE|ES_READONLY|ES_AUTOVSCROLL,
            10, 10, 460, 300, hwnd, (HMENU)IDC_INFO_TEXT, g_hInst, NULL);
        if (g_darkMode) SendMessage(hEdit, 0x0443, 0, (LPARAM)DK_BG);
        CreateWindow("BUTTON", "OK", WS_VISIBLE|WS_CHILD|BS_DEFPUSHBUTTON,
                     195, 320, 90, 28, hwnd, (HMENU)IDC_INFO_OK, g_hInst, NULL);
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == IDC_INFO_OK) { DestroyWindow(hwnd); g_hwndDlg = NULL; }
        return 0;
    case WM_DESTROY:
        if (g_hbrInfoBg) { DeleteObject(g_hbrInfoBg); g_hbrInfoBg = NULL; }
        g_hwndDlg = NULL;
        return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

static void ShowInfoDialog(HWND parent, const char *title, const char *content)
{
    if (g_hwndDlg) { SetForegroundWindow(g_hwndDlg); return; }
    g_infoDlgTitle   = title;
    g_infoDlgContent = content;
    g_hwndDlg = CreateWindowEx(WS_EX_DLGMODALFRAME, "InfoDlgClass", title,
                                WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
                                CW_USEDEFAULT, CW_USEDEFAULT, 494, 395,
                                parent, NULL, g_hInst, NULL);
    SetTitleBarDark(g_hwndDlg, g_darkMode);
    ShowWindow(g_hwndDlg, SW_SHOW);
    UpdateWindow(g_hwndDlg);
}

/* ── Dark menu bar repaint (shared by WM_NCPAINT + WM_NCACTIVATE) ──── */
static void RepaintMenuBar(HWND hwnd)
{
    HMENU hMenu = GetMenu(hwnd); int cnt = hMenu ? GetMenuItemCount(hMenu) : 0;
    MENUBARINFO mbiBar = { sizeof(mbiBar) };
    if (cnt <= 0 || !GetMenuBarInfo(hwnd, OBJID_MENU, 0, &mbiBar)) return;
    RECT rcWin; GetWindowRect(hwnd, &rcWin);
    HDC hdc = GetWindowDC(hwnd);
    RECT rcBar = { mbiBar.rcBar.left   - rcWin.left, mbiBar.rcBar.top    - rcWin.top,
                   mbiBar.rcBar.right  - rcWin.left, mbiBar.rcBar.bottom - rcWin.top };
    HBRUSH hbr = CreateSolidBrush(DK_MENU_BG); FillRect(hdc, &rcBar, hbr); DeleteObject(hbr);
    SetBkMode(hdc, TRANSPARENT); SetTextColor(hdc, DK_TEXT);
    HFONT hOld = (HFONT)SelectObject(hdc, (HFONT)GetStockObject(DEFAULT_GUI_FONT));
    int n = sizeof(g_menuLabels) / sizeof(g_menuLabels[0]);
    for (int i = 0; i < cnt && i < n; i++) {
        MENUBARINFO mbi = { sizeof(mbi) };
        if (!GetMenuBarInfo(hwnd, OBJID_MENU, i + 1, &mbi)) continue;
        RECT rc = { mbi.rcBar.left  - rcWin.left, mbi.rcBar.top    - rcWin.top,
                    mbi.rcBar.right - rcWin.left, mbi.rcBar.bottom - rcWin.top };
        DrawText(hdc, g_menuLabels[i], -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
    SelectObject(hdc, hOld); ReleaseDC(hwnd, hdc);
}

/* ── Main window proc ────────────────────────────────────────────────── */
static LRESULT CALLBACK MainProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {

    case WM_CREATE:
        CreateWindow("BUTTON", "+ Add Button", WS_VISIBLE|WS_CHILD|BS_OWNERDRAW,
                     10, 10, g_winWidth - 20, 26,
                     hwnd, (HMENU)ID_ADD_BTN, g_hInst, NULL);
        return 0;

    case WM_CLOSE:
        if (g_minToTray) {
            ShowWindow(hwnd, SW_HIDE);
            AddTrayIcon();
        } else {
            SaveAll();
            DestroyWindow(hwnd);
        }
        return 0;

    case WM_TRAYICON:
        if (lParam == WM_LBUTTONDBLCLK) {
            RemoveTrayIcon();
            ShowWindow(hwnd, SW_RESTORE);
            SetForegroundWindow(hwnd);
        } else if (lParam == WM_RBUTTONUP) {
            POINT pt; GetCursorPos(&pt);
            HMENU hM = CreatePopupMenu();
            AppendMenu(hM, MF_STRING,   IDM_TRAY_RESTORE, "Restore");
            AppendMenu(hM, MF_SEPARATOR, 0, NULL);
            AppendMenu(hM, MF_STRING,   IDM_TRAY_EXIT, "Exit");
            SetForegroundWindow(hwnd);
            int cmd = TrackPopupMenu(hM, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, NULL);
            DestroyMenu(hM);
            if (cmd == IDM_TRAY_RESTORE) {
                RemoveTrayIcon(); ShowWindow(hwnd, SW_RESTORE); SetForegroundWindow(hwnd);
            } else if (cmd == IDM_TRAY_EXIT) {
                RemoveTrayIcon(); SaveAll(); DestroyWindow(hwnd);
            }
        }
        return 0;

    case WM_CONTEXTMENU: {
        /* wParam = HWND that was right-clicked */
        HWND hClicked = (HWND)wParam;
        POINT pt = { (short)LOWORD(lParam), (short)HIWORD(lParam) };
        g_ctxIndex = -1;
        for (int i = 0; i < g_count; i++) {
            if (g_hwndBtns[i] == hClicked) { g_ctxIndex = i; break; }
        }
        if (g_ctxIndex >= 0) ShowButtonContextMenu(hwnd, g_ctxIndex, pt);
        return 0;
    }

    case WM_ERASEBKGND:
        if (g_darkMode) {
            HDC hdc = (HDC)wParam; RECT rc;
            GetClientRect(hwnd, &rc);
            if (!g_hbrDkBg) g_hbrDkBg = CreateSolidBrush(DK_BG);
            FillRect(hdc, &rc, g_hbrDkBg);
            return 1;
        }
        return DefWindowProc(hwnd, msg, wParam, lParam);

    case WM_NCPAINT: {
        LRESULT res = DefWindowProc(hwnd, msg, wParam, lParam);
        if (g_darkMode) RepaintMenuBar(hwnd);
        return res;
    }

    case WM_NCACTIVATE: {
        LRESULT res = DefWindowProc(hwnd, msg, wParam, lParam);
        if (g_darkMode) RepaintMenuBar(hwnd);
        return res;
    }

    case WM_MEASUREITEM: {
        MEASUREITEMSTRUCT *mis = (MEASUREITEMSTRUCT *)lParam;
        if (mis->CtlType == ODT_MENU) {
            const char *label = (const char *)mis->itemData;
            HFONT hMenuFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
            HDC hdc = GetDC(hwnd);
            HFONT hOld = (HFONT)SelectObject(hdc, hMenuFont);
            SIZE sz; GetTextExtentPoint32(hdc, label, (int)strlen(label), &sz);
            SelectObject(hdc, hOld); ReleaseDC(hwnd, hdc);
            mis->itemWidth  = sz.cx + 20;
            mis->itemHeight = GetSystemMetrics(SM_CYMENU); /* match system bar height exactly */
            return TRUE;
        }
        return DefWindowProc(hwnd, msg, wParam, lParam);
    }

    case WM_DRAWITEM: {
        LPDRAWITEMSTRUCT dis = (LPDRAWITEMSTRUCT)lParam;
        /* owner-drawn menu */
        if (dis->CtlType == ODT_MENU) {
            const char *label = (const char *)dis->itemData;
            BOOL hot = (dis->itemState & (ODS_SELECTED | ODS_HOTLIGHT)) != 0;
            HBRUSH hbr = CreateSolidBrush(hot ? DK_MENU_HOT : DK_MENU_BG);
            FillRect(dis->hDC, &dis->rcItem, hbr); DeleteObject(hbr);
            SetBkMode(dis->hDC, TRANSPARENT);
            SetTextColor(dis->hDC, DK_TEXT);
            /* Select the same font used in WM_NCPAINT to prevent a size/weight
               jump between the painted state and the hover (DrawItem) state. */
            HFONT hOld = (HFONT)SelectObject(dis->hDC, (HFONT)GetStockObject(DEFAULT_GUI_FONT));
            DrawText(dis->hDC, label, -1, &dis->rcItem, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            SelectObject(dis->hDC, hOld);
            return TRUE;
        }
        int id = (int)dis->CtlID;
        if (id == ID_ADD_BTN) { DrawButton(dis, -1); return TRUE; }
        if (id >= ID_BUTTON_BASE && id < ID_BUTTON_BASE + g_count) {
            DrawButton(dis, id - ID_BUTTON_BASE); return TRUE;
        }
        return DefWindowProc(hwnd, msg, wParam, lParam);
    }

    case WM_COMMAND: {
        int id = LOWORD(wParam);

        if (id == IDM_MOVE_UP && g_ctxIndex > 0) {
            ButtonConfig tmp = g_buttons[g_ctxIndex];
            g_buttons[g_ctxIndex] = g_buttons[g_ctxIndex - 1];
            g_buttons[g_ctxIndex - 1] = tmp;
            HICON hi = g_icons[g_ctxIndex];
            g_icons[g_ctxIndex] = g_icons[g_ctxIndex - 1];
            g_icons[g_ctxIndex - 1] = hi;
            SaveAll(); RefreshMainWindow(); g_ctxIndex = -1;

        } else if (id == IDM_MOVE_DOWN && g_ctxIndex >= 0 && g_ctxIndex < g_count - 1) {
            ButtonConfig tmp = g_buttons[g_ctxIndex];
            g_buttons[g_ctxIndex] = g_buttons[g_ctxIndex + 1];
            g_buttons[g_ctxIndex + 1] = tmp;
            HICON hi = g_icons[g_ctxIndex];
            g_icons[g_ctxIndex] = g_icons[g_ctxIndex + 1];
            g_icons[g_ctxIndex + 1] = hi;
            SaveAll(); RefreshMainWindow(); g_ctxIndex = -1;

        } else if (id == IDM_EDIT_BTN && g_ctxIndex >= 0) {
            if (g_hwndDlg) { SetForegroundWindow(g_hwndDlg); break; }
            g_editIndex = g_ctxIndex; g_ctxIndex = -1;
            g_hwndDlg = CreateWindowEx(WS_EX_DLGMODALFRAME, "AddDlgClass", "Edit Button",
                WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
                CW_USEDEFAULT, CW_USEDEFAULT, 400, 390,
                hwnd, NULL, g_hInst, NULL);
            SetTitleBarDark(g_hwndDlg, g_darkMode);
            ShowWindow(g_hwndDlg, SW_SHOW); UpdateWindow(g_hwndDlg);

        } else if (id == IDM_DELETE_BTN && g_ctxIndex >= 0) {
            char confirm[300];
            snprintf(confirm, sizeof(confirm), "Delete \"%s\"?", g_buttons[g_ctxIndex].name);
            if (MessageBox(hwnd, confirm, "Confirm Delete", MB_YESNO | MB_ICONQUESTION) == IDYES) {
                if (g_icons[g_ctxIndex]) { DestroyIcon(g_icons[g_ctxIndex]); g_icons[g_ctxIndex] = NULL; }
                for (int i = g_ctxIndex; i < g_count - 1; i++) {
                    g_buttons[i] = g_buttons[i + 1];
                    g_icons[i]   = g_icons[i + 1];
                }
                g_icons[g_count - 1] = NULL;
                g_count--;
                SaveAll(); RefreshMainWindow();
            }
            g_ctxIndex = -1;

        } else if (id == ID_HELP_INSTRUCTIONS) {
            ShowInfoDialog(hwnd, "Instructions",
                "HOW IT WORKS\r\n"
                "Simple Launcher shows a configurable list of buttons. "
                "Click a button to launch the program or file assigned to it.\r\n"
                "\r\n"
                "ADDING A BUTTON\r\n"
                "Click '+ Add Button'. Fill in:\r\n"
                "  - Display name: label shown on the button\r\n"
                "  - Path to launch: full path to a program or file\r\n"
                "  - Arguments / options: optional command-line arguments\r\n"
                "  - Run as administrator: triggers a UAC elevation prompt\r\n"
                "  - Show icon from program: shows the .exe icon on the button\r\n"
                "  - Separator: inserts a thin divider line instead of a button\r\n"
                "\r\n"
                "EDITING / DELETING / REORDERING\r\n"
                "Right-click any button to open a context menu with options to "
                "Edit, Delete, Move Up, or Move Down.\r\n"
                "\r\n"
                "ADMIN BORDER COLOR\r\n"
                "Buttons set to run as administrator display a colored border "
                "as a visual reminder that UAC will be triggered. The color "
                "can be customised in Settings.\r\n"
                "\r\n"
                "SETTINGS\r\n"
                "Click 'Settings' in the menu bar to configure:\r\n"
                "  - Dark mode\r\n"
                "  - Admin border color (color picker)\r\n"
                "  - Font size\r\n"
                "  - Window width\r\n"
                "  - Always on top\r\n"
                "  - Minimize to system tray\r\n"
                "\r\n"
                "SYSTEM TRAY\r\n"
                "When 'Minimize to system tray' is on, closing the window hides "
                "it. Double-click the tray icon to restore, or right-click for "
                "Restore / Exit.\r\n"
                "\r\n"
                "CONFIGURATION FILE\r\n"
                "All settings are saved automatically to launcher.ini in the "
                "same folder as launcher.exe. You can edit it manually in Notepad.");

        } else if (id == ID_SETTINGS) {
            if (g_hwndDlg) { SetForegroundWindow(g_hwndDlg); break; }
            g_hwndDlg = CreateWindowEx(WS_EX_DLGMODALFRAME, "SettingsDlgClass", "Settings",
                WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
                CW_USEDEFAULT, CW_USEDEFAULT, 295, 385,
                hwnd, NULL, g_hInst, NULL);
            SetTitleBarDark(g_hwndDlg, g_darkMode);
            ShowWindow(g_hwndDlg, SW_SHOW); UpdateWindow(g_hwndDlg);

        } else if (id == ID_HELP_ABOUT) {
            ShowInfoDialog(hwnd, "About Simple Launcher",
                "Simple Launcher\r\n"
                "Version 1.9\r\n"
                "\r\n"
                "Author:   UberGuidoZ\r\n"
                "Contact:  https://github.com/UberGuidoZ");

        } else if (id == ID_ADD_BTN) {
            if (g_hwndDlg) { SetForegroundWindow(g_hwndDlg); break; }
            g_editIndex = -1;
            g_hwndDlg = CreateWindowEx(WS_EX_DLGMODALFRAME, "AddDlgClass", "Add Button",
                WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
                CW_USEDEFAULT, CW_USEDEFAULT, 400, 390,
                hwnd, NULL, g_hInst, NULL);
            SetTitleBarDark(g_hwndDlg, g_darkMode);
            ShowWindow(g_hwndDlg, SW_SHOW); UpdateWindow(g_hwndDlg);

        } else if (id >= ID_BUTTON_BASE && id < ID_BUTTON_BASE + g_count) {
            int idx = id - ID_BUTTON_BASE;
            if (!g_buttons[idx].isSeparator) {
                HINSTANCE hRet = ShellExecute(NULL,
                                     g_buttons[idx].admin ? "runas" : "open",
                                     g_buttons[idx].path,
                                     g_buttons[idx].args[0] ? g_buttons[idx].args : NULL,
                                     NULL, SW_SHOW);
                if ((INT_PTR)hRet <= 32) {
                    char errmsg[512];
                    snprintf(errmsg, sizeof(errmsg),
                             "Failed to launch:\n%s\n\nError code: %d",
                             g_buttons[idx].path, (int)(INT_PTR)hRet);
                    MessageBox(hwnd, errmsg, "Launch Error", MB_OK | MB_ICONWARNING);
                }
            }
        }
        return 0;
    }

    case WM_DESTROY:
        RemoveTrayIcon();
        FreeIcons();
        SaveAll();
        if (g_hbrDkBg) { DeleteObject(g_hbrDkBg); g_hbrDkBg = NULL; }
        if (g_hFont)   { DeleteObject(g_hFont);   g_hFont   = NULL; }
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

/* ── Entry point ─────────────────────────────────────────────────────── */
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrev, LPSTR lpCmd, int nShow)
{
    g_hInst = hInstance;
    GetIniPath();
    LoadSettings();
    LoadButtons();
    RecreateFont();
    LoadButtonIcons();
    if (g_darkMode) g_hbrDkBg = CreateSolidBrush(DK_BG);

    /* Register dialog window classes — reuse one struct, vary only proc + name */
    WNDCLASS wcd = {0};
    wcd.hInstance    = hInstance;
    wcd.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wcd.hCursor      = LoadCursor(NULL, IDC_ARROW);
    wcd.lpfnWndProc  = InfoDlgProc;     wcd.lpszClassName = "InfoDlgClass";     RegisterClass(&wcd);
    wcd.lpfnWndProc  = SettingsDlgProc; wcd.lpszClassName = "SettingsDlgClass"; RegisterClass(&wcd);
    wcd.lpfnWndProc  = AddDlgProc;      wcd.lpszClassName = "AddDlgClass";      RegisterClass(&wcd);

    WNDCLASSEX wc = {0};
    wc.cbSize        = sizeof(WNDCLASSEX);
    wc.lpfnWndProc   = MainProc;
    wc.hInstance     = hInstance;
    wc.lpszClassName = "LauncherMain";
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    wc.hIcon         = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_APPICON));
    wc.hIconSm       = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_APPICON));
    RegisterClassEx(&wc);

    int startX = (g_winX != -1 && g_winY != -1) ? g_winX : CW_USEDEFAULT;
    int startY = (g_winX != -1 && g_winY != -1) ? g_winY : CW_USEDEFAULT;

    g_hwndMain = CreateWindow("LauncherMain", g_winTitle,
                              WS_OVERLAPPEDWINDOW,
                              startX, startY, g_winWidth + 22, 120,
                              NULL, NULL, hInstance, NULL);

    RebuildMenu();
    RefreshMainWindow();
    SetTitleBarDark(g_hwndMain, g_darkMode);
    ApplyOpacity();
    if (g_alwaysOnTop)
        SetWindowPos(g_hwndMain, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);

    ShowWindow(g_hwndMain, nShow);
    UpdateWindow(g_hwndMain);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        if (g_hwndDlg && IsDialogMessage(g_hwndDlg, &msg)) continue;
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return (int)msg.wParam;
}
