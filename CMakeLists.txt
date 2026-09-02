cmake_minimum_required(VERSION 3.20)
project(ModeFlow LANGUAGES CXX)

set(MAIN_TARGET ${PROJECT_NAME})
set(CORE_TARGET ${PROJECT_NAME}Core)
set(TEST_TARGET ${PROJECT_NAME}Tests)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

include(CompilerConfig)
include(QtTranslations)
include(WindowsDependencies)
include(AppResources)

set(SITE_URL "https://goloveshko.github.io"
    CACHE STRING "The project website URL")
set(UPDATE_URL "https://raw.githubusercontent.com/goloveshko/ModeFlow/main/metadata/update.json"
    CACHE STRING "The dynamic URL for auto-update manifest checks")
set(LICENSE_URL "https://github.com/goloveshko/ModeFlow/blob/main/LICENSE"
    CACHE STRING "The project license URL")

# vcpkg static Qt packages may omit qtpaths, which triggers a noisy deployment
# warning during find_package(Qt6 ...) even though the build still works.
if(WIN32 AND DEFINED VCPKG_TARGET_TRIPLET AND VCPKG_TARGET_TRIPLET MATCHES "-static")
  set(QT_NO_QTPATHS_DEPLOYMENT_WARNING ON)
endif()

# Set output directory for the compiled .exe
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build/bin/$<CONFIG>")

# --- Dependencies ---
find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets Concurrent Network Svg)

# --- Logic to detect Static vs Shared ---
if(NOT DEFINED MODEFLOW_STATIC_BUILD)
  get_target_property(QT_IS_STATIC Qt6::Core TYPE)
  if (QT_IS_STATIC STREQUAL "STATIC_LIBRARY")
    set(MODEFLOW_STATIC_BUILD ON)
  else()
    set(MODEFLOW_STATIC_BUILD OFF)
  endif()
endif()

configure_compiler()

qt_standard_project_setup()

# Default tests compilation to ON to catch interface regressions early.
option(BUILD_TESTING "Build the testing tree." ON)
include(CTest)

# Organization in IDEs (Visual Studio)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# --- External Libraries ---
# QHotkey
set(QHOTKEY_EXAMPLES OFF CACHE BOOL "" FORCE)
add_subdirectory(external/QHotkey)

# AudioDeviceLib
add_subdirectory(external/AudioDeviceLib)
set_target_properties(AudioDeviceLib PROPERTIES EXCLUDE_FROM_ALL TRUE)
set_property(DIRECTORY external/AudioDeviceLib/demos PROPERTY EXCLUDE_FROM_ALL TRUE)


