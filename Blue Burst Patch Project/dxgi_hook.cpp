#include "dxgi_hook.h"
#include <atomic>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <stdio.h>
#include <thread>
#include <windows.h>

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")

static bool g_bDebugLogs = false;

void Log(const char *fmt, ...) {
  if (!g_bDebugLogs)
    return;
  FILE *f = nullptr;
  if (fopen_s(&f, "dxgi_hook.log", "a") == 0 && f) {
    va_list args;
    va_start(args, fmt);
    vfprintf(f, fmt, args);
    va_end(args);
    fclose(f);
  }
}

typedef HRESULT(__stdcall *IDXGISwapChain_Present_Proto)(
    IDXGISwapChain *pSwapChain, UINT SyncInterval, UINT Flags);
IDXGISwapChain_Present_Proto oPresent = nullptr;

typedef HRESULT(__stdcall *IDXGISwapChain1_Present1_Proto)(
    IDXGISwapChain *pSwapChain, UINT SyncInterval, UINT PresentFlags,
    const DXGI_PRESENT_PARAMETERS *pPresentParameters);
IDXGISwapChain1_Present1_Proto oPresent1 = nullptr;

typedef HRESULT(__stdcall *IDXGIFactory_CreateSwapChain_Proto)(
    IDXGIFactory *pFactory, IUnknown *pDevice, DXGI_SWAP_CHAIN_DESC *pDesc,
    IDXGISwapChain **ppSwapChain);
IDXGIFactory_CreateSwapChain_Proto oCreateSwapChain = nullptr;

typedef HRESULT(__stdcall *CreateDXGIFactory_Proto)(REFIID riid, void **ppFactory);
typedef HRESULT(__stdcall *CreateDXGIFactory1_Proto)(REFIID riid, void **ppFactory);
typedef HRESULT(__stdcall *CreateDXGIFactory2_Proto)(UINT Flags, REFIID riid, void **ppFactory);

CreateDXGIFactory_Proto oCreateDXGIFactory = nullptr;
CreateDXGIFactory1_Proto oCreateDXGIFactory1 = nullptr;
CreateDXGIFactory2_Proto oCreateDXGIFactory2 = nullptr;

std::atomic<bool> g_dxgiHooked{false};
std::atomic<bool> g_apiHooked{false};

bool g_bInterpEnabled = false;
DWORD g_targetHz = 60;

