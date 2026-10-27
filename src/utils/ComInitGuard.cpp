#include "ComInitGuard.h"

#include <QtGlobal>

#ifdef Q_OS_WIN
#include <objbase.h>
#endif

namespace ModeFlow::Utils {

ComInitGuard::ComInitGuard(Apartment apartment) {
#ifdef Q_OS_WIN
    m_hr = CoInitializeEx(nullptr, static_cast<DWORD>(apartment));
    m_ok = SUCCEEDED(m_hr) || m_hr == RPC_E_CHANGED_MODE;
#else
    Q_UNUSED(apartment);
    m_hr = 0;
    m_ok = true;
#endif
}

ComInitGuard::~ComInitGuard() {
#ifdef Q_OS_WIN
    if (SUCCEEDED(m_hr)) {
        CoUninitialize();
    }
#endif
}

bool ComInitGuard::isOk() const {
    return m_ok;
}

} // namespace ModeFlow::Utils
