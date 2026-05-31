#include "main.h"

static NOTIFYICONDATAW g_nid = {};
static HWND  g_msgWnd = nullptr;
static HWND  g_overlay = nullptr;
static bool  g_active = false;
static bool  g_dragging = false;
static bool g_clampPan = true;   // select clamp by default
static POINT g_lastPt = {};

static Capture g_cap;
static Renderer g_rend;
static ComPtr<ID3D11Texture2D> g_frame;  // keep this screenshot till release
static Config g_cfg;
static bool g_settingsOpen = false;
static int  g_settingsResult = 0;
static HWND g_settingsWnd = nullptr;
static int    g_capturing = 0; // 0 = not capturing


LRESULT CALLBACK OverlayProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_KEYDOWN:
        if (wp == VK_ESCAPE) { HideOverlay(); return 0; }
        if (wp == g_cfg.keyReset) { g_rend.ResetView(); return 0; }
        break;

    case WM_MOUSEWHEEL: {
        POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        ScreenToClient(hwnd, &pt);
        float factor = (GET_WHEEL_DELTA_WPARAM(wp) > 0) ? 1.1f : 1.0f / 1.1f;
        g_rend.ZoomAt((float)pt.x, (float)pt.y, factor);
        return 0;
    }

    case WM_LBUTTONDOWN:
        g_dragging = true;
        g_lastPt.x = GET_X_LPARAM(lp); g_lastPt.y = GET_Y_LPARAM(lp);
        SetCapture(hwnd);
        return 0;

    case WM_MOUSEMOVE:
        if (g_dragging) {
            int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
            g_rend.Pan((float)(x - g_lastPt.x), (float)(y - g_lastPt.y));
            g_lastPt.x = x; g_lastPt.y = y;
        }
        return 0;
    case WM_LBUTTONUP:
        g_dragging = false;
        ReleaseCapture();
        return 0;

    case WM_PAINT: {
        PAINTSTRUCT ps; BeginPaint(hwnd, &ps);
        if (g_rend.swap) g_rend.Draw(g_cap.ctx.Get());
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_DESTROY:
        g_overlay = nullptr; g_active = false;
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT CALLBACK MsgWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_HOTKEY:
        if (wp == HOTKEY_START && !g_active) {
            if (!g_cap.CaptureFrame(g_frame)) {
                MessageBoxW(nullptr, L"Capture failed", L"ERROR", MB_ICONERROR);
                return 0;
            }
            ShowOverlay();   // create window
            if (!g_rend.Init(g_overlay, g_cap.dev.Get(), g_cap.width, g_cap.height)) {
                MessageBoxW(nullptr, L"Render failed to init", L"ERROR", MB_ICONERROR);
                HideOverlay(); return 0;
            }
            g_rend.clamp = g_clampPan;
            g_rend.SetFrame(g_cap.dev.Get(), g_frame.Get());
            g_rend.Draw(g_cap.ctx.Get());
        }
        return 0;
    case WM_TRAY:
        if (LOWORD(lp) == WM_RBUTTONUP || LOWORD(lp) == WM_CONTEXTMENU)
            ShowTrayMenu(hwnd);
        return 0;
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDM_ABOUT:
            MessageBoxW(nullptr, L"BoomerWin v0.1\n",
                L"About", MB_OK | MB_ICONINFORMATION);
            return 0;
        case IDM_QUIT:
            DestroyWindow(hwnd);   // -> WM_DESTROY -> PostQuitMessage
            return 0;
        case IDM_CLAMP:
            g_clampPan = !g_clampPan;
            g_rend.clamp = g_clampPan;     // sync to render
            return 0;
        }
        case IDM_SETTINGS:
            OpenSettings(GetModuleHandleW(nullptr));
            return 0;
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static bool SaveTextureToPng(ID3D11Device* dev, ID3D11DeviceContext* ctx,
    ID3D11Texture2D* tex, const wchar_t* path) {
    D3D11_TEXTURE2D_DESC desc; tex->GetDesc(&desc);

    D3D11_TEXTURE2D_DESC sd = desc;
    sd.Usage = D3D11_USAGE_STAGING; sd.BindFlags = 0;
    sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ; sd.MiscFlags = 0;
    ComPtr<ID3D11Texture2D> staging;
    if (FAILED(dev->CreateTexture2D(&sd, nullptr, &staging))) return false;
    ctx->CopyResource(staging.Get(), tex);

    D3D11_MAPPED_SUBRESOURCE map;
    if (FAILED(ctx->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &map))) return false;

    ComPtr<IWICImagingFactory> factory;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr,
        CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)))) {
        ctx->Unmap(staging.Get(), 0); return false;
    }

    ComPtr<IWICBitmap> bmp;
    factory->CreateBitmapFromMemory(desc.Width, desc.Height,
        GUID_WICPixelFormat32bppBGRA, map.RowPitch, map.RowPitch * desc.Height,
        (BYTE*)map.pData, &bmp);
    ctx->Unmap(staging.Get(), 0);

    ComPtr<IWICStream> stream; factory->CreateStream(&stream);
    if (FAILED(stream->InitializeFromFilename(path, GENERIC_WRITE))) return false;

    ComPtr<IWICBitmapEncoder> enc; factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &enc);
    enc->Initialize(stream.Get(), WICBitmapEncoderNoCache);
    ComPtr<IWICBitmapFrameEncode> fr; enc->CreateNewFrame(&fr, nullptr);
    fr->Initialize(nullptr);
    fr->SetSize(desc.Width, desc.Height);
    WICPixelFormatGUID fmt = GUID_WICPixelFormat32bppBGRA;
    fr->SetPixelFormat(&fmt);
    fr->WriteSource(bmp.Get(), nullptr);
    fr->Commit();
    enc->Commit();
    return true;
}

