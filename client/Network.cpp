#define WIN32_LEAN_AND_MEAN
#include <windows.h>    
#include <winsock2.h>
#include <ws2tcpip.h> 
#pragma comment(lib, "ws2_32.lib")

#include "Network.h"
#include <iostream>

static bool g_winsockInited = false;

bool InitWinSock()
{
    if(g_winsockInited) return true;

    WSADATA wsa;
    int err = WSAStartup(MAKEWORD(2,2), &wsa);
    if(err != 0)
    {
        std::cerr << "[!] WSAStartup failed: " << err << std::endl;
        return false;
    }
    g_winsockInited = true;
    return true;
}

SOCKET ConnectToServer(const std::string& ip, int port)
{
    if(!InitWinSock()) 
        return INVALID_SOCKET;

    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if(s == INVALID_SOCKET)
    {
        std::cerr << "[!] socket() error\n";
        return INVALID_SOCKET;
    }

    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(port);

    if(inet_pton(AF_INET, ip.c_str(), &addr.sin_addr) != 1)
    {
        std::cerr << "[!] Invalid IP address: " << ip << "\n";
        closesocket(s);
        return INVALID_SOCKET;
    }

    if(connect(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR)
    {
        std::cerr << "[!] connect() error\n";
        closesocket(s);
        return INVALID_SOCKET;
    }
    return s;
}

bool SendAll(SOCKET s, const char* buf, int totalSize)
{
    int sent = 0;
    while(sent < totalSize)
    {
        int r = send(s, buf + sent, totalSize - sent, 0);
        if(r <= 0) 
            return false;
        sent += r;
    }
    return true;
}

bool RecvAll(SOCKET s, char* buf, int totalSize)
{
    int recved = 0;
    while(recved < totalSize)
    {
        int r = recv(s, buf + recved, totalSize - recved, 0);
        if(r <= 0)
            return false;
        recved += r;
    }
    return true;
}

int SendInt(SOCKET s, int val)
{
    return SendAll(s, reinterpret_cast<const char*>(&val), sizeof(val))
           ? sizeof(val)
           : SOCKET_ERROR;
}

int RecvInt(SOCKET s, int& value)
{
    return RecvAll(s, reinterpret_cast<char*>(&value), sizeof(value))
           ? sizeof(value)        
           : 0;                   
}

bool SendHandshake(SOCKET s, Connection c)
{
    if(!SendAll(s, reinterpret_cast<const char*>(gc_magik), sizeof(gc_magik)))
        return false;

    int cc = static_cast<int>(c);
    return SendAll(s, reinterpret_cast<const char*>(&cc), sizeof(cc));
}

bool SendPacket(SOCKET s, int opcode, const void* data, int dataSize)
{
    PacketHeader hdr;
    hdr.opcode    = opcode;
    hdr.dataSize  = dataSize;

    if(!SendAll(s, reinterpret_cast<const char*>(&hdr), sizeof(hdr)))
        return false;

    if(dataSize > 0 && data)
    {
        if(!SendAll(s, static_cast<const char*>(data), dataSize))
            return false;
    }
    return true;
}

bool RecvPacket(SOCKET s, PacketHeader &hdr, std::vector<char> &outData)
{
    if(!RecvAll(s, reinterpret_cast<char*>(&hdr), sizeof(hdr)))
        return false;

    if(hdr.dataSize < 0)
        return false;

    outData.resize(hdr.dataSize);
    if(hdr.dataSize > 0)
    {
        if(!RecvAll(s, outData.data(), hdr.dataSize))
            return false;
    }
    return true;
}
