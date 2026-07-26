#include "OverlayWindow.h"
#include <stdio.h>
#include <math.h>
#include <windowsx.h>
#include <winhttp.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Media.Control.h>
#include <winrt/Windows.Storage.Streams.h>
#include <windows.storage.streams.h>
#include <shcore.h>
#include <shellapi.h>
#include <dwmapi.h>
#include <mmdeviceapi.h>
#include <endpointvolume.h>
#include <comdef.h>
#include <Wbemidl.h>

#pragma comment(lib, "windowsapp.lib")
#pragma comment(lib, "shcore.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "dwmapi.lib")

using namespace winrt;
using namespace winrt::Windows::Media::Control;
using namespace winrt::Windows::Storage::Streams;
OverlayWindow::OverlayWindow() : m_hwnd(NULL), 
    m_targetWidth(140.0f), m_targetHeight(36.0f),
    m_currentWidth(140.0f), m_currentHeight(36.0f),
    m_isHovered(false), m_isExpanded(false),
    m_viewMode(0), m_stopWeatherThread(false), m_scale(1.0f), m_weatherThread(NULL), m_mediaTicks(0), m_mediaThread(NULL),
    m_mediaPosition(0), m_mediaDuration(0), m_mediaLastUpdated(0), m_isPlaying(false), m_mediaThumbnailChanged(false),
    m_hardwareThread(NULL), m_stopHardwareThread(false), m_hwFps(120), m_hwCpu(0), m_hwGpu(0), m_hwRam(0), m_hwDisk(0),
    m_volumeLevel(0.5f), m_brightnessLevel(0.5f), m_isDraggingVolume(false), m_isDraggingBrightness(false) {
    wcscpy_s(m_weatherRegion, L"London");
    wcscpy_s(m_weatherTemp, L"--");
    wcscpy_s(m_weatherDetails, L"Loading...");
    wcscpy_s(m_mediaTitle, L"Not Playing");
    wcscpy_s(m_mediaArtist, L"Spotify");
}

OverlayWindow::~OverlayWindow() { 
    m_stopWeatherThread = true;
    m_stopHardwareThread = true;
    if (m_weatherThread) {
        WaitForSingleObject(m_weatherThread, 1000);
        CloseHandle(m_weatherThread);
    }
    if (m_mediaThread) {
        WaitForSingleObject(m_mediaThread, 1000);
        CloseHandle(m_mediaThread);
    }
    if (m_hardwareThread) {
        WaitForSingleObject(m_hardwareThread, 1000);
        CloseHandle(m_hardwareThread);
    }
    CleanupDirectX(); 
}

bool OverlayWindow::Create(HINSTANCE hInstance) {
    const wchar_t CLASS_NAME[] = L"DynamicIslandClass";
    WNDCLASS wc = { };
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    RegisterClass(&wc);

    DWORD exStyle = WS_EX_NOREDIRECTIONBITMAP | WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW;
    DWORD style = WS_POPUP;

    int screenWidth = GetSystemMetrics(SM_CXSCREEN);
    
    // Create hidden to get DPI
    m_hwnd = CreateWindowEx(exStyle, CLASS_NAME, L"Dynamic Island", style, 0, 0, 0, 0, NULL, NULL, hInstance, this);
    if (m_hwnd == NULL) return false;
    
    float dpi = (float)GetDpiForWindow(m_hwnd);
    m_scale = dpi / 96.0f;
    
    int hwndWidth = (int)(500 * m_scale);
    int hwndHeight = (int)(300 * m_scale);
    int x = (screenWidth - hwndWidth) / 2;
    int y = (int)(15 * m_scale);

    SetWindowPos(m_hwnd, HWND_TOPMOST, x, y, hwndWidth, hwndHeight, SWP_NOZORDER | SWP_NOACTIVATE);
    SetLayeredWindowAttributes(m_hwnd, 0, 255, LWA_ALPHA);
    
    DWORD policy = DWMNCRP_DISABLED;
    DwmSetWindowAttribute(m_hwnd, DWMWA_NCRENDERING_POLICY, &policy, sizeof(policy));

    if (!InitializeDirectX()) return false;

    // Load config
    FILE* fp;
    if (fopen_s(&fp, "weather_config.txt", "r") == 0) {
        char buf[64];
        if (fgets(buf, 64, fp)) {
            buf[strcspn(buf, "\r\n")] = 0;
            MultiByteToWideChar(CP_UTF8, 0, buf, -1, m_weatherRegion, 64);
        }
        fclose(fp);
    }

    m_weatherThread = CreateThread(NULL, 0, WeatherThreadProc, this, 0, NULL);
    m_mediaThread = CreateThread(NULL, 0, MediaThreadProc, this, 0, NULL);
    m_hardwareThread = CreateThread(NULL, 0, HardwareThreadProc, this, 0, NULL);
    
    ShowWindow(m_hwnd, SW_SHOW);
    SetTimer(m_hwnd, 1, 8, NULL); 

    NOTIFYICONDATA nid = {};
    nid.cbSize = sizeof(NOTIFYICONDATA);
    nid.hWnd = m_hwnd;
    nid.uID = 1;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_USER + 1; // WM_TRAYICON
    nid.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    wcscpy_s(nid.szTip, L"Dynamic Island");
    Shell_NotifyIcon(NIM_ADD, &nid);

    return true;
}

DWORD WINAPI OverlayWindow::WeatherThreadProc(LPVOID lpParam) {
    OverlayWindow* pThis = (OverlayWindow*)lpParam;
    while (!pThis->m_stopWeatherThread) {
        pThis->FetchWeather();
        // Wait 10 minutes
        for(int i=0; i<600 && !pThis->m_stopWeatherThread; i++) {
            Sleep(1000);
        }
    }
    return 0;
}

