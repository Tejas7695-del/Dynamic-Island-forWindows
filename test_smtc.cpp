#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Media.Control.h>
#include <winrt/Windows.Storage.Streams.h>
#include <iostream>

using namespace winrt;
using namespace winrt::Windows::Media::Control;

int main() {
    init_apartment();
    auto manager = GlobalSystemMediaTransportControlsSessionManager::RequestAsync().get();
    auto session = manager.GetCurrentSession();
    if (session) {
        auto timeline = session.GetTimelineProperties();
        std::wcout << L"Timeline Position: " << timeline.Position().count() << std::endl;
        
        auto info = session.TryGetMediaPropertiesAsync().get();
        auto thumbnail = info.Thumbnail();
        if (thumbnail) {
            std::wcout << L"Got thumbnail stream reference!" << std::endl;
        }
    }
    return 0;
}