static void ShowTrayMenu(HWND hwnd) {
    POINT pt; GetCursorPos(&pt);
    wchar_t k[64], line[128];
    HMENU m = CreatePopupMenu();

    KeyName(g_cfg.keyCapture, k, 64);
    wsprintfW(line, L"Freeze£ºCtrl+Alt+%s", k);
    AppendMenuW(m, MF_STRING | MF_GRAYED, 0, line);
    KeyName(g_cfg.keyExit, k, 64);
    wsprintfW(line, L"Release£º%s", k);
    AppendMenuW(m, MF_STRING | MF_GRAYED, 0, line);

    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING | (g_clampPan ? MF_CHECKED : 0), IDM_CLAMP, L"Clamp");
    AppendMenuW(m, MF_STRING, IDM_SETTINGS, L"Shortcuts...");
    AppendMenuW(m, MF_STRING, IDM_ABOUT, L"About");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, IDM_QUIT, L"Quit");

    SetForegroundWindow(hwnd);
    TrackPopupMenu(m, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, nullptr);
    PostMessage(hwnd, WM_NULL, 0, 0);
    DestroyMenu(m);
}

static void ShowOverlay() {
    g_overlay = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW, L"BoomerWinOverlay", L"", WS_POPUP,
        g_cap.originX, g_cap.originY, (int)g_cap.width, (int)g_cap.height,
        nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    ShowWindow(g_overlay, SW_SHOW);
    SetForegroundWindow(g_overlay);
    SetFocus(g_overlay);
    g_active = true;
}

static void HideOverlay() {
    if (g_overlay) DestroyWindow(g_overlay);
    g_overlay = nullptr; g_active = false;
    g_rend = Renderer{};
    g_frame.Reset();
}

static void SaveConfig() {
    HKEY h;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\WinBoomer", 0, nullptr,
        0, KEY_WRITE, nullptr, &h, nullptr) == ERROR_SUCCESS) {
        RegSetValueExW(h, L"KeyCapture", 0, REG_DWORD, (BYTE*)&g_cfg.keyCapture, 4);
        RegSetValueExW(h, L"KeyExit", 0, REG_DWORD, (BYTE*)&g_cfg.keyExit, 4);
        RegSetValueExW(h, L"KeyReset", 0, REG_DWORD, (BYTE*)&g_cfg.keyReset, 4);
        DWORD clamp = g_clampPan ? 1 : 0;
        RegSetValueExW(h, L"ClampPan", 0, REG_DWORD, (BYTE*)&clamp, 4);
        RegCloseKey(h);
    }
}