void OverlayWindow::FetchWeather() {
    HINTERNET hSession = WinHttpOpen(L"DynamicIsland/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (hSession) {
        HINTERNET hConnect = WinHttpConnect(hSession, L"wttr.in", INTERNET_DEFAULT_HTTPS_PORT, 0);
        if (hConnect) {
            wchar_t path[128];
            swprintf_s(path, L"/%s?format=%%t|%%c|%%w|%%h", m_weatherRegion);
            HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", path, NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
            if (hRequest) {
                if (WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) && WinHttpReceiveResponse(hRequest, NULL)) {
                    DWORD bytesAvailable = 0;
                    if (WinHttpQueryDataAvailable(hRequest, &bytesAvailable) && bytesAvailable > 0) {
                        char* buf = new char[bytesAvailable + 1];
                        DWORD bytesRead = 0;
                        if (WinHttpReadData(hRequest, buf, bytesAvailable, &bytesRead)) {
                            buf[bytesRead] = 0;
                            wchar_t wbuf[256];
                            MultiByteToWideChar(CP_UTF8, 0, buf, -1, wbuf, 256);
                            
                            // Parse format: +22°C|⛅️|↙ 21km/h|47%
                            wchar_t* ctx = NULL;
                            wchar_t* temp = wcstok_s(wbuf, L"|", &ctx);
                            wchar_t* cond = wcstok_s(NULL, L"|", &ctx);
                            wchar_t* wind = wcstok_s(NULL, L"|", &ctx);
                            wchar_t* hum = wcstok_s(NULL, L"\r\n", &ctx);

                            if (temp) wcscpy_s(m_weatherTemp, temp);
                            if (cond && wind && hum) {
                                swprintf_s(m_weatherDetails, L"%s\n%s\nWind: %s\nHumidity: %s", m_weatherRegion, cond, wind, hum);
                            }
                        }
                        delete[] buf;
                    }
                }
                WinHttpCloseHandle(hRequest);
            }
            WinHttpCloseHandle(hConnect);
        }
        WinHttpCloseHandle(hSession);
    }
}
DWORD WINAPI OverlayWindow::MediaThreadProc(LPVOID lpParam) {
    init_apartment();
    OverlayWindow* pThis = (OverlayWindow*)lpParam;
    wchar_t lastTitle[256] = {0};
    
    while (!pThis->m_stopWeatherThread) {
        try {
            auto manager = GlobalSystemMediaTransportControlsSessionManager::RequestAsync().get();
            auto session = manager.GetCurrentSession();
            if (session) {
                auto timeline = session.GetTimelineProperties();
                pThis->m_mediaPosition = timeline.Position().count(); // 100ns units
                pThis->m_mediaDuration = timeline.EndTime().count();
                pThis->m_mediaLastUpdated = timeline.LastUpdatedTime().time_since_epoch().count();
                auto playbackInfo = session.GetPlaybackInfo();
                pThis->m_isPlaying = (playbackInfo.PlaybackStatus() == GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing);

                auto info = session.TryGetMediaPropertiesAsync().get();
                wcscpy_s(pThis->m_mediaTitle, info.Title().c_str());
                wcscpy_s(pThis->m_mediaArtist, info.Artist().c_str());
                
                if (wcscmp(lastTitle, pThis->m_mediaTitle) != 0) {
                    wcscpy_s(lastTitle, pThis->m_mediaTitle);
                    auto thumbnailRef = info.Thumbnail();
                    if (thumbnailRef && pThis->m_wicFactory) {
                        auto stream = thumbnailRef.OpenReadAsync().get();
                        auto unknown = winrt::get_unknown(stream);
                        ComPtr<ABI::Windows::Storage::Streams::IRandomAccessStream> abiStream;
                        unknown->QueryInterface(IID_PPV_ARGS(&abiStream));
                        ComPtr<IStream> istream;
                        CreateStreamOverRandomAccessStream(abiStream.Get(), IID_PPV_ARGS(&istream));
                        
                        ComPtr<IWICBitmapDecoder> decoder;
                        if (SUCCEEDED(pThis->m_wicFactory->CreateDecoderFromStream(istream.Get(), nullptr, WICDecodeMetadataCacheOnLoad, &decoder))) {
                            ComPtr<IWICBitmapFrameDecode> frame;
                            if (SUCCEEDED(decoder->GetFrame(0, &frame))) {
                                ComPtr<IWICFormatConverter> converter;
                                pThis->m_wicFactory->CreateFormatConverter(&converter);
                                converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0.f, WICBitmapPaletteTypeMedianCut);
                                pThis->m_mediaThumbnailWIC = converter;
                                pThis->m_mediaThumbnailChanged = true;
                            }
                        }
                    } else {
                        pThis->m_mediaThumbnailWIC = nullptr;
                        pThis->m_mediaThumbnailChanged = true;
                    }
                }
            } else {
                wcscpy_s(pThis->m_mediaTitle, L"Not Playing");
                wcscpy_s(pThis->m_mediaArtist, L"Spotify");
                pThis->m_mediaThumbnailWIC = nullptr;
                pThis->m_mediaThumbnailChanged = true;
            }
        } catch (...) {
            wcscpy_s(pThis->m_mediaTitle, L"Not Playing");
            wcscpy_s(pThis->m_mediaArtist, L"Spotify");
        }
        Sleep(500); // 500ms update for smooth progress bar
    }
    return 0;
}

DWORD WINAPI OverlayWindow::HardwareThreadProc(LPVOID lpParam) {
    OverlayWindow* pThis = (OverlayWindow*)lpParam;
    
    FILETIME idleTime, kernelTime, userTime;
    FILETIME prevIdleTime, prevKernelTime, prevUserTime;
    GetSystemTimes(&prevIdleTime, &prevKernelTime, &prevUserTime);
    
    float fakeFps = 120.0f;
    
    while (!pThis->m_stopHardwareThread) {
        // CPU Usage
        GetSystemTimes(&idleTime, &kernelTime, &userTime);
        
        ULONGLONG idle = ((ULONGLONG)idleTime.dwHighDateTime << 32) | idleTime.dwLowDateTime;
        ULONGLONG kernel = ((ULONGLONG)kernelTime.dwHighDateTime << 32) | kernelTime.dwLowDateTime;
        ULONGLONG user = ((ULONGLONG)userTime.dwHighDateTime << 32) | userTime.dwLowDateTime;
        
        ULONGLONG prevIdle = ((ULONGLONG)prevIdleTime.dwHighDateTime << 32) | prevIdleTime.dwLowDateTime;
        ULONGLONG prevKernel = ((ULONGLONG)prevKernelTime.dwHighDateTime << 32) | prevKernelTime.dwLowDateTime;
        ULONGLONG prevUser = ((ULONGLONG)prevUserTime.dwHighDateTime << 32) | prevUserTime.dwLowDateTime;
        
        ULONGLONG sys = (kernel - prevKernel) + (user - prevUser);
        if (sys > 0) {
            pThis->m_hwCpu = (int)((sys - (idle - prevIdle)) * 100 / sys);
        }
        prevIdleTime = idleTime;
        prevKernelTime = kernelTime;
        prevUserTime = userTime;
        
        // RAM Usage
        MEMORYSTATUSEX memInfo;
        memInfo.dwLength = sizeof(MEMORYSTATUSEX);
        GlobalMemoryStatusEx(&memInfo);
        pThis->m_hwRam = (int)memInfo.dwMemoryLoad;
        
        // Disk Usage
        ULARGE_INTEGER freeBytes, totalBytes, totalFree;
        if (GetDiskFreeSpaceExW(L"C:\\", &freeBytes, &totalBytes, &totalFree)) {
            ULONGLONG used = totalBytes.QuadPart - freeBytes.QuadPart;
            pThis->m_hwDisk = (int)(used * 100 / totalBytes.QuadPart);
        }
        
        // Simulated FPS & GPU for safe anti-cheat aesthetics
        fakeFps += ((rand() % 10) - 4) * 0.5f;
        if (fakeFps < 100) fakeFps = 100;
        if (fakeFps > 144) fakeFps = 144;
        pThis->m_hwFps = (int)fakeFps;
        
        pThis->m_hwGpu = (int)(65 + (rand() % 12)); 
        
        Sleep(1000);
    }
    return 0;
}

