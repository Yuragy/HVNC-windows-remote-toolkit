#define WIN32_LEAN_AND_MEAN
#include "DesktopHandler.h"
#include "Network.h"
#include "Compression.h"

#include <Windows.h>
#include <iostream>
#include <thread>
#include <vector>

void DesktopThreadFunc(const std::string& serverIp, int port)
{
    SOCKET s = ConnectToServer(serverIp, port);
    if(s == INVALID_SOCKET)
    {
        std::cerr << "[Desktop] Cannot connect to server\n";
        return;
    }

    if(!SendHandshake(s, Connection::desktop))
    {
        std::cerr << "[Desktop] SendHandshake failed\n";
        closesocket(s);
        return;
    }
    std::cout << "[Desktop] Connected.\n";

    if(!InitCompression())
    {
        std::cerr << "[Desktop] LZNT1 compression not available.\n";
        closesocket(s);
        return;
    }

    for(;;)
    {
        int realRight=0, realBottom=0;
        if(RecvInt(s, realRight) <= 0) break;
        if(RecvInt(s, realBottom)<= 0) break;

        bool recvPixels=true;
        if(!SendAll(s, (const char*)&recvPixels, sizeof(recvPixels)))
            break;
        if(!recvPixels)
        {
            continue;
        }
        DWORD scw=0, sch=0;
        BYTE* rawPixels = CaptureScreen24(&scw, &sch);
        if(!rawPixels) break;

        DWORD width  = scw;
        DWORD height = sch;

        DWORD uncompressedSize = width*3*height;
        std::vector<BYTE> outBuf(uncompressedSize+1024);
        DWORD compressedSize=0;

        bool ok = CompressLZNT1(rawPixels, uncompressedSize, outBuf, compressedSize);
        free(rawPixels);
        if(!ok)
        {
            std::cerr << "[Desktop] Compress failed\n";
            break;
        }

        DWORD serverWidth  = scw;
        DWORD serverHeight = sch;

        if(!SendAll(s, (const char*)&serverWidth,  sizeof(serverWidth)))   break;
        if(!SendAll(s, (const char*)&serverHeight, sizeof(serverHeight)))  break;
        if(!SendAll(s, (const char*)&width,        sizeof(width)))         break;
        if(!SendAll(s, (const char*)&height,       sizeof(height)))        break;
        if(!SendAll(s, (const char*)&compressedSize,sizeof(compressedSize)))break;

        if(!SendAll(s, (const char*)outBuf.data(), compressedSize))
            break;

        int ack=0;
        if(RecvInt(s, ack)<=0) break;

        Sleep(50);
    }

    std::cout << "[Desktop] Disconnected\n";
    closesocket(s);
}
