// NFSUWeather v2 - Hujan natural, kabut horizon, langit gelap bergradasi untuk NFS Underground 1
// Menyadap IDirect3DDevice9::Present & ::SetRenderState (vtable dibagi semua device).
// Tidak butuh alamat memori speed.exe.
//
// Build x86:  cl /LD /O2 /EHsc /MT NFSUWeather.cpp user32.lib /Fe:NFSUWeather.asi

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d9.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

struct Config {
    int   rain, rainDrops, splash, rainAlpha;
    float rainSpeed, rainLength, rainWind, rainGust;
    int   lightning, lightAlpha;
    int   fog, fixedFog, fogR, fogG, fogB, fogTop, fogHorizon, fogBottom;
    float horizon, fogStart, fogEnd;
    int   dark, darkR, darkG, darkB, darkTop, darkBottom;
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
static int Cl(int a) { return a < 0 ? 0 : (a > 255 ? 255 : a); }
static DWORD Col(int a, int r, int g, int b) { return D3DCOLOR_ARGB(Cl(a), Cl(r), Cl(g), Cl(b)); }

static void LoadConfig() {
    C.rain = GetI("Rain", "Enabled", 1);
    C.rainDrops = GetI("Rain", "Drops", 500);
    C.splash = GetI("Rain", "Splash", 1);
    C.rainAlpha = GetI("Rain", "Alpha", 120);
    C.rainSpeed = GetF("Rain", "Speed", 1300.0f);
    C.rainLength = GetF("Rain", "Length", 26.0f);
    C.rainWind = GetF("Rain", "Wind", 0.12f);
    C.rainGust = GetF("Rain", "Gust", 0.10f);
    C.lightning = GetI("Rain", "Lightning", 0);
    C.lightAlpha = GetI("Rain", "LightningAlpha", 110);

    C.fog = GetI("Fog", "Enabled", 1);
    C.fixedFog = GetI("Fog", "UseGameFog", 0);
    C.fogR = GetI("Fog", "R", 75); C.fogG = GetI("Fog", "G", 82); C.fogB = GetI("Fog", "B", 98);
    C.horizon = GetF("Fog", "Horizon", 0.47f);
    C.fogTop = GetI("Fog", "TopAlpha", 20);
    C.fogHorizon = GetI("Fog", "HorizonAlpha", 110);
    C.fogBottom = GetI("Fog", "BottomAlpha", 25);
    C.fogStart = GetF("Fog", "Start", 30.0f);
    C.fogEnd = GetF("Fog", "End", 250.0f);

    C.dark = GetI("DarkSky", "Enabled", 1);
    C.darkR = GetI("DarkSky", "R", 5); C.darkG = GetI("DarkSky", "G", 8); C.darkB = GetI("DarkSky", "B", 25);
    C.darkTop = GetI("DarkSky", "TopAlpha", 150);
    C.darkBottom = GetI("DarkSky", "BottomAlpha", 40);

    C.keyRain = GetI("Hotkeys", "ToggleRain", 118);
    C.keyFog = GetI("Hotkeys", "ToggleFog", 119);
    C.keyDark = GetI("Hotkeys", "ToggleDark", 120);
    if (C.rainDrops > 2000) C.rainDrops = 2000;
    if (C.rainDrops < 0) C.rainDrops = 0;
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

// ---------- Hujan (3 lapisan: jauh, tengah, dekat) ----------
struct Drop { float x, y, v, len, a, gy; int layer; };
struct Splash { float x, y, age, life, size; bool on; };
static Drop drops[2000];
static Splash splashes[300];
static bool dropsInit = false;
static float frand() { return (float)rand() / (float)RAND_MAX; }

static const float LSPEED[3] = { 0.55f, 1.0f, 1.6f };
static const float LLEN[3]   = { 0.5f, 1.0f, 1.8f };
static const float LALPHA[3] = { 0.35f, 0.7f, 1.0f };

static void ResetDrop(Drop& d, int layer, float W, float H, float hz, bool initial) {
    d.layer = layer;
    d.x = frand() * W * 1.2f - W * 0.1f;
    float g0 = H * hz, gh = H - g0;
    float t0 = layer == 0 ? 0.0f : (layer == 1 ? 0.25f : 0.6f);
    float t1 = layer == 0 ? 0.2f : (layer == 1 ? 0.6f : 1.0f);
    d.gy = g0 + gh * (t0 + (t1 - t0) * frand());      // jarak jatuh sesuai perspektif
    d.v = (0.85f + frand() * 0.3f) * LSPEED[layer];
    d.len = (0.7f + frand() * 0.6f) * LLEN[layer];
    d.a = LALPHA[layer] * (0.7f + frand() * 0.3f);
    d.y = initial ? frand() * d.gy : -frand() * 80.0f;
}

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
    if (!(bFog && C.fixedFog)) return;
    g_internal = true;
    oSetRS(dev, D3DRS_FOGCOLOR, D3DCOLOR_XRGB(C.fogR, C.fogG, C.fogB));
    oSetRS(dev, D3DRS_FOGSTART, F2D(C.fogStart));
    oSetRS(dev, D3DRS_FOGEND, F2D(C.fogEnd));
    oSetRS(dev, D3DRS_FOGVERTEXMODE, D3DFOG_LINEAR);
    oSetRS(dev, D3DRS_FOGENABLE, TRUE);
    g_internal = false;
}

static void GradQuad(IDirect3DDevice9* dev, float x, float y, float w, float h, DWORD ct, DWORD cb) {
    TLV v[4] = {
        { x - 0.5f,     y - 0.5f,     0, 1, ct }, { x + w - 0.5f, y - 0.5f,     0, 1, ct },
        { x - 0.5f,     y + h - 0.5f, 0, 1, cb }, { x + w - 0.5f, y + h - 0.5f, 0, 1, cb } };
    dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, v, sizeof(TLV));
}