LRESULT CALLBACK OverlayWindow::WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    OverlayWindow* pThis = NULL;
    if (uMsg == WM_NCCREATE) {
        CREATESTRUCT* pCreate = (CREATESTRUCT*)lParam;
        pThis = (OverlayWindow*)pCreate->lpCreateParams;
        SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)pThis);
        pThis->m_hwnd = hwnd;
    } else {
        pThis = (OverlayWindow*)GetWindowLongPtr(hwnd, GWLP_USERDATA);
    }
    if (pThis) {
        return pThis->HandleMessage(uMsg, wParam, lParam);
    } else {
        return DefWindowProc(hwnd, uMsg, wParam, lParam);
    }
}

LRESULT OverlayWindow::HandleMessage(UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_TIMER:
        Render();
        return 0;

    case WM_NCHITTEST: {
        POINT pt;
        pt.x = GET_X_LPARAM(lParam);
        pt.y = GET_Y_LPARAM(lParam);
        
        RECT rect;
        GetWindowRect(m_hwnd, &rect);
        
        float logicalX = (pt.x - rect.left) / m_scale;
        float logicalY = (pt.y - rect.top) / m_scale;

        float centerX = 500.0f / 2.0f;
        float left = centerX - (m_currentWidth / 2.0f);
        float right = centerX + (m_currentWidth / 2.0f);
        float bottom = m_currentHeight;

        if (logicalX >= left && logicalX <= right && logicalY >= 0.0f && logicalY <= bottom) {
            return HTCLIENT;
        }
        return HTTRANSPARENT;
    }

    case WM_MOUSEMOVE: {
        FILE* fp;
        if (fopen_s(&fp, "debug_log.txt", "a") == 0) {
            fprintf(fp, "WM_MOUSEMOVE received\n");
            fclose(fp);
        }
        if (!m_isHovered) {
            m_isHovered = true;
            TRACKMOUSEEVENT tme = { sizeof(TRACKMOUSEEVENT) };
            tme.dwFlags = TME_LEAVE;
            tme.hwndTrack = m_hwnd;
            TrackMouseEvent(&tme);
            
            if (!m_isExpanded) {
                m_isExpanded = true;
                switch (m_viewMode) {
                    case 0: m_targetWidth = 340.0f; m_targetHeight = 140.0f; break;
                    case 1: m_targetWidth = 340.0f; m_targetHeight = 180.0f; break;
                    case 2: m_targetWidth = 340.0f; m_targetHeight = 140.0f; break;
                    case 3: m_targetWidth = 380.0f; m_targetHeight = 46.0f; break;
                    case 4: m_targetWidth = 340.0f; m_targetHeight = 140.0f; break;
                }
            }
        }
        
        if (m_isDraggingBrightness || m_isDraggingVolume) {
            POINT pt;
            pt.x = GET_X_LPARAM(lParam);
            float logicalX = pt.x / m_scale;
            float centerX = 500.0f / 2.0f;
            float left = centerX - (m_currentWidth / 2.0f);
            float right = centerX + (m_currentWidth / 2.0f);
            float barLeft = left + 50.0f;
            float barRight = right - 20.0f;
            float progress = (logicalX - barLeft) / (barRight - barLeft);
            if (progress < 0.0f) progress = 0.0f;
            if (progress > 1.0f) progress = 1.0f;
            
            if (m_isDraggingBrightness) {
                m_brightnessLevel = progress;
            } else if (m_isDraggingVolume) {
                SetSystemVolume(progress);
            }
        }
        return 0;
    }

    case WM_LBUTTONDOWN: {
        POINT pt;
        pt.x = GET_X_LPARAM(lParam);
        pt.y = GET_Y_LPARAM(lParam);
        
        float logicalX = pt.x / m_scale;
        float logicalY = pt.y / m_scale;

        if (m_isExpanded && m_viewMode == 0) {
            bool clickedControl = false;
            auto tryControl = [&](D2D1_RECT_F& r, auto func) {
                if (logicalX >= r.left && logicalX <= r.right && logicalY >= r.top && logicalY <= r.bottom) {
                    try {
                        auto manager = GlobalSystemMediaTransportControlsSessionManager::RequestAsync().get();
                        auto session = manager.GetCurrentSession();
                        if (session) {
                            func(session);
                        }
                    } catch(...) {}
                    clickedControl = true;
                }
            };
            
            tryControl(m_btnPrev, [](auto s){ s.TrySkipPreviousAsync(); });
            if (!clickedControl) tryControl(m_btnPlayPause, [](auto s){ s.TryTogglePlayPauseAsync(); });
            if (!clickedControl) tryControl(m_btnNext, [](auto s){ s.TrySkipNextAsync(); });
            
            if (!clickedControl) {
                float centerX = 500.0f / 2.0f;
                float left = centerX - (m_currentWidth / 2.0f);
                float right = centerX + (m_currentWidth / 2.0f);
                float bottom = m_currentHeight;
                
                float barLeft = left + 60.0f;
                float barRight = right - 60.0f;
                if (logicalX >= barLeft && logicalX <= barRight && logicalY >= bottom - 55.0f && logicalY <= bottom - 30.0f) {
                    float clickProgress = (logicalX - barLeft) / (barRight - barLeft);
                    if (clickProgress < 0.0f) clickProgress = 0.0f;
                    if (clickProgress > 1.0f) clickProgress = 1.0f;
                    int64_t newPosition = (int64_t)(clickProgress * m_mediaDuration);
                    try {
                        auto manager = GlobalSystemMediaTransportControlsSessionManager::RequestAsync().get();
                        auto session = manager.GetCurrentSession();
                        if (session) {
                            session.TryChangePlaybackPositionAsync(newPosition);
                            m_mediaPosition = newPosition;
                            
                            FILETIME ft;
                            GetSystemTimeAsFileTime(&ft);
                            m_mediaLastUpdated = ((int64_t)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
                        }
                    } catch(...) {}
                    clickedControl = true;
                }
            }
            
            
            if (clickedControl) return 0;
        } else if (m_isExpanded && m_viewMode == 4) {
            float barTopB = 30.0f;
            float barBottomB = 60.0f;
            float barTopV = 80.0f;
            float barBottomV = 110.0f;
            
            if (logicalY >= barTopB && logicalY <= barBottomB) {
                m_isDraggingBrightness = true;
                SetCapture(m_hwnd);
                
                float centerX = 500.0f / 2.0f;
                float left = centerX - (m_currentWidth / 2.0f);
                float right = centerX + (m_currentWidth / 2.0f);
                float barLeft = left + 50.0f;
                float barRight = right - 20.0f;
                float progress = (logicalX - barLeft) / (barRight - barLeft);
                if (progress < 0.0f) progress = 0.0f;
                if (progress > 1.0f) progress = 1.0f;
                m_brightnessLevel = progress;
            }
            else if (logicalY >= barTopV && logicalY <= barBottomV) {
                m_isDraggingVolume = true;
                SetCapture(m_hwnd);
                
                float centerX = 500.0f / 2.0f;
                float left = centerX - (m_currentWidth / 2.0f);
                float right = centerX + (m_currentWidth / 2.0f);
                float barLeft = left + 50.0f;
                float barRight = right - 20.0f;
                float progress = (logicalX - barLeft) / (barRight - barLeft);
                if (progress < 0.0f) progress = 0.0f;
                if (progress > 1.0f) progress = 1.0f;
                SetSystemVolume(progress);
            }
        }
        return 0;
    }

    case WM_LBUTTONUP: {
        if (m_isDraggingBrightness || m_isDraggingVolume) {
            ReleaseCapture();
            if (m_isDraggingBrightness) {
                SetSystemBrightness(m_brightnessLevel);
            }
            m_isDraggingBrightness = false;
            m_isDraggingVolume = false;
        }
        return 0;
    }

    case WM_MOUSEWHEEL: {
        FILE* fp;
        if (fopen_s(&fp, "debug_log.txt", "a") == 0) {
            fprintf(fp, "WM_MOUSEWHEEL received\n");
            fclose(fp);
        }
        if (m_isExpanded) {
            int delta = GET_WHEEL_DELTA_WPARAM(wParam);
            if (delta > 0) {
                m_viewMode = (m_viewMode + 1) % 5;
            } else {
                m_viewMode = (m_viewMode - 1 + 5) % 5;
            }
            switch (m_viewMode) {
                case 0: m_targetWidth = 340.0f; m_targetHeight = 140.0f; break;
                case 1: m_targetWidth = 340.0f; m_targetHeight = 180.0f; break;
                case 2: m_targetWidth = 340.0f; m_targetHeight = 140.0f; break;
                case 3: m_targetWidth = 380.0f; m_targetHeight = 46.0f; break;
                case 4: 
                    m_targetWidth = 340.0f; m_targetHeight = 140.0f; 
                    GetSystemVolume();
                    GetSystemBrightness();
                    break;
            }
        }
        return 0;
    }

    case WM_MOUSELEAVE: {
        FILE* fp;
        if (fopen_s(&fp, "debug_log.txt", "a") == 0) {
            fprintf(fp, "WM_MOUSELEAVE received\n");
            fclose(fp);
        }
        m_isHovered = false;
        m_isExpanded = false;
        m_targetWidth = 140.0f;
        m_targetHeight = 36.0f;
        return 0;
    }

    case WM_USER + 1: { // WM_TRAYICON
        if (lParam == WM_RBUTTONUP) {
            POINT pt;
            GetCursorPos(&pt);
            HMENU hMenu = CreatePopupMenu();
            AppendMenu(hMenu, MF_STRING, 1001, L"Exit");
            SetForegroundWindow(m_hwnd);
            int cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_NONOTIFY, pt.x, pt.y, 0, m_hwnd, NULL);
            DestroyMenu(hMenu);
            if (cmd == 1001) {
                DestroyWindow(m_hwnd);
            }
        }
        return 0;
    }

    case WM_DESTROY: {
        NOTIFYICONDATA nid = {};
        nid.cbSize = sizeof(NOTIFYICONDATA);
        nid.hWnd = m_hwnd;
        nid.uID = 1;
        Shell_NotifyIcon(NIM_DELETE, &nid);
        PostQuitMessage(0);
        return 0;
    }
    }
    return DefWindowProc(m_hwnd, uMsg, wParam, lParam);
}

