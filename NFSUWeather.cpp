// NFSUWeather - Hujan, Kabut, Langit Gelap untuk NFS Underground 1 (EXPERIMENTAL / belum diuji)
// Cara kerja: menyadap IDirect3DDevice9::Present dan ::SetRenderState (vtable dibagi semua device).
// Tidak butuh alamat memori speed.exe.
//
// Build (pilih salah satu, WAJIB 32-bit / x86):
//   MinGW : i686-w64-mingw32-g++ -shared -O2 -static -o NFSUWeather.asi NFSUWeather.cpp -luser32
//   MSVC  : cl /LD /O2 /EHsc NFSUWeather.cpp user32.lib /Fe:NFSUWeather.asi   (Developer Command Prompt x86)
// Pasang: NFSUWeather.asi + NFSUWeather.ini ke folder "scripts" (atau folder game) bersama ASI loader.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d9.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

struct Config {
    int   rain, rainDrops, fog, fixedFog, dark;
    float rainSpeed, rainLength, rainWind;
    int   rainAlpha;
    int   fogR, fogG, fogB, hazeAlpha;
    float fogStart, fogEnd;
    int   darkR, darkG, darkB, darkAlpha;
    int   keyRain, keyFog, keyDark;
};
static Config C;
static bool bRain, bFog, bDark;
static char iniPath[MAX_PATH];

static float GetF(const char* sec, const char* key, float def) {
    char buf[64], d[64]; snprintf(d, sizeof(d), "%f", def);
    GetPrivateProfileStringA(sec, key, d, buf, 64, iniPath);
    return (float)atof(buf);
}
static int GetI(const char* sec, const char* key, int def) {
    return GetPrivateProfileIntA(sec, key, def, iniPath);
}

static void LoadConfig() {
    C.rain = GetI("Rain", "Enabled", 1);
    C.rainDrops = GetI("Rain", "Drops", 400);
    C.rainSpeed = GetF("Rain", "Speed", 1400.0f);
    C.rainLength = GetF("Rain", "Length", 28.0f);
    C.rainWind = GetF("Rain", "Wind", 0.15f);
    C.rainAlpha = GetI("Rain", "Alpha", 110);
    C.fog = GetI("Fog", "Enabled", 1);
    C.fixedFog = GetI("Fog", "UseGameFog", 1);
    C.fogR = GetI("Fog", "R", 150); C.fogG = GetI("Fog", "G", 155); C.fogB = GetI("Fog", "B", 165);
    C.fogStart = GetF("Fog", "Start", 30.0f);
    C.fogEnd = GetF("Fog", "End", 250.0f);
    C.hazeAlpha = GetI("Fog", "HazeAlpha", 60);
    C.dark = GetI("DarkSky", "Enabled", 1);
    C.darkR = GetI("DarkSky", "R", 5); C.darkG = GetI("DarkSky", "G", 8); C.darkB = GetI("DarkSky", "B", 25);
    C.darkAlpha = GetI("DarkSky", "Alpha", 110);
    C.keyRain = GetI("Hotkeys", "ToggleRain", 118);
    C.keyFog = GetI("Hotkeys", "ToggleFog", 119);
    C.keyDark = GetI("Hotkeys", "ToggleDark", 120);
    if (C.rainDrops > 2000) C.rainDrops = 2000;
    bRain = C.rain != 0; bFog = C.fog != 0; bDark = C.dark != 0;
}

typedef HRESULT (WINAPI *SetRS_t)(IDirect3DDevice9*, D3DRENDERSTATETYPE, DWORD);
typedef HRESULT (WINAPI *Present_t)(IDirect3DDevice9*, const RECT*, const RECT*, HWND, const RGNDATA*);
static SetRS_t   oSetRS = NULL;
static Present_t oPresent = NULL;
static bool g_internal = false;

static DWORD F2D(float f) { union { float f; DWORD d; } u; u.f = f; return u.d; }

struct TLV { float x, y, z, rhw; DWORD color; };
#define TLV_FVF (D3DFVF_XYZRHW | D3DFVF_DIFFUSE)

struct Drop { float x, y, v, len; };
static Drop drops[2000];
static bool dropsInit = false;
static float frand() { return (float)rand() / (float)RAND_MAX; }