static void LoadConfig() {
    HKEY h;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\WinBoomer", 0, KEY_READ, &h)
        == ERROR_SUCCESS) {
        DWORD v, cb = 4;
        if (RegQueryValueExW(h, L"KeyCapture", 0, 0, (BYTE*)&v, &cb) == ERROR_SUCCESS) g_cfg.keyCapture = v;
        cb = 4; if (RegQueryValueExW(h, L"KeyExit", 0, 0, (BYTE*)&v, &cb) == ERROR_SUCCESS) g_cfg.keyExit = v;
        cb = 4; if (RegQueryValueExW(h, L"KeyReset", 0, 0, (BYTE*)&v, &cb) == ERROR_SUCCESS) g_cfg.keyReset = v;
        cb = 4; if (RegQueryValueExW(h, L"ClampPan", 0, 0, (BYTE*)&v, &cb) == ERROR_SUCCESS) { g_clampPan = (v != 0); }
        RegCloseKey(h);
    }
}

/*
   Hot Keys
*/
static Config g_tmpCfg;
static HWND   g_hCap, g_hExit, g_hReset;
static void RefreshDlgButtons() {
    wchar_t s[64], label[96];
    KeyName(g_tmpCfg.keyCapture, s, 64);
    wsprintfW(label, L"Capture: Ctrl+Alt+%s", s); SetWindowTextW(g_hCap, label);
    KeyName(g_tmpCfg.keyExit, s, 64);
    wsprintfW(label, L"Release: %s", s);       SetWindowTextW(g_hExit, label);
    KeyName(g_tmpCfg.keyReset, s, 64);
    wsprintfW(label, L"Reset: %s", s);           SetWindowTextW(g_hReset, label);
}

LRESULT CALLBACK SettingsProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: {
        HINSTANCE hi = ((LPCREATESTRUCT)lp)->hInstance;
        CreateWindowW(L"STATIC", L"Click on the button, then press the key", WS_CHILD | WS_VISIBLE,
            20, 12, 320, 20, hwnd, 0, hi, 0);
        g_hCap = CreateWindowW(L"BUTTON", L"", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            20, 40, 320, 30, hwnd, (HMENU)IDC_CAP, hi, 0);
        g_hExit = CreateWindowW(L"BUTTON", L"", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            20, 78, 320, 30, hwnd, (HMENU)IDC_EXIT, hi, 0);
        g_hReset = CreateWindowW(L"BUTTON", L"", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            20, 116, 320, 30, hwnd, (HMENU)IDC_RESET, hi, 0);
        CreateWindowW(L"BUTTON", L"OK", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
            160, 160, 80, 28, hwnd, (HMENU)IDC_OK, hi, 0);
        CreateWindowW(L"BUTTON", L"Cancel", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            260, 160, 80, 28, hwnd, (HMENU)IDC_CANCEL, hi, 0);
        RefreshDlgButtons();
        return 0;
    }
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDC_CAP:   g_capturing = IDC_CAP;   SetWindowTextW(g_hCap, L"New key..."); SetFocus(hwnd); return 0;
        case IDC_EXIT:  g_capturing = IDC_EXIT;  SetWindowTextW(g_hExit, L"New key..."); SetFocus(hwnd); return 0;
        case IDC_RESET: g_capturing = IDC_RESET; SetWindowTextW(g_hReset, L"New key..."); SetFocus(hwnd); return 0;
        case IDC_OK:
            g_cfg = g_tmpCfg;
            UnregisterHotKey(g_msgWnd, HOTKEY_START);
            if (!RegisterHotKey(g_msgWnd, HOTKEY_START,
                MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, g_cfg.keyCapture)) {
                MessageBoxW(hwnd, L"Failed to bind",
                    L"Info", MB_ICONWARNING);
                RegisterHotKey(g_msgWnd, HOTKEY_START,
                    MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, g_cfg.keyCapture);
            }
            SaveConfig();
            DestroyWindow(hwnd);
            return 0;
        case IDC_CANCEL:DestroyWindow(hwnd); g_settingsResult = 0; return 0;
        }
        return 0;
    case WM_KEYDOWN:
        if (g_capturing) {
            UINT vk = (UINT)wp;
            if (vk != VK_LBUTTON && vk != VK_RBUTTON) {   // ignore mouse btn
                if (g_capturing == IDC_CAP)   g_tmpCfg.keyCapture = vk;
                if (g_capturing == IDC_EXIT)  g_tmpCfg.keyExit = vk;
                if (g_capturing == IDC_RESET) g_tmpCfg.keyReset = vk;
            }
            g_capturing = 0;
            RefreshDlgButtons();
        }
        return 0;
    case WM_CLOSE:
        DestroyWindow(hwnd); g_settingsResult = 0; return 0;
    case WM_DESTROY:
        g_settingsOpen = false; return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static void KeyName(UINT vk, wchar_t* buf, int cb) {
    UINT sc = MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
    switch (vk) {
    case VK_LEFT: case VK_RIGHT: case VK_UP: case VK_DOWN:
    case VK_HOME: case VK_END: case VK_PRIOR: case VK_NEXT:
    case VK_INSERT: case VK_DELETE:
        sc |= 0x100; break;
    }
    if (!GetKeyNameTextW(sc << 16, buf, cb) || buf[0] == 0)
        lstrcpynW(buf, L"?", cb);
}