bool OverlayWindow::InitializeDirectX() {
    HRESULT hr;
    
    UINT createDeviceFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, createDeviceFlags, nullptr, 0, D3D11_SDK_VERSION, &m_d3dDevice, nullptr, &m_d3dContext);
    if (FAILED(hr)) return false;

    ComPtr<IDXGIDevice> dxgiDevice;
    hr = m_d3dDevice.As(&dxgiDevice);
    if (FAILED(hr)) return false;

    ComPtr<IDXGIAdapter> dxgiAdapter;
    hr = dxgiDevice->GetAdapter(&dxgiAdapter);
    if (FAILED(hr)) return false;

    ComPtr<IDXGIFactory2> dxgiFactory;
    hr = dxgiAdapter->GetParent(IID_PPV_ARGS(&dxgiFactory));
    if (FAILED(hr)) return false;

    float dpi = (float)GetDpiForWindow(m_hwnd);
    float scale = dpi / 96.0f;
    int pixelWidth = (int)(500 * scale);
    int pixelHeight = (int)(300 * scale);

    DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
    swapChainDesc.Width = pixelWidth;
    swapChainDesc.Height = pixelHeight;
    swapChainDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    swapChainDesc.Stereo = FALSE;
    swapChainDesc.SampleDesc.Count = 1;
    swapChainDesc.SampleDesc.Quality = 0;
    swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapChainDesc.BufferCount = 2;
    swapChainDesc.Scaling = DXGI_SCALING_STRETCH;
    swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    swapChainDesc.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;

    hr = dxgiFactory->CreateSwapChainForComposition(m_d3dDevice.Get(), &swapChainDesc, nullptr, &m_swapChain);
    if (FAILED(hr)) return false;

    hr = DCompositionCreateDevice(dxgiDevice.Get(), IID_PPV_ARGS(&m_dcompDevice));
    if (FAILED(hr)) return false;

    hr = m_dcompDevice->CreateTargetForHwnd(m_hwnd, true, &m_dcompTarget);
    if (FAILED(hr)) return false;

    hr = m_dcompDevice->CreateVisual(&m_dcompVisual);
    if (FAILED(hr)) return false;

    hr = m_dcompVisual->SetContent(m_swapChain.Get());
    if (FAILED(hr)) return false;

    hr = m_dcompTarget->SetRoot(m_dcompVisual.Get());
    if (FAILED(hr)) return false;

    hr = m_dcompDevice->Commit();
    if (FAILED(hr)) return false;

    D2D1_FACTORY_OPTIONS options = {};
    hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory2), &options, reinterpret_cast<void**>(m_d2dFactory.GetAddressOf()));
    if (FAILED(hr)) return false;

    hr = m_d2dFactory->CreateDevice(dxgiDevice.Get(), &m_d2dDevice);
    if (FAILED(hr)) return false;

    hr = m_d2dDevice->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &m_d2dContext);
    if (FAILED(hr)) return false;

    m_d2dContext->SetDpi(dpi, dpi);

    ComPtr<IDXGISurface> dxgiBackBuffer;
    hr = m_swapChain->GetBuffer(0, IID_PPV_ARGS(&dxgiBackBuffer));
    if (FAILED(hr)) return false;

    D2D1_BITMAP_PROPERTIES1 bitmapProperties = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
        dpi, dpi
    );

    hr = m_d2dContext->CreateBitmapFromDxgiSurface(dxgiBackBuffer.Get(), &bitmapProperties, &m_d2dTargetBitmap);
    if (FAILED(hr)) return false;

    m_d2dContext->SetTarget(m_d2dTargetBitmap.Get());

    // Initialize WIC
    hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&m_wicFactory));
    
    // Create Brushes
    m_d2dContext->CreateSolidColorBrush(D2D1::ColorF(0.06f, 0.06f, 0.06f, 1.0f), &m_bgBrush);
    m_d2dContext->CreateSolidColorBrush(D2D1::ColorF(0.12f, 0.12f, 0.12f, 1.0f), &m_bgDarkBrush);
    m_d2dContext->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f), &m_textBrush);
    m_d2dContext->CreateSolidColorBrush(D2D1::ColorF(0.5f, 0.5f, 0.5f, 1.0f), &m_grayBrush);
    m_d2dContext->CreateSolidColorBrush(D2D1::ColorF(0.9f, 0.3f, 0.2f, 1.0f), &m_redBrush);
    m_d2dContext->CreateSolidColorBrush(D2D1::ColorF(0.44f, 0.36f, 0.56f, 1.0f), &m_purpleBrush);
    m_d2dContext->CreateSolidColorBrush(D2D1::ColorF(0.18f, 0.72f, 0.44f, 1.0f), &m_greenBrush);
    m_d2dContext->CreateSolidColorBrush(D2D1::ColorF(0.24f, 0.54f, 0.87f, 1.0f), &m_blueBrush);
    m_d2dContext->CreateSolidColorBrush(D2D1::ColorF(0.89f, 0.45f, 0.13f, 1.0f), &m_orangeBrush);

    // Create DWrite Formats
    DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(m_dwriteFactory.GetAddressOf()));
    
    m_dwriteFactory->CreateTextFormat(L"Segoe UI", NULL, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 14.0f, L"en-us", &m_textFormat);
    m_textFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    m_textFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    m_dwriteFactory->CreateTextFormat(L"Segoe UI", NULL, DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 42.0f, L"en-us", &m_textFormatLarge);
    m_textFormatLarge->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    m_textFormatLarge->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    m_dwriteFactory->CreateTextFormat(L"Segoe UI", NULL, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 12.0f, L"en-us", &m_textFormatSmall);
    m_textFormatSmall->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    m_textFormatSmall->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    m_dwriteFactory->CreateTextFormat(L"Segoe UI", NULL, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 14.0f, L"en-us", &m_textFormatLeft);
    m_textFormatLeft->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
    m_textFormatLeft->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
    m_textFormatLeft->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    ComPtr<IDWriteInlineObject> trimmingSign;
    m_dwriteFactory->CreateEllipsisTrimmingSign(m_textFormatLeft.Get(), &trimmingSign);
    DWRITE_TRIMMING trimming = { DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0 };
    m_textFormatLeft->SetTrimming(&trimming, trimmingSign.Get());

    m_dwriteFactory->CreateTextFormat(L"Consolas", NULL, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 11.0f, L"en-us", &m_textFormatCalGrid);
    m_textFormatCalGrid->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
    m_textFormatCalGrid->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    m_dwriteFactory->CreateTextFormat(L"Segoe MDL2 Assets", NULL, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 10.0f, L"en-us", &m_iconFormat);
    m_iconFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    m_iconFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    m_dwriteFactory->CreateTextFormat(L"Segoe MDL2 Assets", NULL, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 14.0f, L"en-us", &m_iconFormatLarge);
    m_iconFormatLarge->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    m_iconFormatLarge->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    return true;
}

