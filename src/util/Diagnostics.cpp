#include "pch.h"
#include "Diagnostics.h"

namespace rm {

void Diagnostics::MaybeRollFpsWindow_NoLock(Clock::time_point now)
{
    const auto elapsed = std::chrono::duration<double>(now - m_fpsWindowStart).count();
    if (elapsed >= 1.0) {
        m_captureFps = static_cast<double>(m_framesInWindow) / elapsed;
        m_presentFps = static_cast<double>(m_presentsInWindow) / elapsed;
        m_framesInWindow = 0;
        m_presentsInWindow = 0;
        m_fpsWindowStart = now;
    }
}

void Diagnostics::SetCaptureSize(int w, int h)
{
    std::lock_guard lock(m_mutex);
    m_captureW = w;
    m_captureH = h;
}

void Diagnostics::SetCropSize(int w, int h)
{
    std::lock_guard lock(m_mutex);
    m_cropW = w;
    m_cropH = h;
}

void Diagnostics::OnFrameReceived()
{
    std::lock_guard lock(m_mutex);
    const auto now = Clock::now();
    ++m_framesReceived;
    ++m_framesInWindow;
    m_lastFrameTime = now;
    m_hasFrameTime = true;
    MaybeRollFpsWindow_NoLock(now);
}

void Diagnostics::OnFrameDropped()
{
    std::lock_guard lock(m_mutex);
    ++m_dropped;
}

void Diagnostics::OnPresent()
{
    std::lock_guard lock(m_mutex);
    const auto now = Clock::now();
    ++m_presents;
    ++m_presentsInWindow;
    MaybeRollFpsWindow_NoLock(now);
}

void Diagnostics::SetNote(std::wstring note)
{
    std::lock_guard lock(m_mutex);
    m_note = std::move(note);
}

DiagnosticsSnapshot Diagnostics::Snapshot() const
{
    std::lock_guard lock(m_mutex);
    DiagnosticsSnapshot s;
    s.captureWidth = m_captureW;
    s.captureHeight = m_captureH;
    s.cropWidth = m_cropW;
    s.cropHeight = m_cropH;
    s.captureFps = m_captureFps;
    s.presentFps = m_presentFps;
    s.droppedFrames = m_dropped;
    s.framesReceived = m_framesReceived;
    s.presents = m_presents;
    s.note = m_note;
    if (m_hasFrameTime) {
        s.lastFrameAgeMs = std::chrono::duration<double, std::milli>(Clock::now() - m_lastFrameTime).count();
    }
    return s;
}

} // namespace rm
