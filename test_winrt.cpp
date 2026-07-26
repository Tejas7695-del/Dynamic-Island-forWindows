#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Media.Control.h>
#include <iostream>

using namespace winrt;
using namespace Windows::Media::Control;

int main() {
    init_apartment();
    auto manager = GlobalSystemMediaTransportControlsSessionManager::RequestAsync().get();
    auto session = manager.GetCurrentSession();
    if (session) {
        auto info = session.TryGetMediaPropertiesAsync().get();
        std::wcout << info.Title().c_str() << L" - " << info.Artist().c_str() << std::endl;
    }
    return 0;
}