# --- Core & Common Sources ---
# This list contains all shared components. It will be compiled strictly ONCE
# using CMake's OBJECT library type, saving up to 50% of compilation times.
set(COMMON_SOURCES
    # CORE
    src/core/AppController.cpp src/core/AppController.h
    src/core/AppServices.cpp src/core/AppServices.h
    src/core/CliParser.cpp src/core/CliParser.h
    src/core/CliArgs.h
    src/core/CommandLineBuilder.cpp src/core/CommandLineBuilder.h
    src/core/ConfigManager.cpp src/core/ConfigManager.h
    src/core/ConfigTypes.cpp src/core/ConfigTypes.h
    src/core/IDialogManager.h
    src/core/ISettingsManager.h
    src/core/IStyleManager.h
    src/core/IWorkspaceManager.h
    src/core/LocalizationManager.cpp src/core/LocalizationManager.h
    src/core/ServiceFactory.cpp src/core/ServiceFactory.h
    src/core/ServiceWiring.cpp src/core/ServiceWiring.h
    src/core/StartupPreflight.cpp src/core/StartupPreflight.h
    src/core/StylePaths.h
    src/core/WorkspaceModel.cpp src/core/WorkspaceModel.h
    src/core/WorkspaceService.cpp src/core/WorkspaceService.h
    src/core/impl/SettingsManager.cpp src/core/impl/SettingsManager.h
    src/core/impl/WorkspaceManager.cpp src/core/impl/WorkspaceManager.h
    src/core/impl/StyleManager.cpp src/core/impl/StyleManager.h

    # GUI
    src/gui/AboutDialog.cpp src/gui/AboutDialog.h src/gui/AboutDialog.ui
    src/gui/AppLaunchDialog.cpp src/gui/AppLaunchDialog.h src/gui/AppLaunchDialog.ui
    src/gui/AppListWidget.cpp src/gui/AppListWidget.h
    src/gui/BaseDialog.cpp src/gui/BaseDialog.h
    src/gui/DialogManager.cpp src/gui/DialogManager.h
    src/gui/FluentItemDelegate.cpp src/gui/FluentItemDelegate.h
    src/gui/FontAwesome.cpp src/gui/FontAwesome.h
    src/gui/HotkeyEdit.cpp src/gui/HotkeyEdit.h
    src/gui/HotkeyValidator.cpp src/gui/HotkeyValidator.h
    src/gui/LogHighlighter.cpp src/gui/LogHighlighter.h
    src/gui/LogViewerDialog.cpp src/gui/LogViewerDialog.h src/gui/LogViewerDialog.ui
    src/gui/MainWindow.cpp src/gui/MainWindow.h src/gui/MainWindow.ui
    src/gui/ProfileEditor.cpp src/gui/ProfileEditor.h
    src/gui/ProfileIconMenu.cpp src/gui/ProfileIconMenu.h
    src/gui/ProfileListView.cpp src/gui/ProfileListView.h
    src/gui/ProfileTransfer.cpp src/gui/ProfileTransfer.h
    src/gui/SettingsDialog.cpp src/gui/SettingsDialog.h src/gui/SettingsDialog.ui
    src/gui/StyleBridge.cpp src/gui/StyleBridge.h
    src/gui/TrayController.cpp src/gui/TrayController.h
    src/gui/UpdateDialog.cpp src/gui/UpdateDialog.h src/gui/UpdateDialog.ui

    # SERVICES
    src/services/AppLauncher.cpp src/services/AppLauncher.h
    src/services/AudioDeviceManager.cpp src/services/AudioDeviceManager.h
    src/services/AudioFeedbackService.cpp src/services/AudioFeedbackService.h
    src/services/AutostartManager.cpp src/services/AutostartManager.h
    src/services/DisplayManager.cpp src/services/DisplayManager.h
    src/services/HotkeyManager.cpp src/services/HotkeyManager.h
    src/services/SingleInstanceGuard.cpp src/services/SingleInstanceGuard.h
    src/services/UpdateService.cpp src/services/UpdateService.h

    # UTILS
    src/utils/ComInitGuard.cpp src/utils/ComInitGuard.h
    src/utils/Constants.h
    src/utils/CrashHandler.cpp src/utils/CrashHandler.h
    src/utils/DeviceUtils.cpp src/utils/DeviceUtils.h
    src/utils/Logging.cpp src/utils/Logging.h
    src/utils/LogManager.cpp src/utils/LogManager.h
    src/utils/ProfileSerializer.cpp src/utils/ProfileSerializer.h
    src/utils/StyleUtils.cpp src/utils/StyleUtils.h
    src/utils/SystemUtils.cpp src/utils/SystemUtils.h
    src/utils/TaskScheduler.cpp src/utils/TaskScheduler.h
    src/utils/VersionInfo.h
    src/utils/WinKeyTranslator.cpp src/utils/WinKeyTranslator.h
)

# --- CENTRALIZED OBJECT LIBRARY (${CORE_TARGET}) ---
# Compiles all shared sources ONCE as an object library.
# Both the main executable and the tests will link against this target.
add_library(${CORE_TARGET} OBJECT ${COMMON_SOURCES})