static void OpenSettings(HINSTANCE hi) {
    if (g_settingsOpen) { SetForegroundWindow(g_settingsWnd); return; }  // ignore opened twice
    g_tmpCfg = g_cfg;
    g_settingsOpen = true;

    static bool reg = false;
    if (!reg) {
        WNDCLASSW wc = {};
        wc.lpfnWndProc = SettingsProc; wc.hInstance = hi;
        wc.lpszClassName = L"BoomerWinSettings";
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        RegisterClassW(&wc);
        reg = true;
    }
    g_settingsWnd = CreateWindowExW(WS_EX_TOPMOST, L"BoomerWinSettings", L"Shortcut Settings",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
        CW_USEDEFAULT, CW_USEDEFAULT, 380, 240,
        nullptr, nullptr, hi, nullptr);
    ShowWindow(g_settingsWnd, SW_SHOW);
}


int APIENTRY wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    WNDCLASSW mc = {};
    mc.lpfnWndProc = MsgWndProc; mc.hInstance = hInst; mc.lpszClassName = L"BoomerWinMsg";
    RegisterClassW(&mc);
    g_msgWnd = CreateWindowExW(0, L"BoomerWinMsg", L"", 0, 0, 0, 0, 0,
        HWND_MESSAGE, nullptr, hInst, nullptr);

    WNDCLASSW oc = {};
    oc.lpfnWndProc = OverlayProc; oc.hInstance = hInst;
    oc.lpszClassName = L"BoomerWinOverlay"; oc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    RegisterClassW(&oc);

    RegisterHotKey(g_msgWnd, HOTKEY_START,MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, g_cfg.keyCapture);

    g_nid.cbSize = sizeof(g_nid); g_nid.hWnd = g_msgWnd; g_nid.uID = 1;
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAY;
    g_nid.hIcon = LoadIcon(hInst, MAKEINTRESOURCE(IDI_ICON1));
    lstrcpyW(g_nid.szTip, L"BoomerWin");
    Shell_NotifyIconW(NIM_ADD, &g_nid);

    if (!g_cap.InitDevice()) {
        MessageBoxW(nullptr, L"D3D Failed to initialize", L"ERROR", MB_ICONERROR);
    }

    MSG msg = {};
    while (msg.message != WM_QUIT) {
        if (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        else if (g_active && g_rend.swap && g_rend.NeedsRedraw()) {
            g_rend.Animate();
            g_rend.Draw(g_cap.ctx.Get());
        }
        else {
            WaitMessage();
        }
    }

    Shell_NotifyIconW(NIM_DELETE, &g_nid);
    UnregisterHotKey(g_msgWnd, HOTKEY_START);
    return 0;
}