static HRESULT WINAPI hkSetRS(IDirect3DDevice9* dev, D3DRENDERSTATETYPE st, DWORD v) {
    if (!g_internal && bFog && C.fixedFog) {
        switch (st) {
        case D3DRS_FOGENABLE:     v = TRUE; break;
        case D3DRS_FOGCOLOR:      v = D3DCOLOR_XRGB(C.fogR, C.fogG, C.fogB); break;
        case D3DRS_FOGSTART:      v = F2D(C.fogStart); break;
        case D3DRS_FOGEND:        v = F2D(C.fogEnd); break;
        case D3DRS_FOGTABLEMODE:  v = D3DFOG_NONE; break;
        case D3DRS_FOGVERTEXMODE: v = D3DFOG_LINEAR; break;
        default: break;
        }
    }
    return oSetRS(dev, st, v);
}

static void ApplyFixedFog(IDirect3DDevice9* dev) {
    g_internal = true;
    if (bFog && C.fixedFog) {
        oSetRS(dev, D3DRS_FOGCOLOR, D3DCOLOR_XRGB(C.fogR, C.fogG, C.fogB));
        oSetRS(dev, D3DRS_FOGSTART, F2D(C.fogStart));
        oSetRS(dev, D3DRS_FOGEND, F2D(C.fogEnd));
        oSetRS(dev, D3DRS_FOGVERTEXMODE, D3DFOG_LINEAR);
        oSetRS(dev, D3DRS_FOGENABLE, TRUE);
    }
    g_internal = false;
}

static void Quad(IDirect3DDevice9* dev, float x, float y, float w, float h, DWORD col) {
    TLV v[4] = {
        { x - 0.5f,     y - 0.5f,     0, 1, col }, { x + w - 0.5f, y - 0.5f,     0, 1, col },
        { x - 0.5f,     y + h - 0.5f, 0, 1, col }, { x + w - 0.5f, y + h - 0.5f, 0, 1, col } };
    dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, v, sizeof(TLV));
}

static void DrawOverlay(IDirect3DDevice9* dev) {
    bool haze = bFog && C.hazeAlpha > 0;
    if (!bRain && !haze && !bDark) return;

    D3DVIEWPORT9 vp;
    if (FAILED(dev->GetViewport(&vp))) return;
    float X = (float)vp.X, Y = (float)vp.Y, W = (float)vp.Width, H = (float)vp.Height;

    IDirect3DStateBlock9* sb = NULL;
    if (FAILED(dev->CreateStateBlock(D3DSBT_ALL, &sb))) return;
    g_internal = true;
    if (SUCCEEDED(dev->BeginScene())) {
        dev->SetVertexShader(NULL); dev->SetPixelShader(NULL);
        dev->SetFVF(TLV_FVF); dev->SetTexture(0, NULL);
        dev->SetRenderState(D3DRS_ZENABLE, FALSE);
        dev->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
        dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
        dev->SetRenderState(D3DRS_LIGHTING, FALSE);
        dev->SetRenderState(D3DRS_FOGENABLE, FALSE);
        dev->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
        dev->SetRenderState(D3DRS_STENCILENABLE, FALSE);
        dev->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
        dev->SetRenderState(D3DRS_COLORWRITEENABLE, 0xF);
        dev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
        dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
        dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
        dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
        dev->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
        dev->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
        dev->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
        dev->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
        dev->SetTextureStageState(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);

        if (bDark) Quad(dev, X, Y, W, H, D3DCOLOR_ARGB(C.darkAlpha, C.darkR, C.darkG, C.darkB));
        if (haze)  Quad(dev, X, Y, W, H, D3DCOLOR_ARGB(C.hazeAlpha, C.fogR, C.fogG, C.fogB));

        if (bRain) {
            static DWORD last = 0;
            DWORD now = GetTickCount();
            float dt = last ? (now - last) / 1000.0f : 0.016f;
            last = now;
            if (dt > 0.1f) dt = 0.1f;
            if (!dropsInit) {
                for (int i = 0; i < 2000; i++) {
                    drops[i].x = frand() * W; drops[i].y = frand() * H;
                    drops[i].v = 0.7f + frand() * 0.6f; drops[i].len = 0.6f + frand() * 0.8f;
                }
                dropsInit = true;
            }
            static TLV lines[2000 * 2];
            DWORD col = D3DCOLOR_ARGB(C.rainAlpha, 200, 210, 235);
            DWORD colT = D3DCOLOR_ARGB(0, 200, 210, 235);
            int n = C.rainDrops;
            for (int i = 0; i < n; i++) {
                Drop& d = drops[i];
                d.y += C.rainSpeed * d.v * dt * (H / 720.0f);
                d.x += C.rainSpeed * d.v * dt * C.rainWind * (H / 720.0f);
                if (d.y > H) { d.y = -20.0f; d.x = frand() * W; }
                if (d.x > W) d.x -= W;
                float L = C.rainLength * d.len * (H / 720.0f);
                lines[i * 2]     = { X + d.x - C.rainWind * L, Y + d.y - L, 0, 1, colT };
                lines[i * 2 + 1] = { X + d.x, Y + d.y, 0, 1, col };
            }
            dev->DrawPrimitiveUP(D3DPT_LINELIST, n, lines, sizeof(TLV));
        }
        dev->EndScene();
    }
    sb->Apply();
    sb->Release();
    g_internal = false;
}