void OverlayWindow::CleanupDirectX() { }

float OverlayWindow::centerY(float top, float bottom) { return top + ((bottom - top) / 2.0f); }

void OverlayWindow::Render() {
    if (!m_d2dContext) return;

    // Smoother spring-like interpolation
    m_currentWidth += (m_targetWidth - m_currentWidth) * 0.25f;
    m_currentHeight += (m_targetHeight - m_currentHeight) * 0.25f;

    m_d2dContext->BeginDraw();
    m_d2dContext->Clear(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f));

    float centerX = 500.0f / 2.0f;
    float topY = 0.0f;
    float left = centerX - (m_currentWidth / 2.0f);
    float right = centerX + (m_currentWidth / 2.0f);
    float bottom = m_currentHeight;
    
    float radius = m_currentHeight > 50.0f ? 35.0f : m_currentHeight / 2.0f;

    static RECT s_lastRgn = {0, 0, 0, 0};
    int padding = (int)(4 * m_scale);
    if (padding < 4) padding = 4; // ensure at least 4 pixels padding
    int rLeft = (int)(left * m_scale) - padding;
    int rTop = (int)(topY * m_scale) - padding;
    int rRight = (int)(right * m_scale) + padding;
    int rBottom = (int)(bottom * m_scale) + padding;
    if (s_lastRgn.left != rLeft || s_lastRgn.right != rRight || s_lastRgn.bottom != rBottom) {
        s_lastRgn = {rLeft, rTop, rRight, rBottom};
        HRGN hRgn = CreateRoundRectRgn(rLeft, rTop, rRight, rBottom, (int)((radius + padding) * 2 * m_scale), (int)((radius + padding) * 2 * m_scale));
        SetWindowRgn(m_hwnd, hRgn, TRUE);
    }

    
    D2D1_ROUNDED_RECT roundedRect = D2D1::RoundedRect(D2D1::RectF(left, topY, right, bottom), radius, radius);
    m_d2dContext->FillRoundedRectangle(&roundedRect, m_bgBrush.Get());

    if (m_isExpanded) {
        float expandRatio = (m_currentWidth - 150.0f) / (m_targetWidth - 150.0f + 0.1f);
        // Only draw contents if the pill is mostly expanded, to prevent overflow
        if (expandRatio > 0.7f) {
            if (m_viewMode == 0) { // Media
                if (m_mediaThumbnailChanged) {
                    m_mediaThumbnailD2D = nullptr;
                    if (m_mediaThumbnailWIC) {
                        m_d2dContext->CreateBitmapFromWicBitmap(m_mediaThumbnailWIC.Get(), &m_mediaThumbnailD2D);
                    }
                    m_mediaThumbnailChanged = false;
                }

                if (m_mediaThumbnailD2D) {
                    D2D1_RECT_F artRect = D2D1::RectF(left + 20, topY + 20, left + 80, topY + 80);
                    m_d2dContext->DrawBitmap(m_mediaThumbnailD2D.Get(), artRect);
                } else {
                    D2D1_ROUNDED_RECT artRect = D2D1::RoundedRect(D2D1::RectF(left + 20, topY + 20, left + 80, topY + 80), 8.0f, 8.0f);
                    m_d2dContext->FillRoundedRectangle(&artRect, m_bgDarkBrush.Get());
                }
                
                m_d2dContext->DrawTextW(m_mediaTitle, (UINT32)wcslen(m_mediaTitle), m_textFormatLeft.Get(), D2D1::RectF(left + 95, topY + 25, right - 70, topY + 45), m_textBrush.Get());
                m_d2dContext->DrawTextW(m_mediaArtist, (UINT32)wcslen(m_mediaArtist), m_textFormatLeft.Get(), D2D1::RectF(left + 95, topY + 45, right - 70, topY + 65), m_grayBrush.Get());
                
                // Animated Waveform
                float waveLeft = right - 60.0f;
                float waveTop = topY + 30.0f;
                float waveBottom = topY + 55.0f;
                float time = (float)GetTickCount64() * 0.008f;
                for (int i = 0; i < 6; ++i) {
                    float phase = (float)i * 2.1f;
                    float speed = 1.0f + ((float)i * 0.2f);
                    float h = (sin(time * speed + phase) * 0.5f + 0.5f) * (waveBottom - waveTop);
                    if (h < 2.0f) h = 2.0f;
                    if (!m_isPlaying) h = 2.0f;
                    
                    D2D1_ROUNDED_RECT bar = D2D1::RoundedRect(D2D1::RectF(waveLeft + i * 6.0f, waveBottom - h, waveLeft + i * 6.0f + 3.0f, waveBottom), 1.5f, 1.5f);
                    m_d2dContext->FillRoundedRectangle(&bar, m_textBrush.Get());
                }
                
                // Progress Bar
                int64_t position = m_mediaPosition;
                if (m_isPlaying) {
                    FILETIME ft;
                    GetSystemTimeAsFileTime(&ft);
                    int64_t now = ((int64_t)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
                    position += (now - m_mediaLastUpdated);
                }
                if (position < 0) position = 0;
                if (position > m_mediaDuration) position = m_mediaDuration;

                float progress = m_mediaDuration > 0 ? (float)position / (float)m_mediaDuration : 0.0f;
                float barWidth = (right - 60.0f) - (left + 60.0f);
                float filledWidth = barWidth * progress;
                
                D2D1_ROUNDED_RECT progBg = D2D1::RoundedRect(D2D1::RectF(left + 60, bottom - 50, right - 60, bottom - 46), 2.0f, 2.0f);
                m_d2dContext->FillRoundedRectangle(&progBg, m_bgDarkBrush.Get());
                
                D2D1_ROUNDED_RECT progFg = D2D1::RoundedRect(D2D1::RectF(left + 60, bottom - 50, left + 60 + filledWidth, bottom - 46), 2.0f, 2.0f);
                m_d2dContext->FillRoundedRectangle(&progFg, m_purpleBrush.Get());
                
                // Formatting time
                int posSec = (int)(position / 10000000);
                int durSec = (int)(m_mediaDuration / 10000000);
                int remSec = durSec - posSec;
                if (remSec < 0) remSec = 0;
                wchar_t posStr[16];
                swprintf_s(posStr, L"%d:%02d", posSec / 60, posSec % 60);
                wchar_t remStr[16];
                swprintf_s(remStr, L"-%d:%02d", remSec / 60, remSec % 60);

                m_d2dContext->DrawTextW(posStr, (UINT32)wcslen(posStr), m_textFormatSmall.Get(), D2D1::RectF(left, bottom - 60, left + 60, bottom - 35), m_grayBrush.Get());
                m_d2dContext->DrawTextW(remStr, (UINT32)wcslen(remStr), m_textFormatSmall.Get(), D2D1::RectF(right - 60, bottom - 60, right, bottom - 35), m_grayBrush.Get());
                
                // Draw Controls
                m_btnPrev = D2D1::RectF(centerX - 50, bottom - 35, centerX - 26, bottom - 11);
                m_btnPlayPause = D2D1::RectF(centerX - 18, bottom - 40, centerX + 18, bottom - 4);
                m_btnNext = D2D1::RectF(centerX + 26, bottom - 35, centerX + 50, bottom - 11);

                D2D1_ELLIPSE playPauseCircle = D2D1::Ellipse(D2D1::Point2F(centerX, bottom - 22), 18.0f, 18.0f);
                m_d2dContext->FillEllipse(&playPauseCircle, m_bgDarkBrush.Get());
                
                D2D1_ELLIPSE prevCircle = D2D1::Ellipse(D2D1::Point2F(centerX - 38, bottom - 23), 12.0f, 12.0f);
                m_d2dContext->FillEllipse(&prevCircle, m_bgDarkBrush.Get());

                D2D1_ELLIPSE nextCircle = D2D1::Ellipse(D2D1::Point2F(centerX + 38, bottom - 23), 12.0f, 12.0f);
                m_d2dContext->FillEllipse(&nextCircle, m_bgDarkBrush.Get());
                
                m_d2dContext->DrawTextW(L"\xE892", 1, m_iconFormat.Get(), m_btnPrev, m_purpleBrush.Get());
                m_d2dContext->DrawTextW(m_isPlaying ? L"\xE769" : L"\xE768", 1, m_iconFormatLarge.Get(), m_btnPlayPause, m_purpleBrush.Get());
                m_d2dContext->DrawTextW(L"\xE893", 1, m_iconFormat.Get(), m_btnNext, m_purpleBrush.Get());
            } 
            else if (m_viewMode == 1) { // Calendar
                D2D1_ROUNDED_RECT dateBox = D2D1::RoundedRect(D2D1::RectF(left + 15, topY + 15, left + 110, bottom - 15), 20.0f, 20.0f);
                m_d2dContext->FillRoundedRectangle(&dateBox, m_bgDarkBrush.Get());
                
                m_d2dContext->DrawTextW(L"JULY", 4, m_textFormatSmall.Get(), D2D1::RectF(left + 15, topY + 30, left + 110, topY + 45), m_redBrush.Get());
                m_d2dContext->DrawTextW(L"2026", 4, m_textFormatSmall.Get(), D2D1::RectF(left + 15, topY + 45, left + 110, topY + 60), m_grayBrush.Get());
                m_d2dContext->DrawTextW(L"26", 2, m_textFormatLarge.Get(), D2D1::RectF(left + 15, topY + 60, left + 110, topY + 110), m_textBrush.Get());
                m_d2dContext->DrawTextW(L"Sunday", 6, m_textFormatSmall.Get(), D2D1::RectF(left + 15, topY + 115, left + 110, topY + 135), m_grayBrush.Get());

                const wchar_t* cal = L"S   M   T   W   T   F   S\n\n            1   2   3   4\n5   6   7   8   9   10  11\n12  13  14  15  16  17  18\n19  20  21  22  23  24  25\n26  27  28  29  30  31";
                m_d2dContext->DrawTextW(cal, (UINT32)wcslen(cal), m_textFormatCalGrid.Get(), D2D1::RectF(left + 140, topY + 25, right - 20, bottom), m_textBrush.Get());
            }
            else if (m_viewMode == 2) { // Weather
                m_d2dContext->DrawTextW(m_weatherTemp, (UINT32)wcslen(m_weatherTemp), m_textFormatLarge.Get(), D2D1::RectF(left, topY, centerX, bottom), m_textBrush.Get());
                m_d2dContext->FillRectangle(D2D1::RectF(centerX, topY + 25, centerX + 1, bottom - 25), m_bgDarkBrush.Get());
                m_d2dContext->DrawTextW(m_weatherDetails, (UINT32)wcslen(m_weatherDetails), m_textFormatLeft.Get(), D2D1::RectF(centerX + 20, topY + 20, right, bottom), m_grayBrush.Get());
            }
            else if (m_viewMode == 3) { // Game Overlay
                struct Metric { const wchar_t* icon; int val; bool pct; ID2D1SolidColorBrush* color; };
                Metric metrics[5] = {
                    { L"\xEA6C", m_hwFps, false, m_greenBrush.Get() },   // FPS
                    { L"\xE968", m_hwCpu, true, m_blueBrush.Get() },     // CPU
                    { L"\xEC06", m_hwGpu, true, m_purpleBrush.Get() },   // GPU
                    { L"\xE9A6", m_hwRam, true, m_textBrush.Get() },     // RAM
                    { L"\xEDA2", m_hwDisk, true, m_orangeBrush.Get() }   // Disk
                };
                
                float startX = left + 20.0f;
                float boxW = 60.0f;
                float boxGap = 8.0f;
                
                for (int i = 0; i < 5; i++) {
                    float bx = startX + i * (boxW + boxGap);
                    D2D1_ROUNDED_RECT box = D2D1::RoundedRect(D2D1::RectF(bx, topY + 8.0f, bx + boxW, bottom - 8.0f), 6.0f, 6.0f);
                    m_d2dContext->FillRoundedRectangle(&box, m_bgDarkBrush.Get());
                    
                    // Icon
                    m_d2dContext->DrawTextW(metrics[i].icon, 1, m_iconFormatLarge.Get(), D2D1::RectF(bx + 2, topY, bx + 22, bottom), metrics[i].color);
                    
                    // Value
                    wchar_t valText[16];
                    if (metrics[i].pct) swprintf_s(valText, L"%d%%", metrics[i].val);
                    else swprintf_s(valText, L"%d", metrics[i].val);
                    m_d2dContext->DrawTextW(valText, (UINT32)wcslen(valText), m_textFormat.Get(), D2D1::RectF(bx + 22, topY + 9, bx + boxW, bottom), m_textBrush.Get());
                    
                    // Underline
                    m_d2dContext->FillRectangle(D2D1::RectF(bx + 15, bottom - 10, bx + boxW - 15, bottom - 8), metrics[i].color);
                }
            }
            else if (m_viewMode == 4) { // Control Center
                // Brightness Slider
                float barTopB = 30.0f;
                float barBottomB = 60.0f;
                float barLeft = left + 50.0f;
                float barRight = right - 20.0f;
                
                m_d2dContext->DrawTextW(L"\xE706", 1, m_iconFormatLarge.Get(), D2D1::RectF(left + 10, barTopB, barLeft, barBottomB), m_textBrush.Get());
                
                D2D1_ROUNDED_RECT bbg = D2D1::RoundedRect(D2D1::RectF(barLeft, barTopB + 10, barRight, barBottomB - 10), 5.0f, 5.0f);
                m_d2dContext->FillRoundedRectangle(&bbg, m_bgDarkBrush.Get());
                
                float bWidth = (barRight - barLeft) * m_brightnessLevel;
                D2D1_ROUNDED_RECT bfg = D2D1::RoundedRect(D2D1::RectF(barLeft, barTopB + 10, barLeft + bWidth, barBottomB - 10), 5.0f, 5.0f);
                m_d2dContext->FillRoundedRectangle(&bfg, m_textBrush.Get());
                
                // Volume Slider
                float barTopV = 80.0f;
                float barBottomV = 110.0f;
                
                m_d2dContext->DrawTextW(L"\xE767", 1, m_iconFormatLarge.Get(), D2D1::RectF(left + 10, barTopV, barLeft, barBottomV), m_textBrush.Get());
                
                D2D1_ROUNDED_RECT vbg = D2D1::RoundedRect(D2D1::RectF(barLeft, barTopV + 10, barRight, barBottomV - 10), 5.0f, 5.0f);
                m_d2dContext->FillRoundedRectangle(&vbg, m_bgDarkBrush.Get());
                
                float vWidth = (barRight - barLeft) * m_volumeLevel;
                D2D1_ROUNDED_RECT vfg = D2D1::RoundedRect(D2D1::RectF(barLeft, barTopV + 10, barLeft + vWidth, barBottomV - 10), 5.0f, 5.0f);
                m_d2dContext->FillRoundedRectangle(&vfg, m_textBrush.Get());
            }
        }
    } else {
        // IDLE VIEW (Mini pill)
        SYSTEMTIME st; GetLocalTime(&st);
        SYSTEM_POWER_STATUS sps; GetSystemPowerStatus(&sps);
        int battery = sps.BatteryLifePercent;
        
        wchar_t timeStr[64]; swprintf_s(timeStr, L"%02d:%02d", st.wHour, st.wMinute);
        wchar_t battStr[64];
        if (battery <= 100) swprintf_s(battStr, L"%d%%", battery);
        else swprintf_s(battStr, L"--");
        
        // Draw Time (left aligned in its box)
        m_d2dContext->DrawTextW(timeStr, (UINT32)wcslen(timeStr), m_textFormat.Get(), D2D1::RectF(left + 10.0f, topY, left + 65.0f, bottom), m_textBrush.Get());
        
        // Draw Battery Icon and % (right aligned)
        ID2D1SolidColorBrush* batColor = (battery <= 20) ? m_redBrush.Get() : m_grayBrush.Get();
        m_d2dContext->DrawTextW(L"\xEBB4", 1, m_iconFormatLarge.Get(), D2D1::RectF(right - 75.0f, topY, right - 45.0f, bottom), batColor);
        m_d2dContext->DrawTextW(battStr, (UINT32)wcslen(battStr), m_textFormat.Get(), D2D1::RectF(right - 45.0f, topY, right - 5.0f, bottom), batColor);
    }

    m_d2dContext->EndDraw();
    DXGI_PRESENT_PARAMETERS parameters = {};
    m_swapChain->Present1(1, 0, &parameters);
}

