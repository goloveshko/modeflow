#ifndef VERSIONINFO_H
#define VERSIONINFO_H

#define APP_VERSION_MAJOR 0
#define APP_VERSION_MINOR 9
#define APP_VERSION_PATCH 0
#define APP_VERSION_BUILD 0

#define STRINGIZE_HELPER(x) #x
#define STRINGIZE(x) STRINGIZE_HELPER(x)

#define APP_COPYRIGHT_YEAR "2026"

#define APP_VERSION_STR STRINGIZE(APP_VERSION_MAJOR) "." STRINGIZE(APP_VERSION_MINOR) "." STRINGIZE(APP_VERSION_PATCH)
#define APP_COMPANY_NAME "Goloveshko"
#define APP_INTERNAL_NAME "ModeFlow"
#define APP_EXECUTABLE_NAME APP_INTERNAL_NAME ".exe"
#define APP_PRODUCT_NAME APP_INTERNAL_NAME " - Workspace Manager"
#define APP_DOMAIN "sergey.is-a.dev"
#define APP_ICON_PATH "ModeFlow.ico"
#define APP_COPYRIGHT "Copyright (C) " APP_COPYRIGHT_YEAR " " APP_COMPANY_NAME

// Dynamic URLs with CMake override support
#ifndef SITE_URL
#define SITE_URL "https://sergey.is-a.dev"
#endif

#ifndef LICENSE_URL
#define LICENSE_URL "https://github.com/goloveshko/ModeFlow/blob/main/LICENSE"
#endif

#ifndef UPDATE_URL
#define UPDATE_URL "https://raw.githubusercontent.com/goloveshko/ModeFlow/main/metadata/update.json"
#endif

#ifdef __cplusplus

namespace ModeFlow::Info {
inline const QString Version = QString::fromLatin1(APP_VERSION_STR);
inline const QString Company = QString::fromLatin1(APP_COMPANY_NAME);
inline const QString ProductName = QString::fromLatin1(APP_PRODUCT_NAME);
inline const QString Copyright = QString::fromUtf8(APP_COPYRIGHT);
inline const QString Domain = QString::fromLatin1(APP_DOMAIN);

inline const QString SiteUrl = QString::fromLatin1(SITE_URL);
inline const QString LicenseUrl = QString::fromLatin1(LICENSE_URL);
inline const QString UpdateManifestUrl = QString::fromLatin1(UPDATE_URL);
} // namespace ModeFlow::Info
#endif

#endif // VERSIONINFO_H
