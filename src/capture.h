#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <utility>

#include "capture_session.h"
#include "capture_settings.h"

namespace imterm {

// Combo items own their values, including dynamically enumerated port names.
// Replacing a list or destroying its source cannot invalidate an item.
template<typename T>
struct ComboDataItem {
    std::string label;
    T value;
    ComboDataItem(std::string text, T data) : label(std::move(text)), value(std::move(data)) {}
};

class Capture {
public:
    explicit Capture(CaptureSession::TransportFactory transportFactory = MakeSerialTransport,
        std::filesystem::path settingsPath = "imterm.toml");
    ~Capture();
    Capture(const Capture&) = delete;
    Capture& operator=(const Capture&) = delete;
    bool Connect(const SettingsValues& values);
    void Render();
    void Update();
    void Shutdown();
    CaptureSession& GetSession();
    const CaptureSettings& GetSettings() const;
    float GetInterfaceZoomFactor();
    void InitializeInterfaceZoom();
    void ApplyPendingInterfaceZoom();
    void QueueInterfaceZoomWheel(float delta);
private:
    struct Impl;
    std::unique_ptr<Impl> mImpl;
};

} // namespace imterm
