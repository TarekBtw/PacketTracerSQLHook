#pragma once
#include <d3d11.h>
#include "kiero.h"
#include "minhook/include/MinHook.h"


typedef BOOL(__stdcall* wglSwapBuffers_t)(HDC hDc);
extern wglSwapBuffers_t oSwapBuffers;  

DWORD WINAPI MainThread(LPVOID);