#pragma once

namespace ModeFlow::Utils {

/**
 * @brief Lightweight, zero-pollution RAII Guard for Windows COM initialization.
 * Keeps standard <windows.h> out of global headers to prevent macro conflicts.
 */
class ComInitGuard {
public:
    // Values mirror COINIT_* from the Windows API so this header can stay platform-free.
    enum class Apartment : unsigned long {
        MultiThreaded = 0,
        ApplicationThreaded = 2
    };

    // Worker threads without a message pump must not take an application apartment.
    explicit ComInitGuard(Apartment apartment = Apartment::ApplicationThreaded);
    ~ComInitGuard();

    bool isOk() const;

    ComInitGuard(const ComInitGuard&) = delete;
    ComInitGuard& operator=(const ComInitGuard&) = delete;
    ComInitGuard(ComInitGuard&&) = delete;
    ComInitGuard& operator=(ComInitGuard&&) = delete;

private:
    bool m_ok = false;
    long m_hr = 0; // HRESULT is internally represented as a long
};

} // namespace ModeFlow::Utils