# Target-specific includes (propagated publicly)
target_include_directories(${CORE_TARGET} PUBLIC
    src
    src/core
    src/core/impl
    src/gui
    src/services
    src/utils
    external/QHotkey/QHotkey
    external/AudioDeviceLib/include
)

# Centralized dependency linking for the object library (propagated publicly)
target_link_libraries(${CORE_TARGET}
    PUBLIC
        Qt6::Widgets
        Qt6::Concurrent
        Qt6::Network
        Qt6::Svg
        QHotkey::QHotkey
        AudioDeviceLib
)

link_windows_system_libs(${CORE_TARGET})

# Centralized preprocessor definitions (propagated publicly)
target_compile_definitions(${CORE_TARGET} PUBLIC QT_MESSAGELOGCONTEXT)

# Inject the dynamic update URL definition to the preprocessor for both static and shared builds
target_compile_definitions(${CORE_TARGET} PUBLIC "SITE_URL=\"${SITE_URL}\"")
target_compile_definitions(${CORE_TARGET} PUBLIC "UPDATE_URL=\"${UPDATE_URL}\"")
target_compile_definitions(${CORE_TARGET} PUBLIC "LICENSE_URL=\"${LICENSE_URL}\"")

if(MODEFLOW_STATIC_BUILD)
  target_compile_definitions(${CORE_TARGET} PUBLIC MODEFLOW_STATIC)
  message(STATUS "Build Mode: STATIC")
else()
  message(STATUS "Build Mode: SHARED (DLL)")
endif()

# --- MAIN APPLICATION TARGET ---
set(PROJECT_SOURCES
    src/main.cpp
)

if(WIN32)
  list(APPEND PROJECT_SOURCES "assets/win/resources.rc")
endif()

source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${PROJECT_SOURCES})

# Declare the main GUI executable
qt_add_executable(${MAIN_TARGET}
    WIN32 MACOSX_BUNDLE
    ${PROJECT_SOURCES}
)

# --- Resources & Assets (assets & i18n) ---
# Compile resources directly into the executable target.
# This guarantees that the linker includes static resource initializers (QRC) on startup.
setup_app_resources(${MAIN_TARGET})
setup_qt_system_translations(${MAIN_TARGET})

# Simply link against our centralized Core Object Library!
target_link_libraries(${MAIN_TARGET} PRIVATE ${CORE_TARGET})

# Configure main executable properties
set_target_properties(${MAIN_TARGET} PROPERTIES
    WIN32_EXECUTABLE TRUE
)


# --- UNIT TESTS SUBDIRECTORY ---
if(BUILD_TESTING)
  # Triggers cmake compilation inside tests/ directory
  add_subdirectory(tests)
endif()


# --- IDE CONFIGURATIONS ---
if(MSVC)
  set_target_properties(${MAIN_TARGET} PROPERTIES
        VS_DEBUGGER_ENVIRONMENT "PATH=${_qt_bin_dir}\;$ENV{PATH}"
    )
endif()

if(TARGET qhotkey)
  set_target_properties(qhotkey PROPERTIES FOLDER "External")
endif()

if(TARGET AudioDeviceLib)
  set_target_properties(AudioDeviceLib PROPERTIES FOLDER "External")
endif()

set_directory_properties(PROPERTIES VS_STARTUP_PROJECT ${MAIN_TARGET})

# --- Centralized Compilation Database Synchronization ---
# Automatically copy the generated compile_commands.json from the active binary directory
# directly to the project's root build/ folder. This keeps the root directory clean
# while ensuring that clangd (or other LSPs) can always locate the active configuration.
if(CMAKE_EXPORT_COMPILE_COMMANDS)
  add_custom_target(copy_compile_commands ALL
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_SOURCE_DIR}/build"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_BINARY_DIR}/compile_commands.json"
            "${CMAKE_SOURCE_DIR}/build/compile_commands.json"
        COMMENT "Syncing active compile_commands.json to root build/ directory..."
    )
endif()