static TLV lineBuf[2000 * 4 + 300 * 4 + 16];

static void DrawRain(IDirect3DDevice9* dev, float X, float Y, float W, float H) {
    static DWORD last = 0;
    DWORD now = GetTickCount();
    float dt = last ? (now - last) / 1000.0f : 0.016f;
    last = now;
    if (dt > 0.1f) dt = 0.1f;
    float t = now / 1000.0f;
    float s = H / 720.0f;

    if (!dropsInit) {
        for (int i = 0; i < 2000; i++) {
            int m = i % 10;
            ResetDrop(drops[i], m < 4 ? 0 : (m < 8 ? 1 : 2), W, H, C.horizon, true);
        }
        dropsInit = true;
    }
    float gust = C.rainGust * (sinf(t * 0.35f) * 0.6f + sinf(t * 0.9f + 1.7f) * 0.4f);
    float slant = C.rainWind + gust;
    int cnt = 0;
    int n = C.rainDrops;
    for (int i = 0; i < n; i++) {
        Drop& d = drops[i];
        float dy = C.rainSpeed * d.v * s * dt;
        d.y += dy;
        d.x += dy * slant;
        if (d.y >= d.gy) {
            if (C.splash && d.layer >= 1) {
                for (int k = 0; k < 300; k++) if (!splashes[k].on) {
                    splashes[k].on = true; splashes[k].x = d.x; splashes[k].y = d.gy;
                    splashes[k].age = 0; splashes[k].life = 0.18f + frand() * 0.12f;
                    splashes[k].size = (d.layer == 2 ? 6.0f : 3.5f) * s;
                    break;
                }
            }
            ResetDrop(d, d.layer, W, H, C.horizon, false);
            continue;
        }
        float L = C.rainLength * d.len * s;
        int a = (int)(C.rainAlpha * d.a);
        DWORD head = Col(a, 195, 205, 228), tail = Col(0, 195, 205, 228);
        float hx = X + d.x, hy = Y + d.y;
        lineBuf[cnt++] = { hx - slant * L, hy - L, 0, 1, tail };
        lineBuf[cnt++] = { hx, hy, 0, 1, head };
        if (d.layer == 2) {   // tetes dekat dibuat sedikit lebih tebal
            lineBuf[cnt++] = { hx - slant * L + 1.0f, hy - L, 0, 1, tail };
            lineBuf[cnt++] = { hx + 1.0f, hy, 0, 1, head };
        }
    }
    for (int k = 0; k < 300; k++) {
        Splash& p = splashes[k];
        if (!p.on) continue;
        p.age += dt;
        if (p.age >= p.life) { p.on = false; continue; }
        float f = p.age / p.life;
        float sz = p.size * (0.5f + f);
        DWORD c = Col((int)(C.rainAlpha * 0.8f * (1.0f - f)), 205, 215, 235);
        float px = X + p.x, py = Y + p.y;
        lineBuf[cnt++] = { px, py, 0, 1, c };
        lineBuf[cnt++] = { px - sz, py - sz * 0.6f, 0, 1, c };
        lineBuf[cnt++] = { px, py, 0, 1, c };
        lineBuf[cnt++] = { px + sz, py - sz * 0.6f, 0, 1, c };
    }
    if (cnt >= 2) dev->DrawPrimitiveUP(D3DPT_LINELIST, cnt / 2, lineBuf, sizeof(TLV));

    if (C.lightning) {
        static DWORD nextFlash = 0, flashStart = 0;
        if (!nextFlash) nextFlash = now + 5000 + rand() % 15000;
        if (now >= nextFlash) { flashStart = now; nextFlash = now + 8000 + rand() % 22000; }
        if (flashStart) {
            DWORD fe = now - flashStart;
            float k = 0;
            if (fe < 120) k = 1.0f - fe / 120.0f;
            else if (fe >= 170 && fe < 300) k = 0.6f * (1.0f - (fe - 170) / 130.0f);
            if (k > 0) GradQuad(dev, X, Y, W, H, Col((int)(C.lightAlpha * k), 200, 215, 255), Col((int)(C.lightAlpha * k * 0.5f), 200, 215, 255));
        }
    }
}

static void DrawOverlay(IDirect3DDevice9* dev) {
    if (!(bRain || bFog || bDark)) return;

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

        // Langit gelap: pekat di atas, memudar ke bawah (jalan & HUD bawah tetap terang)
        if (bDark)
            GradQuad(dev, X, Y, W, H, Col(C.darkTop, C.darkR, C.darkG, C.darkB), Col(C.darkBottom, C.darkR, C.darkG, C.darkB));

        // Kabut horizon: paling tebal di garis cakrawala, menipis ke atas dan ke bawah
        if (bFog) {
            float hy = Y + H * C.horizon;
            GradQuad(dev, X, Y, W, hy - Y, Col(C.fogTop, C.fogR, C.fogG, C.fogB), Col(C.fogHorizon, C.fogR, C.fogG, C.fogB));
            GradQuad(dev, X, hy, W, Y + H - hy, Col(C.fogHorizon, C.fogR, C.fogG, C.fogB), Col(C.fogBottom, C.fogR, C.fogG, C.fogB));
        }

        if (bRain) DrawRain(dev, X, Y, W, H);
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
        strcat(iniPath, ".ini");
        LoadConfig();
        CreateThread(NULL, 0, InitThread, NULL, 0, NULL);
    }
    return TRUE;
}
