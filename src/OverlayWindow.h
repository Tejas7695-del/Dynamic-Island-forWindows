#pragma once

#include <windows.h>
#include <d3d11.h>
#include <dxgi1_3.h>
#include <d2d1_2.h>
#include <cstdint>
#include <dcomp.h>
#include <dwrite.h>
#include <wincodec.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

class OverlayWindow {
public:
    OverlayWindow();
    ~OverlayWindow();

    bool Create(HINSTANCE hInstance);
    void Render();
    void Update();

private:
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(UINT uMsg, WPARAM wParam, LPARAM lParam);

    bool InitializeDirectX();
    void CleanupDirectX();
    
    HWND m_hwnd;
    
    // D3D / DXGI
    ComPtr<ID3D11Device> m_d3dDevice;
    ComPtr<ID3D11DeviceContext> m_d3dContext;
    ComPtr<IDXGISwapChain1> m_swapChain;
    
    // D2D
    ComPtr<ID2D1Factory2> m_d2dFactory;
    ComPtr<ID2D1Device> m_d2dDevice;
    ComPtr<ID2D1DeviceContext> m_d2dContext;
    ComPtr<ID2D1Bitmap1> m_d2dTargetBitmap;
    
    // DComp
    ComPtr<IDCompositionDevice> m_dcompDevice;
    ComPtr<IDCompositionTarget> m_dcompTarget;
    ComPtr<IDCompositionVisual> m_dcompVisual;
    
    // WIC
    ComPtr<IWICImagingFactory> m_wicFactory;
    ComPtr<IWICBitmapSource> m_mediaThumbnailWIC; // Updated by background thread
    ComPtr<ID2D1Bitmap> m_mediaThumbnailD2D;      // Updated by main thread
    bool m_mediaThumbnailChanged;
    
    // DWrite
    ComPtr<IDWriteFactory> m_dwriteFactory;
    ComPtr<IDWriteTextFormat> m_textFormat;
    ComPtr<IDWriteTextFormat> m_textFormatLarge;
    ComPtr<IDWriteTextFormat> m_textFormatSmall;
    ComPtr<IDWriteTextFormat> m_textFormatLeft;
    ComPtr<IDWriteTextFormat> m_textFormatCalGrid;
    ComPtr<IDWriteTextFormat> m_iconFormat;
    ComPtr<IDWriteTextFormat> m_iconFormatLarge;
    
    // Brushes
    ComPtr<ID2D1SolidColorBrush> m_bgBrush;
    ComPtr<ID2D1SolidColorBrush> m_bgDarkBrush;
    ComPtr<ID2D1SolidColorBrush> m_textBrush;
    ComPtr<ID2D1SolidColorBrush> m_grayBrush;
    ComPtr<ID2D1SolidColorBrush> m_redBrush;
    ComPtr<ID2D1SolidColorBrush> m_purpleBrush;
    ComPtr<ID2D1SolidColorBrush> m_greenBrush;
    ComPtr<ID2D1SolidColorBrush> m_blueBrush;
    ComPtr<ID2D1SolidColorBrush> m_orangeBrush;

    // Animation & State
    float m_targetWidth;
    float m_targetHeight;
    float m_currentWidth;
    float m_currentHeight;
    bool m_isHovered;
    bool m_isExpanded;
    int m_viewMode; // 0=Media, 1=Calendar, 2=Weather, 3=Hardware
    
    // Weather Data
    bool m_stopWeatherThread;
    float m_scale;
    wchar_t m_weatherRegion[64];
    wchar_t m_weatherTemp[32];
    wchar_t m_weatherDetails[128];
    HANDLE m_weatherThread;
    static DWORD WINAPI WeatherThreadProc(LPVOID lpParam);
    void FetchWeather();
    static DWORD WINAPI HardwareThreadProc(LPVOID lpParam);
    float centerY(float top, float bottom);
    
    // Media Info
    wchar_t m_mediaTitle[128];
    wchar_t m_mediaArtist[128];
    int m_mediaTicks;
    HANDLE m_mediaThread;
    
    int64_t m_mediaPosition;
    int64_t m_mediaDuration;
    int64_t m_mediaLastUpdated;
    bool m_isPlaying;

    static DWORD WINAPI MediaThreadProc(LPVOID lpParam);
    
    // Hardware Info
    HANDLE m_hardwareThread;
    bool m_stopHardwareThread;
    int m_hwFps;
    int m_hwCpu;
    int m_hwGpu;
    int m_hwRam;
    int m_hwDisk;
    
    // Control Center
    float m_volumeLevel;
    float m_brightnessLevel;
    bool m_isDraggingVolume;
    bool m_isDraggingBrightness;
    void SetSystemVolume(float level);
    float GetSystemVolume();
    void SetSystemBrightness(float level);
    float GetSystemBrightness();
    
    // Hit Testing
    D2D1_RECT_F m_btnPrev;
    D2D1_RECT_F m_btnPlayPause;
    D2D1_RECT_F m_btnNext;
};
