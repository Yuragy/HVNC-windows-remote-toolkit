#pragma once

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <vector>

bool InitCompression();
bool CompressLZNT1(const BYTE* inBuf, DWORD inSize, std::vector<BYTE>& outBuf, DWORD& outSize);
BYTE* CaptureScreen24(DWORD* width, DWORD* height);