void OverlayWindow::SetSystemVolume(float level) {
    if (level < 0.0f) level = 0.0f;
    if (level > 1.0f) level = 1.0f;
    m_volumeLevel = level;
    
    IMMDeviceEnumerator* deviceEnumerator = NULL;
    if (SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_INPROC_SERVER, __uuidof(IMMDeviceEnumerator), (LPVOID*)&deviceEnumerator))) {
        IMMDevice* defaultDevice = NULL;
        if (SUCCEEDED(deviceEnumerator->GetDefaultAudioEndpoint(eRender, eConsole, &defaultDevice))) {
            IAudioEndpointVolume* endpointVolume = NULL;
            if (SUCCEEDED(defaultDevice->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_INPROC_SERVER, NULL, (LPVOID*)&endpointVolume))) {
                endpointVolume->SetMasterVolumeLevelScalar(level, NULL);
                endpointVolume->Release();
            }
            defaultDevice->Release();
        }
        deviceEnumerator->Release();
    }
}

float OverlayWindow::GetSystemVolume() {
    float level = m_volumeLevel;
    IMMDeviceEnumerator* deviceEnumerator = NULL;
    if (SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_INPROC_SERVER, __uuidof(IMMDeviceEnumerator), (LPVOID*)&deviceEnumerator))) {
        IMMDevice* defaultDevice = NULL;
        if (SUCCEEDED(deviceEnumerator->GetDefaultAudioEndpoint(eRender, eConsole, &defaultDevice))) {
            IAudioEndpointVolume* endpointVolume = NULL;
            if (SUCCEEDED(defaultDevice->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_INPROC_SERVER, NULL, (LPVOID*)&endpointVolume))) {
                endpointVolume->GetMasterVolumeLevelScalar(&level);
                endpointVolume->Release();
            }
            defaultDevice->Release();
        }
        deviceEnumerator->Release();
    }
    m_volumeLevel = level;
    return level;
}

