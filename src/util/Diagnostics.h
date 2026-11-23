#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>

namespace rm {

struct DiagnosticsSnapshot {
    int captureWidth = 0;
    int captureHeight = 0;
    int cropWidth = 0;
    int cropHeight = 0;
    double captureFps = 0.0;
    double presentFps = 0.0;
    uint64_t droppedFrames = 0;
    uint64_t framesReceived = 0;
    uint64_t presents = 0;
    // Age of the last captured frame relative to this process clock when the snapshot was taken.
    // This is NOT end-to-end display latency.
    double lastFrameAgeMs = -1.0;
    std::wstring note;
};

class Diagnostics {
public:
    void SetCaptureSize(int w, int h);
    void SetCropSize(int w, int h);
    void OnFrameReceived();
    void OnFrameDropped();
    void OnPresent();
    void SetNote(std::wstring note);

    DiagnosticsSnapshot Snapshot() const;

private:
    mutable std::mutex m_mutex;
    int m_captureW = 0;
    int m_captureH = 0;
    int m_cropW = 0;
    int m_cropH = 0;
    uint64_t m_framesReceived = 0;
    uint64_t m_presents = 0;
    uint64_t m_dropped = 0;
    std::wstring m_note;

    using Clock = std::chrono::steady_clock;
    Clock::time_point m_lastFrameTime{};
    bool m_hasFrameTime = false;

    Clock::time_point m_fpsWindowStart = Clock::now();
    uint64_t m_framesInWindow = 0;
    uint64_t m_presentsInWindow = 0;
    double m_captureFps = 0.0;
    double m_presentFps = 0.0;

    void MaybeRollFpsWindow_NoLock(Clock::time_point now);
};

} // namespace rm
