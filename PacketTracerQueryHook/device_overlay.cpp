#include <Windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <vector>
#include <string>
#include <map>
#include <algorithm>
#include <iostream>
#include "imgui/imgui.h"
#include "imgui/imgui_impl_win32.h"
#include "imgui/imgui_impl_dx11.h"
#include "device_overlay.h"
#include "minhook/include/MinHook.h"

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

ImFont* tab_text1 = nullptr;
ImFont* tab_text2 = nullptr;
ImFont* ico = nullptr;
ImFont* ico_subtab = nullptr;

float dpi_scale = 1.0f;

extern std::map<std::string, std::map<std::string, std::string>>* g_deviceAnswers;

// D3D11/ImGui globals
ID3D11Device*           g_pd3dDevice = nullptr;
ID3D11DeviceContext*    g_pd3dDeviceContext = nullptr;
IDXGISwapChain*         g_pSwapChain = nullptr;
HWND                    g_hWnd = nullptr;
WNDPROC                 g_OriginalWndProc = nullptr;
bool                    overlayVisible = false;
int                     currentDeviceIdx = 0;
std::vector<std::string>deviceList;
bool                    imguiInitialized = false;

// Present hook typedef
typedef HRESULT(__stdcall* Present_t)(IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags);
Present_t oPresent = nullptr;

// Forward declarations
LRESULT CALLBACK HookedWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
void UpdateDeviceList();
void RenderDeviceOverlay();

// Device/field overlay logic (unchanged)
void UpdateDeviceList()
{
    deviceList.clear();
    if (!g_deviceAnswers || g_deviceAnswers->empty()) return;
    for (const auto& kv : *g_deviceAnswers)
        deviceList.push_back(kv.first);
    std::sort(deviceList.begin(), deviceList.end());
    if (currentDeviceIdx >= deviceList.size()) currentDeviceIdx = 0;
}

void RenderDeviceOverlay()
{
    if (!overlayVisible) return;
    if (deviceList.empty()) return;
    if (!g_deviceAnswers) return;

    ImGuiIO& io = ImGui::GetIO();
    float width = 480.0f, height = 300.0f;
    ImVec2 pos = ImVec2(io.DisplaySize.x - width - 10, io.DisplaySize.y - height - 10);

    ImGui::SetNextWindowPos(pos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(width, height), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.7f);
    ImGui::Begin("Device Overlay", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoInputs);

    const auto& device = deviceList[currentDeviceIdx];
    ImGui::Text("Device: %s (%d/%d)", device.c_str(), currentDeviceIdx + 1, (int)deviceList.size());

    auto& fields = (*g_deviceAnswers)[device];
    std::vector<std::pair<std::string, std::string>> sorted(fields.begin(), fields.end());
    std::sort(sorted.begin(), sorted.end());
    for (const auto& kv : sorted)
    {
        ImGui::TextUnformatted(("- \"" + kv.first + "\" : \"" + kv.second + "\"").c_str());
    }

    ImGui::End();
}

// ImGui input handling
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

LRESULT CALLBACK HookedWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return true;
    if (msg == WM_KEYDOWN && wParam == VK_F8) {
        overlayVisible = !overlayVisible;
        if (overlayVisible)
            UpdateDeviceList();
    }
    if (overlayVisible && !deviceList.empty()) {
        if (msg == WM_KEYDOWN && wParam == VK_LEFT) {
            currentDeviceIdx = (currentDeviceIdx - 1 + deviceList.size()) % deviceList.size();
        }
        if (msg == WM_KEYDOWN && wParam == VK_RIGHT) {
            currentDeviceIdx = (currentDeviceIdx + 1) % deviceList.size();
        }
    }
    return CallWindowProc(g_OriginalWndProc, hWnd, msg, wParam, lParam);
}

// Present hook
HRESULT __stdcall hkPresent(IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags)
{
    printf("debug: hkPresent called\n");
    static bool wasInit = false;
    if (!wasInit)
    {
        if (SUCCEEDED(pSwapChain->GetDevice(__uuidof(ID3D11Device), (void**)&g_pd3dDevice)))
        {
            g_pd3dDevice->GetImmediateContext(&g_pd3dDeviceContext);

            // Get window handle
            DXGI_SWAP_CHAIN_DESC desc;
            pSwapChain->GetDesc(&desc);
            g_hWnd = desc.OutputWindow;

            // ImGui init
            IMGUI_CHECKVERSION();
            ImGui::CreateContext();
            ImGui_ImplWin32_Init(g_hWnd);
            ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);
            imguiInitialized = true;

            g_OriginalWndProc = (WNDPROC)SetWindowLongPtr(g_hWnd, GWLP_WNDPROC, (LONG_PTR)HookedWndProc);

            wasInit = true;
        }
    }

    if (imguiInitialized)
    {
        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        // --- Simple Overlay for test ---
        static bool show_window = true;
        static int counter = 0;
        if (show_window) {
            ImGui::Begin("Simple Overlay", &show_window, ImGuiWindowFlags_AlwaysAutoResize);
            ImGui::Text("Hello from ImGui!");
            if (ImGui::Button("Increment Counter")) {
                counter++;
            }
            ImGui::SameLine();
            ImGui::Text("Counter: %d", counter);
            if (ImGui::Button("Close")) {
                show_window = false;
            }
            ImGui::End();
        }

        // Your device overlay
        RenderDeviceOverlay();

        ImGui::Render();
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    }

    return oPresent(pSwapChain, SyncInterval, Flags);
}

// MainThread: set up D3D11 Present hook with MinHook
DWORD WINAPI MainThread(LPVOID)
{
    std::cout << "[DEBUG] MainThread started" << std::endl;

    // Create dummy D3D11 device + swapchain to get Present address
    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferCount = 1;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = GetForegroundWindow(); // Any valid HWND
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    ID3D11Device* pDummyDevice = nullptr;
    ID3D11DeviceContext* pDummyContext = nullptr;
    IDXGISwapChain* pDummySwapChain = nullptr;

    HRESULT hr = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
        nullptr, 0, D3D11_SDK_VERSION, &sd, &pDummySwapChain, &pDummyDevice, nullptr, &pDummyContext);

    if (SUCCEEDED(hr) && pDummySwapChain)
    {
        void** pVTable = *(void***)(pDummySwapChain);
        void* pPresentAddr = pVTable[8]; // Present is index 8
        
            if (MH_CreateHook(pPresentAddr, &hkPresent, reinterpret_cast<LPVOID*>(&oPresent)) == MH_OK)
            {
                MH_EnableHook(pPresentAddr);
                std::cout << "[DEBUG] IDXGISwapChain::Present hooked!" << std::endl;
            }
            else
                std::cout << "[DEBUG] MH_CreateHook failed!" << std::endl;
    }
    else
    {
        std::cout << "[DEBUG] Dummy device creation failed!" << std::endl;
    }

    if (pDummyContext) pDummyContext->Release();
    if (pDummySwapChain) pDummySwapChain->Release();
    if (pDummyDevice) pDummyDevice->Release();

    return 0;
}