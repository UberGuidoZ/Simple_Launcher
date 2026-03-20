/*
 * simple_launcher.c - Simple Launcher v2.14
 *
 * Features:
 *   - INI-configured buttons with optional icons, separators, admin elevation
 *   - Dark mode with custom admin border color (via color picker)
 *   - Configurable font size and window width
 *   - Always on top, minimize to system tray
 *   - Right-click buttons to Edit / Duplicate / Delete / Move Up / Move Down
 *   - Separator (divider) lines between buttons
 *   - Icon extracted from target .exe (optional per button)
 *   - Remembers last window position
 *   - Tooltips showing path, args, and working directory on hover
 *   - Working directory field per button
 *   - Open INI in Notepad from menu bar
 *   - Categories - collapsible group headers to organise buttons
 *   - Search / filter bar - type to instantly filter buttons by name
 *   - Compact mode - icon-only grid palette for a tiny always-on-top layout
 *   - Open file location from right-click context menu
 *   - Profiles - switchable INI sets from tray or Profiles menu
 *   - Environment variables — %VAR% expanded in path, args, and working dir
 *   - Launch mode per button - Normal, Minimized, or Hidden
 *   - Version 2.14
 *
 * Compile:
 *
 *   cl simple_launcher.c simple_launcher.res /link user32.lib shell32.lib comdlg32.lib gdi32.lib dwmapi.lib ole32.lib comctl32.lib /subsystem:windows /out:simple_launcher.exe
 */

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <commdlg.h>
#include <commctrl.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <shlobj.h>

/* DWMWA_USE_IMMERSIVE_DARK_MODE was given value 20 in Windows 20H1+.
   Pre-20H1 preview builds used value 19. Both are tried for compatibility. */
#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
#define DWMWA_USE_IMMERSIVE_DARK_MODE_PRE 19

/* EM_SETBKGNDCOLOR is a RichEdit-only message (value 0x0443). It is sent
   speculatively to plain EDIT controls for dark-mode background tinting;
   non-RichEdit windows silently ignore it. */
#ifndef EM_SETBKGNDCOLOR
#define EM_SETBKGNDCOLOR 0x0443
#endif

/* ── Forward declarations ────────────────────────────────────────────── */
static void RefreshMainWindow(void);
static void ApplyFilter(void);
static void RebuildMenu(void);
static void ApplyDarkBackground(void);
static void SaveAll(void);
static void LoadSettings(void);
static void LoadButtons(void);
static void LoadButtonIcons(void);
static void LoadSingleButtonIcon(int i);
static void FreeIcons(void);
static void EnsureDarkGDI(void);
static void FreeDarkGDI(void);
static void RecreateFont(void);
static void SetTitleBarDark(HWND hwnd, int dark);
static void ScanProfiles(void);
static void SwitchProfile(int idx);

/* ── IDs ─────────────────────────────────────────────────────────────── */
#define ID_ADD_BTN            1
#define ID_BUTTON_BASE        100
#define MAX_BUTTONS           64
#define IDI_APPICON           200
#define COMPACT_BTN_SZ        28
#define COMPACT_BTN_GAP       4

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
#define IDC_WORKDIR_EDIT      41
#define IDC_WORKDIR_BROWSE    42
#define IDC_COMPACT_CHECK     45
#define IDC_CAT_CHECK         46
#define IDC_LAUNCH_COMBO      47   /* Normal / Minimized / Hidden */
#define IDC_SEARCH_EDIT       60

/* Prompt dialog (new profile name) */
#define IDC_PROMPT_EDIT       50
#define IDC_PROMPT_OK         51
#define IDC_PROMPT_CANCEL     52

/* Menu IDs */
#define ID_HELP_INSTRUCTIONS  20
#define ID_HELP_ABOUT         21
#define ID_SETTINGS           22
#define ID_OPEN_INI           23
#define ID_PROFILES_MENU      24

/* Right-click context menu */
#define IDM_MOVE_UP           300
#define IDM_MOVE_DOWN         301
#define IDM_EDIT_BTN          302
#define IDM_DELETE_BTN        303
#define IDM_DUPLICATE_BTN     304
#define IDM_OPEN_LOCATION     305

/* System tray */
#define WM_TRAYICON           (WM_APP + 1)
#define ID_TRAY_ICON          1
#define IDM_TRAY_RESTORE      400
#define IDM_TRAY_EXIT         401

/* Profiles */
#define IDM_PROFILE_BASE      600   /* 600..615 → switch to profile N */
#define IDM_PROFILE_NEW       616
#define IDM_PROFILE_DELETE    617
#define MAX_PROFILES          16

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
#define DK_SEARCH   RGB( 40,  40,  40)
#define DK_CAT_BG   RGB( 50,  75, 110)   /* category header — dark mode */
#define LT_CAT_BG   RGB(200, 220, 245)   /* category header — light mode */
#define DK_CAT_TEXT RGB(200, 225, 255)
#define LT_CAT_TEXT RGB( 20,  50, 100)

static const char *g_menuLabels[] = { "Instructions", "Settings", "Profiles", "Open INI", "About" };
static const UINT  g_menuIDs[]    = { ID_HELP_INSTRUCTIONS, ID_SETTINGS, ID_PROFILES_MENU, ID_OPEN_INI, ID_HELP_ABOUT };

/* ── Data ────────────────────────────────────────────────────────────── */
typedef struct {
    char name[256];
    char path[MAX_PATH];
    char args[512];
    char workDir[MAX_PATH]; /* working directory, or empty = use exe's directory */
    char iconPath[MAX_PATH];  /* custom icon file, or empty = use target .exe */
    int  admin;
    int  isSeparator;
    int  isCategory;    /* collapsible group header */
    int  showIcon;
    int  launchMode;    /* 0=Normal, 1=Minimized, 2=Hidden */
} ButtonConfig;

static ButtonConfig g_buttons[MAX_BUTTONS];
static HICON        g_icons[MAX_BUTTONS];
static int          g_count        = 0;
static int          g_collapsed[MAX_BUTTONS]; /* 1 = category is collapsed */

/* Settings */
static int          g_darkMode     = 0;
static int          g_alwaysOnTop  = 0;
static int          g_minToTray    = 0;
static int          g_fontSize     = 9;
static int          g_winWidth     = 300;
static int          g_compactMode  = 0;
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
static HBRUSH       g_hbrSearchDk  = NULL;
static HBRUSH       g_hbrDkBtn     = NULL;    /* cached DK_BTN fill brush */
static HBRUSH       g_hbrDkBtnPre  = NULL;    /* cached DK_BTN_PRE fill brush */
static HBRUSH       g_hbrDkMenuBg  = NULL;    /* cached DK_MENU_BG brush for RepaintMenuBar */
static HBRUSH       g_hbrDkMenuHot = NULL;    /* cached DK_MENU_HOT brush for WM_DRAWITEM hover */
static HBRUSH       g_hbrDkCatBg   = NULL;    /* cached DK_CAT_BG category header background */
static HBRUSH       g_hbrLtCatBg   = NULL;    /* cached LT_CAT_BG category header background */
static HBRUSH       g_hbrLtCatPre  = NULL;    /* cached pressed light-mode category background */
static HPEN         g_hpenDkBorder = NULL;    /* cached DK_BORDER outline pen */
static HPEN         g_hpenDkSep    = NULL;    /* cached DK_SEP separator pen */
static HPEN         g_hpenLtSep    = NULL;    /* cached LT_SEP separator pen */
static HPEN         g_hpenAdminBorder = NULL; /* cached admin border pen */
static COLORREF     g_hpenAdminColor  = (COLORREF)-1; /* tracks color used for the cached pen */
static HFONT        g_hFont        = NULL;
static HFONT        g_hFontBold    = NULL;   /* bold font for category headers */
static int          g_trayAdded    = 0;
static int          g_editIndex    = -1;
static int          g_ctxIndex     = -1;
static COLORREF     g_settingColor;
static COLORREF     g_customColors[16];
static HWND         g_hwndSearch   = NULL;
static char         g_filterText[256] = "";

/* Profiles */
static char         g_basePath[MAX_PATH];
static char         g_profileNames[MAX_PROFILES][64];
static char         g_profilePaths[MAX_PROFILES][MAX_PATH];
static int          g_profileCount  = 0;
static int          g_activeProfile = 0;

/* Prompt dialog (new profile name input) */
static char         g_promptResult[64];
static int          g_promptDone      = 0;
static int          g_promptCancelled = 0;

static const char  *g_infoDlgTitle   = NULL;
static const char  *g_infoDlgContent = NULL;

/* Tooltips */
static HWND  g_hwndTooltip = NULL;
static char  g_tooltipText[MAX_BUTTONS][384];

/* Precomputed lowercase button names for fast filter matching.
   Rebuilt once at the top of RefreshMainWindow instead of re-lowercasing
   every name on every keystroke inside ButtonMatchesFilter. */
static char  g_buttonNamesLower[MAX_BUTTONS][256];

/* Precomputed lowercase version of g_filterText.
   Updated once per EN_CHANGE so ButtonMatchesFilter never lowercases the
   same filter string once per button per keystroke. */
static char  g_filterTextLower[256] = "";