void ReadFrameGenSettings() {
  HKEY hKey;
  if (RegOpenKeyExA(HKEY_CURRENT_USER, "Software\\SonicTeam\\PSOBB", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
    DWORD size = sizeof(DWORD);
    DWORD enabled = 0;
    if (RegQueryValueExA(hKey, "FrameGenEnabled", 0, 0, (LPBYTE)&enabled, &size) == ERROR_SUCCESS) {
      g_bInterpEnabled = (enabled != 0);
    }
    DWORD hz = 60;
    if (RegQueryValueExA(hKey, "TargetHz", 0, 0, (LPBYTE)&hz, &size) == ERROR_SUCCESS) {
      g_targetHz = hz;
    }
    DWORD debugLogs = 0;
    if (RegQueryValueExA(hKey, "DebugLogsEnabled", 0, 0, (LPBYTE)&debugLogs, &size) == ERROR_SUCCESS) {
      g_bDebugLogs = (debugLogs != 0);
    }
    RegCloseKey(hKey);

    if (g_bDebugLogs) {
      Log("[BBIO] FrameGen Settings: Enabled=%d, TargetHz=%d, DebugLogs=%d\n", g_bInterpEnabled, g_targetHz, g_bDebugLogs);
    }
  }
}

HRESULT __stdcall MyPresent(IDXGISwapChain *pSwapChain, UINT SyncInterval, UINT Flags);
HRESULT __stdcall MyPresent1(IDXGISwapChain *pSwapChain, UINT SyncInterval, UINT PresentFlags, const DXGI_PRESENT_PARAMETERS *pPresentParameters);

HRESULT __stdcall MyCreateSwapChain(IDXGIFactory *pFactory, IUnknown *pDevice, DXGI_SWAP_CHAIN_DESC *pDesc, IDXGISwapChain **ppSwapChain) {
  HRESULT hr = oCreateSwapChain(pFactory, pDevice, pDesc, ppSwapChain);
  if (SUCCEEDED(hr) && ppSwapChain && *ppSwapChain) {
    IDXGISwapChain *pSwapChain = *ppSwapChain;
    void **vtable = *(void ***)pSwapChain;

    if (vtable[8] != MyPresent) {
      oPresent = (IDXGISwapChain_Present_Proto)vtable[8];
      Log("[BBIO] Hooking SwapChain Present (8). Original: %p\n", oPresent);
      DWORD oldProtect;
      VirtualProtect(&vtable[8], sizeof(void *), PAGE_EXECUTE_READWRITE, &oldProtect);
      vtable[8] = (void *)MyPresent;
      VirtualProtect(&vtable[8], sizeof(void *), oldProtect, &oldProtect);
    }

    if (vtable[22] != MyPresent1) {
      oPresent1 = (IDXGISwapChain1_Present1_Proto)vtable[22];
      Log("[BBIO] Hooking SwapChain Present1 (22). Original: %p\n", oPresent1);
      DWORD oldProtect;
      VirtualProtect(&vtable[22], sizeof(void *), PAGE_EXECUTE_READWRITE, &oldProtect);
      vtable[22] = (void *)MyPresent1;
      VirtualProtect(&vtable[22], sizeof(void *), oldProtect, &oldProtect);
    }

    g_dxgiHooked = true;
  }
  return hr;
}

HRESULT __stdcall MyPresent(IDXGISwapChain *pSwapChain, UINT SyncInterval, UINT Flags) {
  int repeats = 1;
  if (g_bInterpEnabled && g_targetHz > 30) {
    if (g_targetHz >= 240) repeats = 8;
    else if (g_targetHz >= 165) repeats = 6;
    else if (g_targetHz >= 144) repeats = 5;
    else if (g_targetHz >= 120) repeats = 4;
    else if (g_targetHz >= 90) repeats = 3;
    else if (g_targetHz >= 75) repeats = 3;
    else if (g_targetHz >= 60) repeats = 2;
  }
  HRESULT hr = S_OK;
  for (int i = 0; i < repeats; i++) {
    hr = oPresent(pSwapChain, 1, 0); 
  }
  return hr;
}

HRESULT __stdcall MyPresent1(IDXGISwapChain *pSwapChain, UINT SyncInterval, UINT PresentFlags, const DXGI_PRESENT_PARAMETERS *pPresentParameters) {
  int repeats = 1;
  if (g_bInterpEnabled && g_targetHz > 30) {
    if (g_targetHz >= 240) repeats = 8;
    else if (g_targetHz >= 165) repeats = 6;
    else if (g_targetHz >= 144) repeats = 5;
    else if (g_targetHz >= 120) repeats = 4;
    else if (g_targetHz >= 90) repeats = 3;
    else if (g_targetHz >= 75) repeats = 3;
    else if (g_targetHz >= 60) repeats = 2;
  }
  HRESULT hr = S_OK;
  for (int i = 0; i < repeats; i++) {
    hr = oPresent1(pSwapChain, SyncInterval, PresentFlags, pPresentParameters);
  }
  return hr;
}

void InstallDX12Hook() {
  if (g_apiHooked)
    return;
  g_apiHooked = true;
  Log("[BBIO] InstallDX12Hook initialized for frame generation.\n");
}

extern "C" __declspec(dllexport) void __stdcall InstallDXGIHookSync() {
  Log("[BBIO] InstallDXGIHookSync called from Wrapper.\n");
  InstallDX12Hook();
}

void HookThread() { InstallDX12Hook(); }

void SetupDXGIHook() {
  ReadFrameGenSettings();
  Log("[BBIO] SetupDXGIHook called. FrameGen=%d, TargetHz=%d\n", g_bInterpEnabled, g_targetHz);
  std::thread(HookThread).detach();
}