static void PollHotkeys() {
    static bool d1, d2, d3;
    bool k1 = (GetAsyncKeyState(C.keyRain) & 0x8000) != 0;
    bool k2 = (GetAsyncKeyState(C.keyFog) & 0x8000) != 0;
    bool k3 = (GetAsyncKeyState(C.keyDark) & 0x8000) != 0;
    if (k1 && !d1) bRain = !bRain;
    if (k2 && !d2) bFog = !bFog;
    if (k3 && !d3) bDark = !bDark;
    d1 = k1; d2 = k2; d3 = k3;
}

static HRESULT WINAPI hkPresent(IDirect3DDevice9* dev, const RECT* a, const RECT* b, HWND c, const RGNDATA* d) {
    static bool busy = false;
    if (!busy) {
        busy = true;
        PollHotkeys();
        ApplyFixedFog(dev);
        DrawOverlay(dev);
        busy = false;
    }
    return oPresent(dev, a, b, c, d);
}

static void* Patch(void** vt, int idx, void* hook) {
    DWORD old;
    VirtualProtect(&vt[idx], sizeof(void*), PAGE_READWRITE, &old);
    void* orig = vt[idx];
    vt[idx] = hook;
    VirtualProtect(&vt[idx], sizeof(void*), old, &old);
    return orig;
}

static DWORD WINAPI InitThread(LPVOID) {
    HMODULE h = LoadLibraryA("d3d9.dll");
    if (!h) return 0;
    typedef IDirect3D9* (WINAPI *Create_t)(UINT);
    Create_t create = (Create_t)GetProcAddress(h, "Direct3DCreate9");
    if (!create) return 0;
    IDirect3D9* d3d = create(D3D_SDK_VERSION);
    if (!d3d) return 0;

    HWND w = CreateWindowExA(0, "STATIC", "", WS_OVERLAPPED, 0, 0, 8, 8, NULL, NULL, GetModuleHandleA(NULL), NULL);
    D3DPRESENT_PARAMETERS pp = {};
    pp.Windowed = TRUE;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.BackBufferFormat = D3DFMT_UNKNOWN;
    pp.hDeviceWindow = w;
    IDirect3DDevice9* dev = NULL;
    if (SUCCEEDED(d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, w,
                                    D3DCREATE_SOFTWARE_VERTEXPROCESSING, &pp, &dev)) && dev) {
        void** vt = *(void***)dev;
        oPresent = (Present_t)Patch(vt, 17, (void*)hkPresent);   // Present
        oSetRS   = (SetRS_t)  Patch(vt, 57, (void*)hkSetRS);     // SetRenderState
        dev->Release();
    }
    d3d->Release();
    if (w) DestroyWindow(w);
    return 0;
}

BOOL APIENTRY DllMain(HMODULE hMod, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hMod);
        GetModuleFileNameA(hMod, iniPath, MAX_PATH);
        char* dot = strrchr(iniPath, '.');
        if (dot) *dot = 0;
        strcat(iniPath, ".ini");          // NFSUWeather.asi -> NFSUWeather.ini
        LoadConfig();
        CreateThread(NULL, 0, InitThread, NULL, 0, NULL);
    }
    return TRUE;
}
