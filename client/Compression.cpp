#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <iostream>
#include "Compression.h"

#ifndef NTAPI
#define NTAPI __stdcall
#endif

#ifndef NTSTATUS
typedef LONG NTSTATUS;
#endif

#ifndef COMPRESSION_FORMAT_LZNT1
#define COMPRESSION_FORMAT_LZNT1 2
#endif

#ifndef COMPRESSION_ENGINE_STANDARD
#define COMPRESSION_ENGINE_STANDARD 0
#endif

typedef NTSTATUS (NTAPI *T_RtlCompressBuffer)(
    USHORT CompressionFormat,
    PUCHAR UncompressedBuffer,
    ULONG  UncompressedBufferSize,
    PUCHAR CompressedBuffer,
    ULONG  CompressedBufferSize,
    ULONG  UncompressedChunkSize,
    PULONG FinalCompressedSize,
    PVOID  WorkSpace
);

typedef NTSTATUS (NTAPI *T_RtlGetCompressionWorkSpaceSize)(
    USHORT CompressionFormatAndEngine,
    PULONG CompressBufferWorkSpaceSize,
    PULONG CompressFragmentWorkSpaceSize
);

static T_RtlCompressBuffer             pRtlCompressBuffer            = nullptr;
static T_RtlGetCompressionWorkSpaceSize pRtlGetCompressionWorkSpaceSize = nullptr;

static bool  g_inited      = false;
static ULONG g_workspace1  = 0;
static ULONG g_workspace2  = 0;

bool InitCompression()
{
    if(g_inited)
        return true;

    HMODULE ntdll = LoadLibrary(TEXT("ntdll.dll"));
    if(!ntdll)
    {
        std::cerr << "[!] Cannot load ntdll.dll for compression\n";
        return false;
    }

    pRtlCompressBuffer = (T_RtlCompressBuffer)
        GetProcAddress(ntdll, "RtlCompressBuffer");
    pRtlGetCompressionWorkSpaceSize =
        (T_RtlGetCompressionWorkSpaceSize)
        GetProcAddress(ntdll, "RtlGetCompressionWorkSpaceSize");

    if(!pRtlCompressBuffer || !pRtlGetCompressionWorkSpaceSize)
    {
        std::cerr << "[!] Could not get RtlCompressBuffer or RtlGetCompressionWorkSpaceSize\n";
        FreeLibrary(ntdll);
        return false;
    }

    USHORT fmt = COMPRESSION_FORMAT_LZNT1 | COMPRESSION_ENGINE_STANDARD;
    NTSTATUS st = pRtlGetCompressionWorkSpaceSize(fmt, &g_workspace1, &g_workspace2);
    if(st != 0)
    {
        std::cerr << "[!] RtlGetCompressionWorkSpaceSize failed, NTSTATUS=" << st << "\n";
        return false;
    }

    g_inited = true;
    return true;
}

bool CompressLZNT1(const BYTE* inBuf, DWORD inSize,
                   std::vector<BYTE>& outBuf, DWORD& outSize)
{
    if(!g_inited || !pRtlCompressBuffer)
    {
        std::cerr << "[!] Compress: not inited or not available\n";
        return false;
    }

    std::vector<BYTE> workspace(g_workspace1);

    ULONG finalCompressedSize = 0;
    NTSTATUS st = pRtlCompressBuffer(
        COMPRESSION_FORMAT_LZNT1,
        (PUCHAR)inBuf,
        inSize,
        (PUCHAR)outBuf.data(),
        (ULONG)outBuf.size(),
        4096,
        &finalCompressedSize,
        workspace.data()
    );
    if(st != 0)
    {
        std::cerr << "[!] RtlCompressBuffer failed, NTSTATUS=0x"
                  << std::hex << st << std::dec << "\n";
        return false;
    }

    outSize = finalCompressedSize;
    return true;
}

BYTE* CaptureScreen24(DWORD* width, DWORD* height)
{
    *width  = GetSystemMetrics(SM_CXSCREEN);
    *height = GetSystemMetrics(SM_CYSCREEN);

    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth       = (LONG)(*width);
    bmi.bmiHeader.biHeight      = (LONG)(*height);

    bmi.bmiHeader.biPlanes      = 1;
    bmi.bmiHeader.biBitCount    = 24;
    bmi.bmiHeader.biCompression = BI_RGB;
    HDC hScreenDC = GetDC(NULL);
    if(!hScreenDC)
    {
        std::cerr << "[!] Cannot get screen DC.\n";
        return nullptr;
    }
    HDC hMemDC = CreateCompatibleDC(hScreenDC);
    if(!hMemDC)
    {
        ReleaseDC(NULL, hScreenDC);
        std::cerr << "[!] Cannot create memory DC.\n";
        return nullptr;
    }

    void* pBits = nullptr;
    HBITMAP hDib = CreateDIBSection(
        hMemDC, &bmi, DIB_RGB_COLORS, &pBits, NULL, 0
    );
    if(!hDib || !pBits)
    {
        if(hDib) DeleteObject(hDib);
        DeleteDC(hMemDC);
        ReleaseDC(NULL, hScreenDC);
        std::cerr << "[!] CreateDIBSection failed.\n";
        return nullptr;
    }

    HGDIOBJ oldObj = SelectObject(hMemDC, hDib);
    BitBlt(hMemDC, 0, 0, (int)*width, (int)*height,
           hScreenDC, 0, 0, SRCCOPY);
    SelectObject(hMemDC, oldObj);
    size_t imageSize = (size_t)(*width) * (size_t)(*height) * 3; 
    BYTE* outBuf = (BYTE*)malloc(imageSize);
    if(outBuf)
    {
        memcpy(outBuf, pBits, imageSize);
    }
    else
    {
        std::cerr << "[!] Malloc for outBuf failed.\n";
    }
    DeleteObject(hDib);
    DeleteDC(hMemDC);
    ReleaseDC(NULL, hScreenDC);

    return outBuf;
}