/* ── DWM dark title bar ──────────────────────────────────────────────── */
static void SetTitleBarDark(HWND hwnd, int dark)
{
    BOOL val = dark ? TRUE : FALSE;
    if (FAILED(DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &val, sizeof(val))))
        DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE_PRE, &val, sizeof(val));
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
    if (g_hFont)     { DeleteObject(g_hFont);     g_hFont     = NULL; }
    if (g_hFontBold) { DeleteObject(g_hFontBold); g_hFontBold = NULL; }
    HDC hdc = GetDC(NULL);
    int h = -MulDiv(g_fontSize, GetDeviceCaps(hdc, LOGPIXELSY), 72);
    ReleaseDC(NULL, hdc);
    g_hFont = CreateFont(h, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                         DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                         CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Segoe UI");
    g_hFontBold = CreateFont(h, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
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
static void GetBasePath(void)
{
    GetModuleFileName(NULL, g_basePath, MAX_PATH);
    char *dot = strrchr(g_basePath, '.');
    if (dot) {
        if ((dot - g_basePath) + 5 < MAX_PATH)
            strcpy(dot, ".ini");
    } else {
        size_t len = strlen(g_basePath);
        if (len + 4 < MAX_PATH) strcat(g_basePath, ".ini");
    }
    strcpy(g_iniPath, g_basePath);
}

/* ── Profiles ────────────────────────────────────────────────────────── */
static void ScanProfiles(void)
{
    g_profileCount = 0;
    strcpy(g_profileNames[0], "Default");
    strcpy(g_profilePaths[0], g_basePath);
    g_profileCount = 1;

    char dir[MAX_PATH]; strcpy(dir, g_basePath);
    char *ls = strrchr(dir, '\\'); if (!ls) ls = strrchr(dir, '/');
    if (ls) *(ls + 1) = '\0'; else dir[0] = '\0';

    char pat[MAX_PATH];
    snprintf(pat, MAX_PATH, "%slauncher_*.ini", dir);
    WIN32_FIND_DATA fd;
    HANDLE h = FindFirstFile(pat, &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (g_profileCount >= MAX_PROFILES) break;
            char nm[64];
            strncpy(nm, fd.cFileName + 9, 63); nm[63] = '\0'; /* strip "launcher_" (9 chars) */
            char *dot2 = strrchr(nm, '.'); if (dot2) *dot2 = '\0';
            if (!nm[0]) continue;
            strcpy(g_profileNames[g_profileCount], nm);
            snprintf(g_profilePaths[g_profileCount], MAX_PATH, "%s%s", dir, fd.cFileName);
            g_profileCount++;
        } while (FindNextFile(h, &fd));
        FindClose(h);
    }

    char active[64];
    GetPrivateProfileString("Meta", "ActiveProfile", "Default",
                            active, sizeof(active), g_basePath);
    g_activeProfile = 0;
    for (int i = 0; i < g_profileCount; i++)
        if (_stricmp(g_profileNames[i], active) == 0) { g_activeProfile = i; break; }
    strcpy(g_iniPath, g_profilePaths[g_activeProfile]);
}

static void SwitchProfile(int idx)
{
    if (idx < 0 || idx >= g_profileCount || idx == g_activeProfile) return;
    SaveAll();
    g_activeProfile = idx;
    strcpy(g_iniPath, g_profilePaths[idx]);
    WritePrivateProfileString("Meta", "ActiveProfile",
                              g_profileNames[idx], g_basePath);
    FreeIcons();
    g_count = 0;
    memset(g_buttons,   0, sizeof(g_buttons));
    memset(g_collapsed, 0, sizeof(g_collapsed));
    g_filterText[0]      = '\0';
    g_filterTextLower[0] = '\0';
    if (g_hwndSearch) SetWindowText(g_hwndSearch, "");
    LoadSettings();
    LoadButtons();
    LoadButtonIcons();  /* must run before RefreshMainWindow draws icons */
    RecreateFont();
    ApplyDarkBackground();
    SetTitleBarDark(g_hwndMain, g_darkMode);
    SetWindowText(g_hwndMain, g_winTitle);
    SetWindowPos(g_hwndMain,
                 g_alwaysOnTop ? HWND_TOPMOST : HWND_NOTOPMOST,
                 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
    ApplyOpacity();
    RebuildMenu();
    RefreshMainWindow();
}

static void ShowProfilesMenu(HWND hwnd, int x, int y)
{
    HMENU hM = CreatePopupMenu();
    for (int i = 0; i < g_profileCount; i++) {
        UINT flags = MF_STRING | (i == g_activeProfile ? MF_CHECKED : 0);
        AppendMenu(hM, flags, IDM_PROFILE_BASE + i, g_profileNames[i]);
    }
    AppendMenu(hM, MF_SEPARATOR, 0, NULL);
    AppendMenu(hM, MF_STRING, IDM_PROFILE_NEW, "New Profile...");
    AppendMenu(hM, MF_STRING | (g_activeProfile == 0 ? MF_GRAYED : 0),
               IDM_PROFILE_DELETE, "Delete Current Profile");
    SetForegroundWindow(hwnd);
    TrackPopupMenu(hM, TPM_LEFTBUTTON, x, y, 0, hwnd, NULL);
    DestroyMenu(hM);
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
    g_compactMode = GetPrivateProfileInt("Settings", "CompactMode",      0,              g_iniPath);
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
    if (g_count < 0)           g_count = 0;
    if (g_count > MAX_BUTTONS) g_count = MAX_BUTTONS;
    for (int i = 0; i < g_count; i++) {
        char sec[32];
        snprintf(sec, sizeof(sec), "Button%d", i + 1);
        GetPrivateProfileString(sec, "Name", "",  g_buttons[i].name, 256,      g_iniPath);
        GetPrivateProfileString(sec, "Path", "",  g_buttons[i].path, MAX_PATH, g_iniPath);
        GetPrivateProfileString(sec, "Args",     "", g_buttons[i].args,     512,      g_iniPath);
        GetPrivateProfileString(sec, "WorkDir",  "", g_buttons[i].workDir,  MAX_PATH, g_iniPath);
        GetPrivateProfileString(sec, "IconPath", "", g_buttons[i].iconPath, MAX_PATH, g_iniPath);
        g_buttons[i].admin       = GetPrivateProfileInt(sec, "Admin",      0, g_iniPath);
        g_buttons[i].isSeparator = GetPrivateProfileInt(sec, "Separator",  0, g_iniPath);
        g_buttons[i].isCategory  = GetPrivateProfileInt(sec, "IsCategory", 0, g_iniPath);
        g_buttons[i].showIcon    = GetPrivateProfileInt(sec, "ShowIcon",   0, g_iniPath);
        g_buttons[i].launchMode  = GetPrivateProfileInt(sec, "LaunchMode", 0, g_iniPath);
        if (g_buttons[i].launchMode < 0 || g_buttons[i].launchMode > 2)
            g_buttons[i].launchMode = 0;   /* clamp invalid values to Normal */
        g_collapsed[i] = 0;
    }
}

/* Writes key=value to f, replacing any CR, LF, or '[' in value with a space.
   CR/LF prevent a user-supplied string from injecting extra INI lines.
   '[' prevents a value from being mis-parsed as a section header by some
   INI readers if it appears at the start of a line after line continuation. */
static void WriteEscaped(FILE *f, const char *key, const char *val)
{
    fprintf(f, "%s=", key);
    for (; *val; val++)
        fputc((*val == '\r' || *val == '\n' || *val == '[') ? ' ' : *val, f);
    fputs("\r\n", f);
}

/* Writes all settings and button data to an already-open file handle.
   Shared by both branches of SaveAll so there is exactly one copy of the
   write logic; any field addition or rename only needs to be made here. */
static void WriteINIBody(FILE *f)
{
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
    fprintf(f, "CompactMode=%d\r\n",      g_compactMode);
    WriteEscaped(f, "WindowTitle",        g_winTitle);
    fprintf(f, "\r\n");
    fprintf(f, "[Buttons]\r\n");
    fprintf(f, "Count=%d\r\n", g_count);
    for (int i = 0; i < g_count; i++) {
        fprintf(f, "\r\n");
        fprintf(f, "[Button%d]\r\n",   i + 1);
        WriteEscaped(f, "Name",        g_buttons[i].name);
        WriteEscaped(f, "Path",        g_buttons[i].path);
        WriteEscaped(f, "Args",        g_buttons[i].args);
        WriteEscaped(f, "WorkDir",     g_buttons[i].workDir);
        WriteEscaped(f, "IconPath",    g_buttons[i].iconPath);
        fprintf(f, "Admin=%d\r\n",      g_buttons[i].admin);
        fprintf(f, "Separator=%d\r\n",  g_buttons[i].isSeparator);
        fprintf(f, "IsCategory=%d\r\n", g_buttons[i].isCategory);
        fprintf(f, "ShowIcon=%d\r\n",   g_buttons[i].showIcon);
        fprintf(f, "LaunchMode=%d\r\n", g_buttons[i].launchMode);
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

    /* Write to a temporary file first, then atomically rename it over the
       real INI.  This ensures the INI is never left in a truncated or
       partially-written state if the process is killed mid-save.
       If the path is too long to append ".tmp", fall back to a direct
       overwrite so the save still succeeds. */
    if (strlen(g_iniPath) + 5 >= MAX_PATH) {
        /* Path too long for a .tmp suffix -- write directly. */
        FILE *f = fopen(g_iniPath, "w");
        if (!f) return;
        WriteINIBody(f);
        fclose(f);
        return;
    }

    char tmpPath[MAX_PATH];
    snprintf(tmpPath, MAX_PATH, "%s.tmp", g_iniPath);

    FILE *f = fopen(tmpPath, "w");
    if (!f) return;
    WriteINIBody(f);
    fclose(f);

    /* Atomic replace: the old INI is only overwritten after the new data
       has been fully flushed and closed. */
    MoveFileEx(tmpPath, g_iniPath,
               MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
}

/* ── Environment variable expansion ─────────────────────────────────── */
/* Expands %VAR% tokens in src into dst (MAX_PATH). Falls back to src   */
/* unchanged if ExpandEnvironmentStrings fails or dst would overflow.   */
static void ExpandEnvVars(const char *src, char *dst, DWORD dstSize)
{
    if (!src || !src[0]) { if (dst && dstSize) dst[0] = '\0'; return; }
    DWORD result = ExpandEnvironmentStrings(src, dst, dstSize);
    if (result == 0 || result > dstSize) {
        /* Failed or overflow — copy as-is, safely truncated */
        strncpy(dst, src, dstSize - 1);
        dst[dstSize - 1] = '\0';
    }
}
static void FreeIcons(void)
{
    for (int i = 0; i < MAX_BUTTONS; i++) {
        if (g_icons[i]) { DestroyIcon(g_icons[i]); g_icons[i] = NULL; }
    }
}

/* Creates cached GDI objects for dark-mode drawing if they do not yet exist.
   These constant-color objects are reused across all WM_DRAWITEM calls instead
   of being allocated and freed on every paint. */
static void EnsureDarkGDI(void)
{
    if (!g_hbrDkBtn)     g_hbrDkBtn     = CreateSolidBrush(DK_BTN);
    if (!g_hbrDkBtnPre)  g_hbrDkBtnPre  = CreateSolidBrush(DK_BTN_PRE);
    if (!g_hbrDkMenuBg)  g_hbrDkMenuBg  = CreateSolidBrush(DK_MENU_BG);
    if (!g_hbrDkMenuHot) g_hbrDkMenuHot = CreateSolidBrush(DK_MENU_HOT);
    if (!g_hbrDkCatBg)   g_hbrDkCatBg   = CreateSolidBrush(DK_CAT_BG);
    if (!g_hbrLtCatBg)   g_hbrLtCatBg   = CreateSolidBrush(LT_CAT_BG);
    if (!g_hbrLtCatPre)  g_hbrLtCatPre  = CreateSolidBrush(RGB(180, 205, 235));
    if (!g_hpenDkBorder) g_hpenDkBorder = CreatePen(PS_SOLID, 1, DK_BORDER);
    if (!g_hpenDkSep)    g_hpenDkSep    = CreatePen(PS_SOLID, 1, DK_SEP);
    if (!g_hpenLtSep)    g_hpenLtSep    = CreatePen(PS_SOLID, 1, LT_SEP);
}

/* Releases all cached dark-mode GDI objects. Called when toggling dark mode
   off or on application shutdown. */
static void FreeDarkGDI(void)
{
    if (g_hbrDkBtn)        { DeleteObject(g_hbrDkBtn);        g_hbrDkBtn        = NULL; }
    if (g_hbrDkBtnPre)     { DeleteObject(g_hbrDkBtnPre);     g_hbrDkBtnPre     = NULL; }
    if (g_hbrDkMenuBg)     { DeleteObject(g_hbrDkMenuBg);     g_hbrDkMenuBg     = NULL; }
    if (g_hbrDkMenuHot)    { DeleteObject(g_hbrDkMenuHot);    g_hbrDkMenuHot    = NULL; }
    if (g_hbrDkCatBg)      { DeleteObject(g_hbrDkCatBg);      g_hbrDkCatBg      = NULL; }
    if (g_hbrLtCatBg)      { DeleteObject(g_hbrLtCatBg);      g_hbrLtCatBg      = NULL; }
    if (g_hbrLtCatPre)     { DeleteObject(g_hbrLtCatPre);     g_hbrLtCatPre     = NULL; }
    if (g_hpenDkBorder)    { DeleteObject(g_hpenDkBorder);    g_hpenDkBorder    = NULL; }
    if (g_hpenDkSep)       { DeleteObject(g_hpenDkSep);       g_hpenDkSep       = NULL; }
    if (g_hpenLtSep)       { DeleteObject(g_hpenLtSep);       g_hpenLtSep       = NULL; }
    if (g_hpenAdminBorder) { DeleteObject(g_hpenAdminBorder); g_hpenAdminBorder = NULL;
                             g_hpenAdminColor = (COLORREF)-1; }
}

/* Shared icon-loading helper used by both LoadButtonIcons and
   LoadSingleButtonIcon.  Frees any existing icon in slot i before loading. */
static void LoadIconForSlot(int i)
{
    if (g_icons[i]) { DestroyIcon(g_icons[i]); g_icons[i] = NULL; }
    if (!g_buttons[i].showIcon || g_buttons[i].isSeparator) return;
    const char *src = g_buttons[i].iconPath[0] ? g_buttons[i].iconPath
                                                : g_buttons[i].path;
    if (!src[0]) return;
    /* Try loading as a standalone .ico / .png first */
    HICON hIco = (HICON)LoadImage(NULL, src, IMAGE_ICON, 16, 16, LR_LOADFROMFILE);
    if (hIco) { g_icons[i] = hIco; return; }
    /* Fall back to extracting from .exe */
    HICON hLg = NULL, hSm = NULL;
    ExtractIconEx(src, 0, &hLg, &hSm, 1);
    if (hSm)      { g_icons[i] = hSm; if (hLg) DestroyIcon(hLg); }
    else if (hLg) { g_icons[i] = hLg; }
}

/* Loads or reloads the icon for a single button slot.
   Used after Add, Edit, or Duplicate so that only the affected slot
   needs updating rather than reloading all icons. */
static void LoadSingleButtonIcon(int i)
{
    if (i < 0 || i >= MAX_BUTTONS) return;
    LoadIconForSlot(i);
}

static void LoadButtonIcons(void)
{
    FreeIcons();
    for (int i = 0; i < g_count; i++)
        LoadIconForSlot(i);
}

/* ── Owner-draw ──────────────────────────────────────────────────────── */
static void DrawButton(LPDRAWITEMSTRUCT dis, int idx)
{
    RECT rc = dis->rcItem;
    BOOL pressed = (dis->itemState & ODS_SELECTED);

    /* ── Compact tile (non-Add) ── */
    if (g_compactMode && idx >= 0) {
        if (g_darkMode) {
            EnsureDarkGDI();
            FillRect(dis->hDC, &rc, pressed ? g_hbrDkBtnPre : g_hbrDkBtn);
            HPEN hop = (HPEN)SelectObject(dis->hDC, g_hpenDkBorder);
            HBRUSH hn = (HBRUSH)SelectObject(dis->hDC, GetStockObject(NULL_BRUSH));
            Rectangle(dis->hDC, rc.left, rc.top, rc.right, rc.bottom);
            SelectObject(dis->hDC, hop); SelectObject(dis->hDC, hn);
        } else {
            FillRect(dis->hDC, &rc, (HBRUSH)(COLOR_BTNFACE + 1));
            DrawEdge(dis->hDC, &rc, pressed ? EDGE_SUNKEN : EDGE_RAISED, BF_RECT);
        }
        int sz = 16;
        int ix = rc.left + (rc.right  - rc.left - sz) / 2;
        int iy = rc.top  + (rc.bottom - rc.top  - sz) / 2;
        if (idx < g_count && g_icons[idx]) {
            DrawIconEx(dis->hDC, ix, iy, g_icons[idx], sz, sz, 0, NULL, DI_NORMAL);
        } else if (idx < g_count && g_buttons[idx].name[0]) {
            char letter[2] = { (char)toupper((unsigned char)g_buttons[idx].name[0]), '\0' };
            SetBkMode(dis->hDC, TRANSPARENT);
            SetTextColor(dis->hDC, g_darkMode ? DK_TEXT : GetSysColor(COLOR_BTNTEXT));
            HFONT hof = g_hFont ? (HFONT)SelectObject(dis->hDC, g_hFont) : NULL;
            DrawText(dis->hDC, letter, 1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            if (hof) SelectObject(dis->hDC, hof);
        }
        if (dis->itemState & ODS_FOCUS) DrawFocusRect(dis->hDC, &rc);
        return;
    }

    /* ── Category header ── */
    if (idx >= 0 && idx < g_count && g_buttons[idx].isCategory) {
        COLORREF text = g_darkMode ? DK_CAT_TEXT : LT_CAT_TEXT;
        /* Use cached brushes; no per-paint alloc/free needed. */
        HBRUSH hbr;
        if (pressed)
            hbr = g_darkMode ? g_hbrDkBtnPre : g_hbrLtCatPre;
        else
            hbr = g_darkMode ? g_hbrDkCatBg : g_hbrLtCatBg;
        FillRect(dis->hDC, &rc, hbr);
        const char *arrow = g_collapsed[idx] ? ">" : "v";
        SetBkMode(dis->hDC, TRANSPARENT);
        SetTextColor(dis->hDC, text);
        HFONT hof = g_hFontBold ? (HFONT)SelectObject(dis->hDC, g_hFontBold) : NULL;
        RECT arrowRc = { rc.left + 6, rc.top, rc.left + 20, rc.bottom };
        DrawText(dis->hDC, arrow, -1, &arrowRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        RECT textRc  = { rc.left + 22, rc.top, rc.right - 4, rc.bottom };
        DrawText(dis->hDC, g_buttons[idx].name, -1, &textRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        if (hof) SelectObject(dis->hDC, hof);
        if (dis->itemState & ODS_FOCUS) DrawFocusRect(dis->hDC, &rc);
        return;
    }

    /* ── Separator ── */
    if (idx >= 0 && idx < g_count && g_buttons[idx].isSeparator) {
        /* Reuse the cached dark background brush; light mode uses the stock
           system brush -- neither requires a per-paint alloc/free. */
        HBRUSH hbr = g_darkMode ? g_hbrDkBg : (HBRUSH)(COLOR_BTNFACE + 1);
        FillRect(dis->hDC, &rc, hbr);
        int midY = (rc.top + rc.bottom) / 2;
        EnsureDarkGDI();
        HPEN hOld = (HPEN)SelectObject(dis->hDC,
                        g_darkMode ? g_hpenDkSep : g_hpenLtSep);
        MoveToEx(dis->hDC, rc.left + 6, midY, NULL);
        LineTo(dis->hDC,   rc.right - 6, midY);
        SelectObject(dis->hDC, hOld);
        return;
    }

    /* ── Normal button ── */
    int isAdmin = (idx >= 0 && idx < g_count) ? g_buttons[idx].admin : 0;
    if (g_darkMode) {
        EnsureDarkGDI();
        FillRect(dis->hDC, &rc, pressed ? g_hbrDkBtnPre : g_hbrDkBtn);
        HPEN   hOldP = (HPEN)SelectObject(dis->hDC, g_hpenDkBorder);
        HBRUSH hNull = (HBRUSH)SelectObject(dis->hDC, GetStockObject(NULL_BRUSH));
        Rectangle(dis->hDC, rc.left, rc.top, rc.right, rc.bottom);
        SelectObject(dis->hDC, hOldP); SelectObject(dis->hDC, hNull);
    } else {
        FillRect(dis->hDC, &rc, (HBRUSH)(COLOR_BTNFACE + 1));
        DrawEdge(dis->hDC, &rc, pressed ? EDGE_SUNKEN : EDGE_RAISED, BF_RECT);
    }
    if (isAdmin) {
        /* Cache the admin border pen; recreate only when g_adminColor changes
           because its color is user-configurable. */
        if (!g_hpenAdminBorder || g_hpenAdminColor != g_adminColor) {
            if (g_hpenAdminBorder) DeleteObject(g_hpenAdminBorder);
            g_hpenAdminBorder = CreatePen(PS_SOLID, 2, g_adminColor);
            g_hpenAdminColor  = g_adminColor;
        }
        HPEN   hOldP = (HPEN)SelectObject(dis->hDC, g_hpenAdminBorder);
        HBRUSH hNull = (HBRUSH)SelectObject(dis->hDC, GetStockObject(NULL_BRUSH));
        RECT   inner = { rc.left+3, rc.top+3, rc.right-3, rc.bottom-3 };
        Rectangle(dis->hDC, inner.left, inner.top, inner.right, inner.bottom);
        SelectObject(dis->hDC, hOldP); SelectObject(dis->hDC, hNull);
    }
    int textLeft = rc.left;
    if (idx >= 0 && idx < g_count && g_icons[idx]) {
        int sz = 16, ix = rc.left + 6;
        int iy = rc.top + (rc.bottom - rc.top - sz) / 2;
        DrawIconEx(dis->hDC, ix, iy, g_icons[idx], sz, sz, 0, NULL, DI_NORMAL);
        textLeft = ix + sz + 4;
    }
    SetBkMode(dis->hDC, TRANSPARENT);
    SetTextColor(dis->hDC, g_darkMode ? DK_TEXT : GetSysColor(COLOR_BTNTEXT));
    HFONT hOldFont = g_hFont ? (HFONT)SelectObject(dis->hDC, g_hFont) : NULL;
    RECT textRc = { textLeft, rc.top, rc.right, rc.bottom };
    if (pressed) OffsetRect(&textRc, 1, 1);
    const char *label = (idx == -1) ? "+ Add Button / Separator / Header" :
                        (idx >= 0 && idx < g_count) ? g_buttons[idx].name : "";
    DrawText(dis->hDC, label, -1, &textRc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    if (hOldFont) SelectObject(dis->hDC, hOldFont);
    if (dis->itemState & ODS_FOCUS) DrawFocusRect(dis->hDC, &rc);
}

/* ── Dark background ─────────────────────────────────────────────────── */
static void ApplyDarkBackground(void)
{
    if (g_hbrDkBg)     { DeleteObject(g_hbrDkBg);     g_hbrDkBg     = NULL; }
    if (g_hbrSearchDk) { DeleteObject(g_hbrSearchDk); g_hbrSearchDk = NULL; }
    FreeDarkGDI();
    if (g_darkMode) {
        g_hbrDkBg     = CreateSolidBrush(DK_BG);
        g_hbrSearchDk = CreateSolidBrush(DK_SEARCH);
        EnsureDarkGDI();
    }
    if (g_hwndSearch) InvalidateRect(g_hwndSearch, NULL, TRUE);
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

/* ── Filter helper ───────────────────────────────────────────────────── */

/* Rebuilds the precomputed lowercase name table.  Called once per layout
   pass so ButtonMatchesFilter never re-lowercases on every keystroke. */
static void RebuildFilterCache(void)
{
    for (int i = 0; i < g_count; i++) {
        int j = 0;
        for (const char *p = g_buttons[i].name; *p && j < 255; p++)
            g_buttonNamesLower[i][j++] = (char)tolower((unsigned char)*p);
        g_buttonNamesLower[i][j] = '\0';
    }
}

static int ButtonMatchesFilter(int i)
{
    if (!g_filterText[0]) return 1;
    if (g_buttons[i].isSeparator || g_buttons[i].isCategory) return 0;
    /* Both sides are precomputed lowercase; no per-call allocation needed. */
    return (strstr(g_buttonNamesLower[i], g_filterTextLower) != NULL) ? 1 : 0;
}

/* ── Layout ──────────────────────────────────────────────────────────── */
static void RefreshMainWindow(void)
{
    /* Rebuild the lowercase name cache before any filtering occurs. */
    RebuildFilterCache();

    for (int i = 0; i < MAX_BUTTONS; i++) {
        if (g_hwndBtns[i]) { DestroyWindow(g_hwndBtns[i]); g_hwndBtns[i] = NULL; }
    }
    if (g_hwndTooltip) { DestroyWindow(g_hwndTooltip); g_hwndTooltip = NULL; }

    int btnW = g_winWidth - 20;

    /* Search bar — always visible */
    if (!g_hwndSearch) {
        g_hwndSearch = CreateWindowEx(WS_EX_CLIENTEDGE, "EDIT", "",
            WS_VISIBLE | WS_CHILD | ES_AUTOHSCROLL,
            10, 10, btnW, 22, g_hwndMain, (HMENU)IDC_SEARCH_EDIT, g_hInst, NULL);
        SendMessage(g_hwndSearch, EM_SETCUEBANNER, FALSE, (LPARAM)L"Search\u2026");
        if (g_hFont) SendMessage(g_hwndSearch, WM_SETFONT, (WPARAM)g_hFont, FALSE);
    } else {
        SetWindowPos(g_hwndSearch, HWND_TOP, 10, 10, btnW, 22, SWP_SHOWWINDOW);
        if (g_hFont) SendMessage(g_hwndSearch, WM_SETFONT, (WPARAM)g_hFont, FALSE);
    }

    int y = 10 + 22 + 6;   /* below search bar */

    if (g_compactMode) {
        /* ── Compact grid — icon tiles, no labels, skip separators+categories ── */
        int sz = COMPACT_BTN_SZ, gap = COMPACT_BTN_GAP;
        int cols = (btnW + gap) / (sz + gap);
        if (cols < 1) cols = 1;
        int col = 0;
        for (int i = 0; i < g_count; i++) {
            if (g_buttons[i].isSeparator || g_buttons[i].isCategory) continue;
            int bx = 10 + col * (sz + gap);
            g_hwndBtns[i] = CreateWindow("BUTTON", g_buttons[i].name,
                WS_VISIBLE | WS_CHILD | BS_OWNERDRAW, bx, y, sz, sz,
                g_hwndMain, (HMENU)(UINT_PTR)(ID_BUTTON_BASE + i), g_hInst, NULL);
            if (g_hFont) SendMessage(g_hwndBtns[i], WM_SETFONT, (WPARAM)g_hFont, FALSE);
            col++;
            if (col >= cols) { col = 0; y += sz + gap; }
        }
        if (col > 0) y += sz + gap;

    } else if (g_filterText[0]) {
        /* ── Flat filtered list — skip category structure ── */
        for (int i = 0; i < g_count; i++) {
            if (!ButtonMatchesFilter(i)) continue;
            g_hwndBtns[i] = CreateWindow("BUTTON", g_buttons[i].name,
                WS_VISIBLE | WS_CHILD | BS_OWNERDRAW, 10, y, btnW, 26,
                g_hwndMain, (HMENU)(UINT_PTR)(ID_BUTTON_BASE + i), g_hInst, NULL);
            if (g_hFont) SendMessage(g_hwndBtns[i], WM_SETFONT, (WPARAM)g_hFont, FALSE);
            y += 26 + 5;
        }

    } else {
        /* ── Full list with categories and separators ── */
        int catCollapsed = 0;
        for (int i = 0; i < g_count; i++) {
            if (g_buttons[i].isCategory) {
                catCollapsed = g_collapsed[i];
                int h = 24;
                g_hwndBtns[i] = CreateWindow("BUTTON", g_buttons[i].name,
                    WS_VISIBLE | WS_CHILD | BS_OWNERDRAW, 10, y, btnW, h,
                    g_hwndMain, (HMENU)(UINT_PTR)(ID_BUTTON_BASE + i), g_hInst, NULL);
                if (g_hFontBold) SendMessage(g_hwndBtns[i], WM_SETFONT, (WPARAM)g_hFontBold, FALSE);
                y += h + 3;
            } else if (catCollapsed) {
                continue;
            } else if (g_buttons[i].isSeparator) {
                g_hwndBtns[i] = CreateWindow("BUTTON", "", WS_VISIBLE | WS_CHILD | BS_OWNERDRAW,
                    10, y, btnW, 14, g_hwndMain,
                    (HMENU)(UINT_PTR)(ID_BUTTON_BASE + i), g_hInst, NULL);
                y += 14 + 5;
            } else {
                g_hwndBtns[i] = CreateWindow("BUTTON", g_buttons[i].name,
                    WS_VISIBLE | WS_CHILD | BS_OWNERDRAW, 10, y, btnW, 26,
                    g_hwndMain, (HMENU)(UINT_PTR)(ID_BUTTON_BASE + i), g_hInst, NULL);
                if (g_hFont) SendMessage(g_hwndBtns[i], WM_SETFONT, (WPARAM)g_hFont, FALSE);
                y += 26 + 5;
            }
        }
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

    /* ── Tooltips ── */
    g_hwndTooltip = CreateWindowEx(WS_EX_TOPMOST, TOOLTIPS_CLASS, NULL,
        WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP,
        CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
        g_hwndMain, NULL, g_hInst, NULL);
    if (g_hwndTooltip) {
        SendMessage(g_hwndTooltip, TTM_SETMAXTIPWIDTH, 0, 400);
        SendMessage(g_hwndTooltip, TTM_SETDELAYTIME, TTDT_INITIAL, 500);
        for (int i = 0; i < g_count; i++) {
            if (!g_hwndBtns[i] || g_buttons[i].isSeparator) continue;
            char *tip = g_tooltipText[i];
            int pos = 0;
            if (g_buttons[i].isCategory) {
                snprintf(tip, sizeof(g_tooltipText[i]), "Category: %s", g_buttons[i].name);
            } else {
                pos += snprintf(tip + pos, sizeof(g_tooltipText[i]) - pos,
                                "%s", g_buttons[i].path);
                if (g_buttons[i].args[0])
                    pos += snprintf(tip + pos, sizeof(g_tooltipText[i]) - pos,
                                    "\nArgs: %s", g_buttons[i].args);
                if (g_buttons[i].workDir[0])
                    pos += snprintf(tip + pos, sizeof(g_tooltipText[i]) - pos,
                                    "\nDir:  %s", g_buttons[i].workDir);
                if (g_buttons[i].admin)
                    pos += snprintf(tip + pos, sizeof(g_tooltipText[i]) - pos,
                                    "\n[Run as Administrator]");
                if (g_buttons[i].launchMode == 1)
                    pos += snprintf(tip + pos, sizeof(g_tooltipText[i]) - pos,
                                    "\n[Launch Minimized]");
                else if (g_buttons[i].launchMode == 2)
                    snprintf(tip + pos, sizeof(g_tooltipText[i]) - pos,
                             "\n[Launch Hidden]");
            }
            TOOLINFO ti; ZeroMemory(&ti, sizeof(ti));
            ti.cbSize   = sizeof(TOOLINFO);
            ti.uFlags   = TTF_IDISHWND | TTF_SUBCLASS;
            ti.hwnd     = g_hwndMain;
            ti.uId      = (UINT_PTR)g_hwndBtns[i];
            ti.lpszText = g_tooltipText[i];
            SendMessage(g_hwndTooltip, TTM_ADDTOOL, 0, (LPARAM)&ti);
        }
    }
}

/* ── Filter-only refresh ─────────────────────────────────────────────── */
/* Called when the search text changes. Instead of destroying and recreating
   all child windows, this function shows or hides existing button windows
   and repositions them, which eliminates the flicker caused by repeated
   DestroyWindow / CreateWindow cycles on every keystroke. For layout changes
   that require full reconstruction (compact mode, profile switch, etc.) the
   code still calls RefreshMainWindow. */
static void ApplyFilter(void)
{
    if (g_compactMode) { RefreshMainWindow(); return; }

    int btnW  = g_winWidth - 20;
    int y     = 10 + 22 + 6;

    if (g_filterText[0]) {
        /* Flat filtered list: show only matching regular buttons. */
        for (int i = 0; i < g_count; i++) {
            if (!g_hwndBtns[i]) continue;
            int show = ButtonMatchesFilter(i);
            if (show) {
                SetWindowPos(g_hwndBtns[i], NULL, 10, y, btnW, 26,
                             SWP_NOZORDER | SWP_NOACTIVATE | SWP_SHOWWINDOW);
                y += 26 + 5;
            } else {
                ShowWindow(g_hwndBtns[i], SW_HIDE);
            }
        }
    } else {
        /* Full list with category collapse state restored. */
        int catCollapsed = 0;
        for (int i = 0; i < g_count; i++) {
            if (!g_hwndBtns[i]) continue;
            if (g_buttons[i].isCategory) {
                catCollapsed = g_collapsed[i];
                SetWindowPos(g_hwndBtns[i], NULL, 10, y, btnW, 24,
                             SWP_NOZORDER | SWP_NOACTIVATE | SWP_SHOWWINDOW);
                y += 24 + 3;
            } else if (catCollapsed) {
                ShowWindow(g_hwndBtns[i], SW_HIDE);
            } else {
                int h = g_buttons[i].isSeparator ? 14 : 26;
                SetWindowPos(g_hwndBtns[i], NULL, 10, y, btnW, h,
                             SWP_NOZORDER | SWP_NOACTIVATE | SWP_SHOWWINDOW);
                y += h + 5;
            }
        }
    }

    HWND hAdd = GetDlgItem(g_hwndMain, ID_ADD_BTN);
    SetWindowPos(hAdd, HWND_TOP, 10, y, btnW, 26, SWP_SHOWWINDOW | SWP_FRAMECHANGED);
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
    if (!g_buttons[idx].isSeparator && !g_buttons[idx].isCategory)
        AppendMenu(hMenu, MF_STRING, IDM_EDIT_BTN, "Edit...");
    AppendMenu(hMenu, MF_STRING | (g_count >= MAX_BUTTONS ? MF_GRAYED : 0),
               IDM_DUPLICATE_BTN, "Duplicate");
    if (!g_buttons[idx].isSeparator && !g_buttons[idx].isCategory && g_buttons[idx].path[0])
        AppendMenu(hMenu, MF_STRING, IDM_OPEN_LOCATION, "Open File Location");
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
        { char buf[8]; snprintf(buf, sizeof(buf), "%d", g_fontSize);
          CreateWindow("EDIT", buf, WS_VISIBLE|WS_CHILD|WS_BORDER|ES_NUMBER,
                       95, 82, 40, 20, hwnd, (HMENU)IDC_FONT_EDIT, g_hInst, NULL); }
        CreateWindow("STATIC", "pt", WS_VISIBLE|WS_CHILD,
                     140, 84, 20, 18, hwnd, NULL, g_hInst, NULL);
        /* ── Layout ── */
        CreateWindow("STATIC", "Layout", WS_VISIBLE|WS_CHILD,
                     10, 113, 200, 16, hwnd, NULL, g_hInst, NULL);
        CreateWindow("STATIC", "Window width:", WS_VISIBLE|WS_CHILD,
                     10, 133, 100, 18, hwnd, NULL, g_hInst, NULL);
        { char buf[8]; snprintf(buf, sizeof(buf), "%d", g_winWidth);
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
        CreateWindow("BUTTON", "Compact mode (icon grid, no labels)", WS_VISIBLE|WS_CHILD|BS_AUTOCHECKBOX,
                     10, 230, 255, 20, hwnd, (HMENU)IDC_COMPACT_CHECK, g_hInst, NULL);
        SendDlgItemMessage(hwnd, IDC_COMPACT_CHECK, BM_SETCHECK,
                           g_compactMode ? BST_CHECKED : BST_UNCHECKED, 0);
        /* ── Window ── */
        CreateWindow("STATIC", "Window", WS_VISIBLE|WS_CHILD,
                     10, 260, 200, 16, hwnd, NULL, g_hInst, NULL);
        CreateWindow("STATIC", "Title:", WS_VISIBLE|WS_CHILD,
                     10, 280, 40, 18, hwnd, NULL, g_hInst, NULL);
        CreateWindow("EDIT", g_winTitle, WS_VISIBLE|WS_CHILD|WS_BORDER|ES_AUTOHSCROLL,
                     55, 278, 220, 20, hwnd, (HMENU)IDC_TITLE_EDIT, g_hInst, NULL);
        CreateWindow("STATIC", "Opacity:", WS_VISIBLE|WS_CHILD,
                     10, 306, 55, 18, hwnd, NULL, g_hInst, NULL);
        { char buf[8]; snprintf(buf, sizeof(buf), "%d", g_opacity);
          CreateWindow("EDIT", buf, WS_VISIBLE|WS_CHILD|WS_BORDER|ES_NUMBER,
                       70, 304, 40, 20, hwnd, (HMENU)IDC_OPACITY_EDIT, g_hInst, NULL); }
        CreateWindow("STATIC", "% (10-100)", WS_VISIBLE|WS_CHILD,
                     115, 306, 75, 18, hwnd, NULL, g_hInst, NULL);
        /* ── Buttons ── */
        CreateWindow("BUTTON", "Save",   WS_VISIBLE|WS_CHILD|BS_DEFPUSHBUTTON,
                     108, 338, 80, 26, hwnd, (HMENU)IDC_OK,     g_hInst, NULL);
        CreateWindow("BUTTON", "Cancel", WS_VISIBLE|WS_CHILD|BS_PUSHBUTTON,
                     198, 338, 80, 26, hwnd, (HMENU)IDC_CANCEL, g_hInst, NULL);
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
            int newCompact = (SendDlgItemMessage(hwnd, IDC_COMPACT_CHECK, BM_GETCHECK, 0, 0) == BST_CHECKED) ? 1 : 0;
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
            if (newCompact != g_compactMode) { g_compactMode = newCompact; needRefresh = 1; }
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

/* ── Prompt dialog (single text input — used for new profile name) ───── */
static HBRUSH g_hbrPromptBg = NULL;

static LRESULT CALLBACK PromptDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    LRESULT dark = HandleDlgDarkColor(hwnd, msg, wParam, &g_hbrPromptBg);
    if (dark != -1) return dark;
    switch (msg) {
    case WM_CREATE:
        CreateWindow("STATIC", "Profile name:", WS_VISIBLE|WS_CHILD,
                     10, 12, 110, 18, hwnd, NULL, g_hInst, NULL);
        CreateWindow("EDIT", "", WS_VISIBLE|WS_CHILD|WS_BORDER|ES_AUTOHSCROLL|WS_TABSTOP,
                     10, 34, 284, 22, hwnd, (HMENU)IDC_PROMPT_EDIT, g_hInst, NULL);
        CreateWindow("BUTTON", "OK", WS_VISIBLE|WS_CHILD|BS_DEFPUSHBUTTON|WS_TABSTOP,
                     114, 66, 80, 26, hwnd, (HMENU)IDC_PROMPT_OK, g_hInst, NULL);
        CreateWindow("BUTTON", "Cancel", WS_VISIBLE|WS_CHILD|BS_PUSHBUTTON|WS_TABSTOP,
                     204, 66, 80, 26, hwnd, (HMENU)IDC_PROMPT_CANCEL, g_hInst, NULL);
        return 0;
    case WM_COMMAND:
        if (LOWORD(wParam) == IDC_PROMPT_OK) {
            GetDlgItemText(hwnd, IDC_PROMPT_EDIT, g_promptResult, sizeof(g_promptResult));
            g_promptDone = 1; DestroyWindow(hwnd);
        } else if (LOWORD(wParam) == IDC_PROMPT_CANCEL) {
            g_promptResult[0] = '\0'; g_promptCancelled = 1; g_promptDone = 1; DestroyWindow(hwnd);
        }
        return 0;
    case WM_CLOSE:
        g_promptResult[0] = '\0'; g_promptCancelled = 1; g_promptDone = 1; DestroyWindow(hwnd); return 0;
    case WM_DESTROY:
        if (g_hbrPromptBg) { DeleteObject(g_hbrPromptBg); g_hbrPromptBg = NULL; }
        g_hwndDlg = NULL; return 0;
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

        CreateWindow("STATIC", "Working directory (optional):", WS_VISIBLE|WS_CHILD,
                     10, 172, 240, 18, hwnd, NULL, g_hInst, NULL);
        CreateWindow("EDIT", edit ? bc->workDir : "",
                     WS_VISIBLE|WS_CHILD|WS_BORDER|ES_AUTOHSCROLL,
                     10, 192, 285, 24, hwnd, (HMENU)IDC_WORKDIR_EDIT, g_hInst, NULL);
        CreateWindow("BUTTON", "Browse...", WS_VISIBLE|WS_CHILD|BS_PUSHBUTTON,
                     305, 192, 75, 24, hwnd, (HMENU)IDC_WORKDIR_BROWSE, g_hInst, NULL);

        CreateWindow("BUTTON", "Run as administrator",
                     WS_VISIBLE|WS_CHILD|BS_AUTOCHECKBOX,
                     10, 225, 220, 22, hwnd, (HMENU)IDC_ADMIN_CHECK, g_hInst, NULL);
        if (edit && bc->admin) SendDlgItemMessage(hwnd, IDC_ADMIN_CHECK, BM_SETCHECK, BST_CHECKED, 0);

        CreateWindow("STATIC", "Launch mode:",
                     WS_VISIBLE|WS_CHILD, 10, 254, 90, 18, hwnd, NULL, g_hInst, NULL);
        { HWND hCb = CreateWindow("COMBOBOX", "",
                     WS_VISIBLE|WS_CHILD|WS_TABSTOP|CBS_DROPDOWNLIST,
                     105, 252, 150, 80, hwnd, (HMENU)IDC_LAUNCH_COMBO, g_hInst, NULL);
          SendMessage(hCb, CB_ADDSTRING, 0, (LPARAM)"Normal");
          SendMessage(hCb, CB_ADDSTRING, 0, (LPARAM)"Minimized");
          SendMessage(hCb, CB_ADDSTRING, 0, (LPARAM)"Hidden");
          SendMessage(hCb, CB_SETCURSEL, edit ? bc->launchMode : 0, 0); }

        CreateWindow("BUTTON", "Show icon from program",
                     WS_VISIBLE|WS_CHILD|BS_AUTOCHECKBOX,
                     10, 280, 220, 20, hwnd, (HMENU)IDC_ICON_CHECK, g_hInst, NULL);
        if (edit && bc->showIcon) SendDlgItemMessage(hwnd, IDC_ICON_CHECK, BM_SETCHECK, BST_CHECKED, 0);

        CreateWindow("STATIC", "Custom icon (ICO/PNG, optional):",
                     WS_VISIBLE|WS_CHILD, 10, 308, 220, 18, hwnd, NULL, g_hInst, NULL);
        CreateWindow("EDIT", edit ? bc->iconPath : "",
                     WS_VISIBLE|WS_CHILD|WS_BORDER|ES_AUTOHSCROLL,
                     10, 328, 285, 22, hwnd, (HMENU)IDC_ICON_PATH_EDIT, g_hInst, NULL);
        CreateWindow("BUTTON", "Browse...", WS_VISIBLE|WS_CHILD|BS_PUSHBUTTON,
                     305, 328, 75, 22, hwnd, (HMENU)IDC_ICON_BROWSE, g_hInst, NULL);

        CreateWindow("BUTTON", "Separator (divider line)",
                     WS_VISIBLE|WS_CHILD|BS_AUTOCHECKBOX,
                     10, 358, 220, 20, hwnd, (HMENU)IDC_SEP_CHECK, g_hInst, NULL);
        CreateWindow("BUTTON", "Category header (collapsible group)",
                     WS_VISIBLE|WS_CHILD|BS_AUTOCHECKBOX,
                     10, 382, 255, 20, hwnd, (HMENU)IDC_CAT_CHECK, g_hInst, NULL);
        if (edit && bc->isSeparator) {
            SendDlgItemMessage(hwnd, IDC_SEP_CHECK, BM_SETCHECK, BST_CHECKED, 0);
            EnableWindow(GetDlgItem(hwnd, IDC_NAME_EDIT),       FALSE);
            EnableWindow(GetDlgItem(hwnd, IDC_PATH_EDIT),       FALSE);
            EnableWindow(GetDlgItem(hwnd, IDC_ARGS_EDIT),       FALSE);
            EnableWindow(GetDlgItem(hwnd, IDC_BROWSE),          FALSE);
            EnableWindow(GetDlgItem(hwnd, IDC_WORKDIR_EDIT),    FALSE);
            EnableWindow(GetDlgItem(hwnd, IDC_WORKDIR_BROWSE),  FALSE);
            EnableWindow(GetDlgItem(hwnd, IDC_ADMIN_CHECK),     FALSE);
            EnableWindow(GetDlgItem(hwnd, IDC_ICON_CHECK),      FALSE);
            EnableWindow(GetDlgItem(hwnd, IDC_ICON_PATH_EDIT),  FALSE);
            EnableWindow(GetDlgItem(hwnd, IDC_ICON_BROWSE),     FALSE);
            EnableWindow(GetDlgItem(hwnd, IDC_CAT_CHECK),       FALSE);
        }
        if (edit && bc->isCategory) {
            SendDlgItemMessage(hwnd, IDC_CAT_CHECK, BM_SETCHECK, BST_CHECKED, 0);
            EnableWindow(GetDlgItem(hwnd, IDC_PATH_EDIT),       FALSE);
            EnableWindow(GetDlgItem(hwnd, IDC_ARGS_EDIT),       FALSE);
            EnableWindow(GetDlgItem(hwnd, IDC_BROWSE),          FALSE);
            EnableWindow(GetDlgItem(hwnd, IDC_WORKDIR_EDIT),    FALSE);
            EnableWindow(GetDlgItem(hwnd, IDC_WORKDIR_BROWSE),  FALSE);
            EnableWindow(GetDlgItem(hwnd, IDC_ADMIN_CHECK),     FALSE);
            EnableWindow(GetDlgItem(hwnd, IDC_ICON_CHECK),      FALSE);
            EnableWindow(GetDlgItem(hwnd, IDC_ICON_PATH_EDIT),  FALSE);
            EnableWindow(GetDlgItem(hwnd, IDC_ICON_BROWSE),     FALSE);
            EnableWindow(GetDlgItem(hwnd, IDC_SEP_CHECK),       FALSE);
        }

        CreateWindow("BUTTON", edit ? "Save" : "Add",
                     WS_VISIBLE|WS_CHILD|BS_DEFPUSHBUTTON,
                     210, 418, 80, 28, hwnd, (HMENU)IDC_OK, g_hInst, NULL);
        CreateWindow("BUTTON", "Cancel",
                     WS_VISIBLE|WS_CHILD|BS_PUSHBUTTON,
                     300, 418, 80, 28, hwnd, (HMENU)IDC_CANCEL, g_hInst, NULL);
        return 0;
    }

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDC_SEP_CHECK: {
            BOOL sep = (SendDlgItemMessage(hwnd, IDC_SEP_CHECK, BM_GETCHECK, 0, 0) == BST_CHECKED);
            if (sep) SendDlgItemMessage(hwnd, IDC_CAT_CHECK, BM_SETCHECK, BST_UNCHECKED, 0);
            EnableWindow(GetDlgItem(hwnd, IDC_NAME_EDIT),      !sep);
            EnableWindow(GetDlgItem(hwnd, IDC_PATH_EDIT),      !sep);
            EnableWindow(GetDlgItem(hwnd, IDC_ARGS_EDIT),      !sep);
            EnableWindow(GetDlgItem(hwnd, IDC_BROWSE),         !sep);
            EnableWindow(GetDlgItem(hwnd, IDC_WORKDIR_EDIT),   !sep);
            EnableWindow(GetDlgItem(hwnd, IDC_WORKDIR_BROWSE), !sep);
            EnableWindow(GetDlgItem(hwnd, IDC_ADMIN_CHECK),    !sep);
            EnableWindow(GetDlgItem(hwnd, IDC_ICON_CHECK),     !sep);
            EnableWindow(GetDlgItem(hwnd, IDC_ICON_PATH_EDIT), !sep);
            EnableWindow(GetDlgItem(hwnd, IDC_ICON_BROWSE),    !sep);
            EnableWindow(GetDlgItem(hwnd, IDC_CAT_CHECK),      !sep);
            break;
        }
        case IDC_CAT_CHECK: {
            BOOL cat = (SendDlgItemMessage(hwnd, IDC_CAT_CHECK, BM_GETCHECK, 0, 0) == BST_CHECKED);
            if (cat) SendDlgItemMessage(hwnd, IDC_SEP_CHECK, BM_SETCHECK, BST_UNCHECKED, 0);
            /* Category only needs a name; disable path/args/icon/admin */
            EnableWindow(GetDlgItem(hwnd, IDC_PATH_EDIT),      !cat);
            EnableWindow(GetDlgItem(hwnd, IDC_ARGS_EDIT),      !cat);
            EnableWindow(GetDlgItem(hwnd, IDC_BROWSE),         !cat);
            EnableWindow(GetDlgItem(hwnd, IDC_WORKDIR_EDIT),   !cat);
            EnableWindow(GetDlgItem(hwnd, IDC_WORKDIR_BROWSE), !cat);
            EnableWindow(GetDlgItem(hwnd, IDC_ADMIN_CHECK),    !cat);
            EnableWindow(GetDlgItem(hwnd, IDC_ICON_CHECK),     !cat);
            EnableWindow(GetDlgItem(hwnd, IDC_ICON_PATH_EDIT), !cat);
            EnableWindow(GetDlgItem(hwnd, IDC_ICON_BROWSE),    !cat);
            EnableWindow(GetDlgItem(hwnd, IDC_SEP_CHECK),      !cat);
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
        case IDC_WORKDIR_BROWSE: {
            /* Use SHBrowseForFolder to pick a directory */
            BROWSEINFO bi; ZeroMemory(&bi, sizeof(bi));
            bi.hwndOwner = hwnd;
            bi.lpszTitle = "Select working directory";
            bi.ulFlags   = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
            LPITEMIDLIST pidl = SHBrowseForFolder(&bi);
            if (pidl) {
                char dir[MAX_PATH] = "";
                if (SHGetPathFromIDList(pidl, dir))
                    SetDlgItemText(hwnd, IDC_WORKDIR_EDIT, dir);
                CoTaskMemFree(pidl);
            }
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
            int isCat = (SendDlgItemMessage(hwnd, IDC_CAT_CHECK, BM_GETCHECK, 0, 0) == BST_CHECKED) ? 1 : 0;
            char name[256], path[MAX_PATH], args[512];
            GetDlgItemText(hwnd, IDC_NAME_EDIT, name, 256);
            GetDlgItemText(hwnd, IDC_PATH_EDIT, path, MAX_PATH);
            GetDlgItemText(hwnd, IDC_ARGS_EDIT, args, 512);
            if (!isSep && !isCat && (!name[0] || !path[0])) {
                MessageBox(hwnd, "Name and Path are required.", "Missing info", MB_OK | MB_ICONWARNING);
                break;
            }
            if (isCat && !name[0]) {
                MessageBox(hwnd, "Please enter a category name.", "Missing info", MB_OK | MB_ICONWARNING);
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
                g_buttons[ti].workDir[0]  = 0;
                g_buttons[ti].iconPath[0] = 0;
                g_buttons[ti].admin       = 0;
                g_buttons[ti].showIcon    = 0;
                g_buttons[ti].isCategory  = 0;
            } else if (isCat) {
                strcpy(g_buttons[ti].name, name);
                g_buttons[ti].path[0]     = 0;
                g_buttons[ti].args[0]     = 0;
                g_buttons[ti].workDir[0]  = 0;
                g_buttons[ti].iconPath[0] = 0;
                g_buttons[ti].admin       = 0;
                g_buttons[ti].showIcon    = 0;
                g_buttons[ti].isCategory  = 1;
            } else {
                char iconPath[MAX_PATH], workDir[MAX_PATH];
                GetDlgItemText(hwnd, IDC_ICON_PATH_EDIT, iconPath, MAX_PATH);
                GetDlgItemText(hwnd, IDC_WORKDIR_EDIT,   workDir,  MAX_PATH);
                strcpy(g_buttons[ti].name,     name);
                strcpy(g_buttons[ti].path,     path);
                strcpy(g_buttons[ti].args,     args);
                strcpy(g_buttons[ti].workDir,  workDir);
                strcpy(g_buttons[ti].iconPath, iconPath);
                g_buttons[ti].admin    = (SendDlgItemMessage(hwnd, IDC_ADMIN_CHECK, BM_GETCHECK, 0, 0) == BST_CHECKED) ? 1 : 0;
                g_buttons[ti].showIcon = (SendDlgItemMessage(hwnd, IDC_ICON_CHECK,  BM_GETCHECK, 0, 0) == BST_CHECKED) ? 1 : 0;
                g_buttons[ti].isCategory = 0;
                int lm = (int)SendDlgItemMessage(hwnd, IDC_LAUNCH_COMBO, CB_GETCURSEL, 0, 0);
                g_buttons[ti].launchMode = (lm >= 0 && lm <= 2) ? lm : 0;
            }
            g_buttons[ti].isSeparator = isSep;
            if (g_editIndex < 0) g_count++;
            LoadSingleButtonIcon(ti);
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
        if (g_darkMode) SendMessage(hEdit, EM_SETBKGNDCOLOR, 0, (LPARAM)DK_BG);
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
    if (!g_hwndDlg) return;
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
    HBRUSH hbr = g_hbrDkMenuBg ? g_hbrDkMenuBg : CreateSolidBrush(DK_MENU_BG);
    FillRect(hdc, &rcBar, hbr);
    if (!g_hbrDkMenuBg) DeleteObject(hbr); /* only free if we had to create a fallback */
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
        CreateWindow("BUTTON", "+ Add Button / Separator / Header", WS_VISIBLE|WS_CHILD|BS_OWNERDRAW,
                     10, 10, g_winWidth - 20, 26,
                     hwnd, (HMENU)ID_ADD_BTN, g_hInst, NULL);
        return 0;

    case WM_CTLCOLOREDIT: {
        HWND hCtrl = (HWND)lParam;
        if (g_darkMode && hCtrl == g_hwndSearch) {
            HDC hdc = (HDC)wParam;
            SetTextColor(hdc, DK_TEXT); SetBkColor(hdc, DK_SEARCH);
            if (!g_hbrSearchDk) g_hbrSearchDk = CreateSolidBrush(DK_SEARCH);
            return (LRESULT)g_hbrSearchDk;
        }
        return DefWindowProc(hwnd, msg, wParam, lParam);
    }

    case WM_CLOSE:
        if (g_minToTray) {
            ShowWindow(hwnd, SW_HIDE);
            AddTrayIcon();
        } else {
            /* SaveAll() is called inside WM_DESTROY; do not call it here
               to avoid writing the INI twice on a normal close. */
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
            HMENU hSub = CreatePopupMenu();
            for (int i = 0; i < g_profileCount; i++) {
                UINT f = MF_STRING | (i == g_activeProfile ? MF_CHECKED : 0);
                AppendMenu(hSub, f, IDM_PROFILE_BASE + i, g_profileNames[i]);
            }
            AppendMenu(hSub, MF_SEPARATOR, 0, NULL);
            AppendMenu(hSub, MF_STRING, IDM_PROFILE_NEW, "New Profile...");
            AppendMenu(hSub, MF_STRING | (g_activeProfile == 0 ? MF_GRAYED : 0),
                       IDM_PROFILE_DELETE, "Delete Current Profile");
            HMENU hM = CreatePopupMenu();
            AppendMenu(hM, MF_POPUP, (UINT_PTR)hSub, "Profiles");
            AppendMenu(hM, MF_SEPARATOR, 0, NULL);
            AppendMenu(hM, MF_STRING, IDM_TRAY_RESTORE, "Restore");
            AppendMenu(hM, MF_SEPARATOR, 0, NULL);
            AppendMenu(hM, MF_STRING, IDM_TRAY_EXIT, "Exit");
            SetForegroundWindow(hwnd);
            int cmd = TrackPopupMenu(hM, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, NULL);
            DestroyMenu(hM);
            if (cmd == IDM_TRAY_RESTORE) {
                RemoveTrayIcon(); ShowWindow(hwnd, SW_RESTORE); SetForegroundWindow(hwnd);
            } else if (cmd == IDM_TRAY_EXIT) {
                RemoveTrayIcon(); DestroyWindow(hwnd); /* SaveAll() runs in WM_DESTROY */
            } else if (cmd >= IDM_PROFILE_BASE && cmd < IDM_PROFILE_BASE + g_profileCount) {
                SwitchProfile(cmd - IDM_PROFILE_BASE);
            } else if (cmd == IDM_PROFILE_NEW) {
                PostMessage(hwnd, WM_COMMAND, IDM_PROFILE_NEW, 0);
            } else if (cmd == IDM_PROFILE_DELETE) {
                PostMessage(hwnd, WM_COMMAND, IDM_PROFILE_DELETE, 0);
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
            mis->itemWidth  = sz.cx + 4;   /* minimal padding — 5 items must fit in one row */
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
            /* Use cached brushes; no per-hover alloc/free needed. */
            HBRUSH hbr = hot ? g_hbrDkMenuHot : g_hbrDkMenuBg;
            if (!hbr) {
                /* Fallback if EnsureDarkGDI has not run yet. */
                hbr = CreateSolidBrush(hot ? DK_MENU_HOT : DK_MENU_BG);
                FillRect(dis->hDC, &dis->rcItem, hbr); DeleteObject(hbr);
            } else {
                FillRect(dis->hDC, &dis->rcItem, hbr);
            }
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
        int notif = HIWORD(wParam);

        /* Search bar */
        if (id == IDC_SEARCH_EDIT && notif == EN_CHANGE) {
            GetWindowText(g_hwndSearch, g_filterText, sizeof(g_filterText));
            /* Keep the precomputed lowercase copy in sync. */
            int fi = 0;
            for (const char *p = g_filterText; *p && fi < 255; p++)
                g_filterTextLower[fi++] = (char)tolower((unsigned char)*p);
            g_filterTextLower[fi] = '\0';
            ApplyFilter();
            return 0;
        }

        if (id == IDM_MOVE_UP && g_ctxIndex > 0) {
            ButtonConfig tmp = g_buttons[g_ctxIndex];
            g_buttons[g_ctxIndex] = g_buttons[g_ctxIndex - 1];
            g_buttons[g_ctxIndex - 1] = tmp;
            HICON hi = g_icons[g_ctxIndex];
            g_icons[g_ctxIndex] = g_icons[g_ctxIndex - 1];
            g_icons[g_ctxIndex - 1] = hi;
            int ci = g_collapsed[g_ctxIndex];
            g_collapsed[g_ctxIndex]     = g_collapsed[g_ctxIndex - 1];
            g_collapsed[g_ctxIndex - 1] = ci;
            SaveAll(); RefreshMainWindow(); g_ctxIndex = -1;

        } else if (id == IDM_MOVE_DOWN && g_ctxIndex >= 0 && g_ctxIndex < g_count - 1) {
            ButtonConfig tmp = g_buttons[g_ctxIndex];
            g_buttons[g_ctxIndex] = g_buttons[g_ctxIndex + 1];
            g_buttons[g_ctxIndex + 1] = tmp;
            HICON hi = g_icons[g_ctxIndex];
            g_icons[g_ctxIndex] = g_icons[g_ctxIndex + 1];
            g_icons[g_ctxIndex + 1] = hi;
            int ci = g_collapsed[g_ctxIndex];
            g_collapsed[g_ctxIndex]     = g_collapsed[g_ctxIndex + 1];
            g_collapsed[g_ctxIndex + 1] = ci;
            SaveAll(); RefreshMainWindow(); g_ctxIndex = -1;

        } else if (id == IDM_EDIT_BTN && g_ctxIndex >= 0) {
            if (g_hwndDlg) { SetForegroundWindow(g_hwndDlg); break; }
            g_editIndex = g_ctxIndex; g_ctxIndex = -1;
            g_hwndDlg = CreateWindowEx(WS_EX_DLGMODALFRAME, "AddDlgClass", "Edit Button",
                WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
                CW_USEDEFAULT, CW_USEDEFAULT, 400, 490,
                hwnd, NULL, g_hInst, NULL);
            if (!g_hwndDlg) break;
            SetTitleBarDark(g_hwndDlg, g_darkMode);
            ShowWindow(g_hwndDlg, SW_SHOW); UpdateWindow(g_hwndDlg);

        } else if (id == IDM_DELETE_BTN && g_ctxIndex >= 0) {
            char confirm[300];
            snprintf(confirm, sizeof(confirm), "Delete \"%s\"?", g_buttons[g_ctxIndex].name);
            if (MessageBox(hwnd, confirm, "Confirm Delete", MB_YESNO | MB_ICONQUESTION) == IDYES) {
                if (g_icons[g_ctxIndex]) { DestroyIcon(g_icons[g_ctxIndex]); g_icons[g_ctxIndex] = NULL; }
                for (int i = g_ctxIndex; i < g_count - 1; i++) {
                    g_buttons[i]   = g_buttons[i + 1];
                    g_icons[i]     = g_icons[i + 1];
                    g_collapsed[i] = g_collapsed[i + 1];
                }
                g_icons[g_count - 1]     = NULL;
                g_collapsed[g_count - 1] = 0;
                g_count--;
                SaveAll(); RefreshMainWindow();
            }
            g_ctxIndex = -1;

        } else if (id == IDM_DUPLICATE_BTN && g_ctxIndex >= 0) {
            if (g_count >= MAX_BUTTONS) {
                MessageBox(hwnd, "Maximum number of buttons reached.", "Error", MB_OK | MB_ICONWARNING);
            } else {
                /* Shift everything after the source down one slot */
                for (int i = g_count; i > g_ctxIndex + 1; i--) {
                    g_buttons[i]   = g_buttons[i - 1];
                    g_icons[i]     = NULL;
                    g_collapsed[i] = g_collapsed[i - 1];
                }
                /* Copy the source into the slot immediately below it */
                g_buttons[g_ctxIndex + 1] = g_buttons[g_ctxIndex];
                char *nm = g_buttons[g_ctxIndex + 1].name;
                if ((int)strlen(nm) + 7 < 256) strcat(nm, " (copy)");
                g_icons[g_ctxIndex + 1]     = NULL;
                g_collapsed[g_ctxIndex + 1] = 0;
                g_count++;
                LoadSingleButtonIcon(g_ctxIndex + 1);
                SaveAll();
                RefreshMainWindow();
            }
            g_ctxIndex = -1;

        } else if (id == IDM_OPEN_LOCATION && g_ctxIndex >= 0) {
            if (g_buttons[g_ctxIndex].path[0]) {
                /* Expand env vars before extracting directory */
                char expanded[MAX_PATH];
                ExpandEnvVars(g_buttons[g_ctxIndex].path, expanded, MAX_PATH);
                char dir[MAX_PATH];
                strncpy(dir, expanded, MAX_PATH - 1);
                dir[MAX_PATH - 1] = '\0';
                char *slash = strrchr(dir, '\\');
                if (!slash) slash = strrchr(dir, '/');
                if (slash) *slash = '\0';
                ShellExecute(NULL, "explore", dir, NULL, NULL, SW_SHOW);
            }
            g_ctxIndex = -1;

        } else if (id >= IDM_PROFILE_BASE && id < IDM_PROFILE_BASE + g_profileCount) {
            SwitchProfile(id - IDM_PROFILE_BASE);

        } else if (id == ID_PROFILES_MENU) {
            POINT pt; GetCursorPos(&pt);
            ShowProfilesMenu(hwnd, pt.x, pt.y);

        } else if (id == IDM_PROFILE_NEW) {
            if (g_hwndDlg) { SetForegroundWindow(g_hwndDlg); return 0; }
            g_promptResult[0] = '\0'; g_promptDone = 0; g_promptCancelled = 0;
            HWND hPr = CreateWindowEx(WS_EX_DLGMODALFRAME, "PromptDlgClass", "New Profile Name",
                WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
                CW_USEDEFAULT, CW_USEDEFAULT, 316, 140,
                hwnd, NULL, g_hInst, NULL);
            if (!hPr) return 0;
            g_hwndDlg = hPr; SetTitleBarDark(hPr, g_darkMode);
            ShowWindow(hPr, SW_SHOW); UpdateWindow(hPr);
            MSG pmsg = {0};
            /* GetMessage returns 0 on WM_QUIT; re-post it so the main loop exits. */
            while (!g_promptDone && GetMessage(&pmsg, NULL, 0, 0) > 0) {
                if (IsWindow(hPr) && IsDialogMessage(hPr, &pmsg)) continue;
                TranslateMessage(&pmsg); DispatchMessage(&pmsg);
            }
            if (pmsg.message == WM_QUIT) {
                PostQuitMessage((int)pmsg.wParam);
                return 0;
            }
            if (!g_promptCancelled && g_promptResult[0]) {
                /* Sanitize name: reject control characters, dots, and the
                   standard Windows filename-illegal characters. Dots are
                   blocked because a name of ".." would produce a confusing
                   path and is never a meaningful profile name. */
                char safe[64]; int si = 0;
                for (int i = 0; g_promptResult[i] && si < 63; i++) {
                    unsigned char c = (unsigned char)g_promptResult[i];
                    /* Allow only printable ASCII (32-126); this also rejects
                       all high-byte multi-byte sequences that could be split
                       at the 63-char truncation boundary and misinterpreted
                       by Windows ANSI file APIs. */
                    if (c < 32 || c > 126) continue;
                    if (c == '.') continue;      /* block dot to prevent ".." etc. */
                    if (c == '/' || c == '\\' || c == ':' || c == '*' ||
                        c == '?' || c == '"'  || c == '<' || c == '>' || c == '|')
                        continue;
                    safe[si++] = (char)c;
                }
                safe[si] = '\0';
                if (!safe[0] || _stricmp(safe, "Default") == 0) {
                    MessageBox(hwnd, "Invalid or reserved profile name.", "Error", MB_OK | MB_ICONWARNING);
                } else {
                    /* Check for duplicate */
                    int dup = 0;
                    for (int i = 0; i < g_profileCount; i++)
                        if (_stricmp(g_profileNames[i], safe) == 0) { dup = 1; break; }
                    if (dup) {
                        char duptxt[128];
                        snprintf(duptxt, sizeof(duptxt), "A profile named \"%s\" already exists.", safe);
                        MessageBox(hwnd, duptxt, "Duplicate Profile", MB_OK | MB_ICONWARNING);
                    } else {
                        char dir[MAX_PATH]; strcpy(dir, g_basePath);
                        char *ls = strrchr(dir, '\\'); if (!ls) ls = strrchr(dir, '/');
                        if (ls) *(ls + 1) = '\0'; else dir[0] = '\0';
                        /* Guard: ensure the full path won't exceed MAX_PATH */
                        if (strlen(dir) + 9 + strlen(safe) + 4 >= MAX_PATH) {
                            MessageBox(hwnd, "Profile name is too long.", "Error", MB_OK | MB_ICONWARNING);
                        } else {
                            char newPath[MAX_PATH];
                            snprintf(newPath, MAX_PATH, "%slauncher_%s.ini", dir, safe);
                            FILE *tf = fopen(newPath, "a"); if (tf) fclose(tf);
                            ScanProfiles();
                            for (int i = 0; i < g_profileCount; i++)
                                if (_stricmp(g_profileNames[i], safe) == 0) { SwitchProfile(i); break; }
                        }
                    }
                }
            }

        } else if (id == IDM_PROFILE_DELETE) {
            if (g_activeProfile == 0) {
                MessageBox(hwnd, "Cannot delete the Default profile.", "Error", MB_OK | MB_ICONWARNING);
            } else {
                char confirm[200];
                snprintf(confirm, sizeof(confirm), "Delete profile \"%s\"?", g_profileNames[g_activeProfile]);
                if (MessageBox(hwnd, confirm, "Confirm Delete", MB_YESNO | MB_ICONQUESTION) == IDYES) {
                    char delPath[MAX_PATH]; strcpy(delPath, g_profilePaths[g_activeProfile]);
                    SwitchProfile(0);
                    DeleteFile(delPath);
                    ScanProfiles();
                    RebuildMenu();
                }
            }

        } else if (id == ID_OPEN_INI) {
            /* Use the fully-qualified system Notepad path to prevent a
               rogue notepad.exe earlier on %PATH% from being executed. */
            char notepadPath[MAX_PATH];
            ExpandEnvironmentStrings("%SystemRoot%\\system32\\notepad.exe",
                                     notepadPath, MAX_PATH);
            ShellExecute(NULL, "open", notepadPath, g_iniPath, NULL, SW_SHOW);

        } else if (id == ID_HELP_INSTRUCTIONS) {
            ShowInfoDialog(hwnd, "Instructions",
                "HOW IT WORKS\r\n"
                "Simple Launcher shows a configurable list of buttons. "
                "Click a button to launch the program or file assigned to it.\r\n"
                "\r\n"
                "ADDING A BUTTON\r\n"
                "Click '+ Add Button'. Fill in:\r\n"
                "  - Display name, Path, Arguments, Working directory\r\n"
                "  - Launch mode: Normal, Minimized, or Hidden\r\n"
                "  - Run as administrator (UAC prompt)\r\n"
                "  - Show icon / Custom icon\r\n"
                "  - Separator or Category header\r\n"
                "\r\n"
                "ENVIRONMENT VARIABLES\r\n"
                "Use %VAR% tokens in Path, Arguments, and Working Directory. "
                "They are expanded at launch time. Examples:\r\n"
                "  %USERPROFILE%\\Documents\\script.bat\r\n"
                "  %APPDATA%\\MyApp\\app.exe\r\n"
                "\r\n"
                "PROFILES\r\n"
                "Profiles let you keep separate button sets in different INI "
                "files. Use the Profiles menu (menu bar or tray) to switch, "
                "create, or delete profiles. The Default profile (launcher.ini) "
                "cannot be deleted. Profile files are named launcher_<name>.ini.\r\n"
                "\r\n"
                "LAUNCH MODE\r\n"
                "Normal: standard window. Minimized: starts in the taskbar. "
                "Hidden: no window shown (useful for background scripts).\r\n"
                "\r\n"
                "CATEGORIES\r\n"
                "Add a 'Category header' to group buttons under a collapsible "
                "section. Click the category bar to collapse or expand it.\r\n"
                "\r\n"
                "SEARCH / FILTER\r\n"
                "Type in the search bar to instantly filter buttons by name.\r\n"
                "\r\n"
                "COMPACT MODE\r\n"
                "Enable in Settings for a small icon-grid palette.\r\n"
                "\r\n"
                "RIGHT-CLICK MENU\r\n"
                "Edit, Duplicate, Open File Location, Delete, Move Up/Down.\r\n"
                "\r\n"
                "CONFIGURATION FILE\r\n"
                "Settings are saved to launcher.ini (Default) or "
                "launcher_<name>.ini for named profiles, next to launcher.exe. "
                "Click 'Open INI' to open the active profile in Notepad.");

        } else if (id == ID_SETTINGS) {
            if (g_hwndDlg) { SetForegroundWindow(g_hwndDlg); break; }
            g_hwndDlg = CreateWindowEx(WS_EX_DLGMODALFRAME, "SettingsDlgClass", "Settings",
                WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
                CW_USEDEFAULT, CW_USEDEFAULT, 295, 410,
                hwnd, NULL, g_hInst, NULL);
            if (!g_hwndDlg) break;
            SetTitleBarDark(g_hwndDlg, g_darkMode);
            ShowWindow(g_hwndDlg, SW_SHOW); UpdateWindow(g_hwndDlg);

        } else if (id == ID_HELP_ABOUT) {
            ShowInfoDialog(hwnd, "About Simple Launcher",
                "Simple Launcher\r\n"
                "Version 2.14\r\n"
                "\r\n"
                "Author:   UberGuidoZ\r\n"
                "Contact:  https://github.com/UberGuidoZ");

        } else if (id == ID_ADD_BTN) {
            if (g_hwndDlg) { SetForegroundWindow(g_hwndDlg); break; }
            g_editIndex = -1;
            g_hwndDlg = CreateWindowEx(WS_EX_DLGMODALFRAME, "AddDlgClass", "Add Button",
                WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
                CW_USEDEFAULT, CW_USEDEFAULT, 400, 490,
                hwnd, NULL, g_hInst, NULL);
            if (!g_hwndDlg) break;
            SetTitleBarDark(g_hwndDlg, g_darkMode);
            ShowWindow(g_hwndDlg, SW_SHOW); UpdateWindow(g_hwndDlg);

        } else if (id >= ID_BUTTON_BASE && id < ID_BUTTON_BASE + g_count) {
            int idx = id - ID_BUTTON_BASE;
            /* Category header: toggle collapsed state */
            if (g_buttons[idx].isCategory) {
                g_collapsed[idx] = !g_collapsed[idx];
                RefreshMainWindow();
                return 0;
            }
            if (!g_buttons[idx].isSeparator) {
                char exPath[MAX_PATH], exArgs[512], exDir[MAX_PATH];
                ExpandEnvVars(g_buttons[idx].path,    exPath, MAX_PATH);
                ExpandEnvVars(g_buttons[idx].args,    exArgs, sizeof(exArgs));
                ExpandEnvVars(g_buttons[idx].workDir, exDir,  MAX_PATH);
                const char *workDir = exDir[0] ? exDir : NULL;
                const char *args    = exArgs[0] ? exArgs : NULL;
                int nShow = SW_SHOW;
                if (g_buttons[idx].launchMode == 1) nShow = SW_SHOWMINIMIZED;
                else if (g_buttons[idx].launchMode == 2) nShow = SW_HIDE;
                HINSTANCE hRet = ShellExecute(NULL,
                                     g_buttons[idx].admin ? "runas" : "open",
                                     exPath, args, workDir, nShow);
                if ((INT_PTR)hRet <= 32) {
                    char errmsg[512];
                    snprintf(errmsg, sizeof(errmsg),
                             "Failed to launch:\n%s\n\nError code: %d",
                             exPath, (int)(INT_PTR)hRet);
                    MessageBox(hwnd, errmsg, "Launch Error", MB_OK | MB_ICONWARNING);
                }
            }
        }
        return 0;
    }

    case WM_DESTROY:
        RemoveTrayIcon();
        FreeIcons();
        if (g_hwndTooltip) { DestroyWindow(g_hwndTooltip); g_hwndTooltip = NULL; }
        if (g_hwndSearch)  { DestroyWindow(g_hwndSearch);  g_hwndSearch  = NULL; }
        SaveAll();
        if (g_hbrDkBg)     { DeleteObject(g_hbrDkBg);     g_hbrDkBg     = NULL; }
        if (g_hbrSearchDk) { DeleteObject(g_hbrSearchDk); g_hbrSearchDk = NULL; }
        FreeDarkGDI();
        if (g_hFont)       { DeleteObject(g_hFont);       g_hFont       = NULL; }
        if (g_hFontBold)   { DeleteObject(g_hFontBold);   g_hFontBold   = NULL; }
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

/* ── Entry point ─────────────────────────────────────────────────────── */
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrev, LPSTR lpCmd, int nShow)
{
    g_hInst = hInstance;
    /* Initialize COM for the apartment so SHBrowseForFolder with
       BIF_NEWDIALOGSTYLE and other shell operations work reliably. */
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    InitCommonControls();   /* required to register TOOLTIPS_CLASS and other common controls */
    GetBasePath();
    ScanProfiles();
    LoadSettings();
    LoadButtons();
    RecreateFont();
    LoadButtonIcons();
    if (g_darkMode) {
        g_hbrDkBg     = CreateSolidBrush(DK_BG);
        g_hbrSearchDk = CreateSolidBrush(DK_SEARCH);
    }
    /* Always create the shared cached GDI objects; category header brushes
       are needed in both dark and light mode. */
    EnsureDarkGDI();

    /* Register dialog window classes — reuse one struct, vary only proc + name */
    WNDCLASS wcd = {0};
    wcd.hInstance    = hInstance;
    wcd.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wcd.hCursor      = LoadCursor(NULL, IDC_ARROW);
    wcd.lpfnWndProc  = InfoDlgProc;     wcd.lpszClassName = "InfoDlgClass";     RegisterClass(&wcd);
    wcd.lpfnWndProc  = SettingsDlgProc; wcd.lpszClassName = "SettingsDlgClass"; RegisterClass(&wcd);
    wcd.lpfnWndProc  = AddDlgProc;      wcd.lpszClassName = "AddDlgClass";      RegisterClass(&wcd);
    wcd.lpfnWndProc  = PromptDlgProc;   wcd.lpszClassName = "PromptDlgClass";   RegisterClass(&wcd);

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
    if (!g_hwndMain) return 1;

    RebuildMenu();
    RefreshMainWindow();
    SetTitleBarDark(g_hwndMain, g_darkMode);
    ApplyOpacity();
    if (g_alwaysOnTop)
        SetWindowPos(g_hwndMain, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);

    ShowWindow(g_hwndMain, nShow);
    UpdateWindow(g_hwndMain);

    MSG msg = {0};
    while (GetMessage(&msg, NULL, 0, 0) > 0) {
        if (g_hwndDlg && IsDialogMessage(g_hwndDlg, &msg)) continue;
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    CoUninitialize();
    return (int)msg.wParam;
}