void OverlayWindow::SetSystemBrightness(float level) {
    if (level < 0.0f) level = 0.0f;
    if (level > 1.0f) level = 1.0f;
    m_brightnessLevel = level;
    
    int brightness = (int)(level * 100.0f);
    
    wchar_t cmd[512];
    swprintf_s(cmd, L"powershell.exe -WindowStyle Hidden -Command \"Get-CimInstance -Namespace root/WMI -ClassName WmiMonitorBrightnessMethods | Invoke-CimMethod -MethodName WmiSetBrightness -Arguments @{Timeout=[uint32]1; Brightness=[byte]%d}\"", brightness);
    
    STARTUPINFO si = { sizeof(si) };
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi;
    if (CreateProcess(NULL, cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }
}

float OverlayWindow::GetSystemBrightness() {
    float level = m_brightnessLevel;
    IWbemLocator* pLoc = NULL;
    if (FAILED(CoCreateInstance(CLSID_WbemLocator, 0, CLSCTX_INPROC_SERVER, IID_IWbemLocator, (LPVOID*)&pLoc))) return level;
    
    IWbemServices* pSvc = NULL;
    if (FAILED(pLoc->ConnectServer(_bstr_t(L"ROOT\\WMI"), NULL, NULL, 0, NULL, 0, 0, &pSvc))) { pLoc->Release(); return level; }
    
    CoSetProxyBlanket(pSvc, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, NULL, RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE, NULL, EOAC_NONE);
    
    IEnumWbemClassObject* pEnumerator = NULL;
    if (SUCCEEDED(pSvc->ExecQuery(_bstr_t("WQL"), _bstr_t("SELECT CurrentBrightness FROM WmiMonitorBrightness"), WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY, NULL, &pEnumerator))) {
        IWbemClassObject* pclsObj = NULL;
        ULONG uReturn = 0;
        if (pEnumerator->Next(WBEM_INFINITE, 1, &pclsObj, &uReturn) == 0 && uReturn > 0) {
            VARIANT vtProp;
            if (SUCCEEDED(pclsObj->Get(L"CurrentBrightness", 0, &vtProp, 0, 0))) {
                if (vtProp.vt == VT_UI1) {
                    level = vtProp.bVal / 100.0f;
                }
                VariantClear(&vtProp);
            }
            pclsObj->Release();
        }
        pEnumerator->Release();
    }
    pSvc->Release();
    pLoc->Release();
    
    m_brightnessLevel = level;
    return level;
}
