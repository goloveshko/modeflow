# Changelog

All notable changes to the **ModeFlow** project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).


## [1.0.0] - 2026-10-06

### 🇬🇧 English
Welcome to the official **v1.0.0** release of **ModeFlow**! This milestone release introduces a modernized Windows 11 Fluent experience, a robust background execution model, and seamless GitHub Releases integration.

#### ✨ Features & UI/UX
* **Windows 11 Fluent Design:** Integrated native Mica material, rounded corners, and semi-transparent Fluent Cards for all configuration groups.
* **Custom Themed Dialogs:** All file pickers and dialogs are styled natively according to the active theme (Light/Dark) with no unstyled system dialogs.
* **Profile Confirmation Prompts:** Added a "Confirm before applying profiles" toggle in Settings to prevent accidental profile activation from the tray or list.
* **Clean System Tray:** Streamlined tray context menu with quick access to active profiles, audio outputs, and display switching.
* **Visual Profile Indicator:** Active profile is clearly highlighted with an accent-colored indicator dot in the sidebar list.

#### 🐛 Bug Fixes
* **Profile Autosave Race Condition:** Fixed a critical issue where switching between profiles rapidly or during hotkey assignment could overwrite the selected profile with the previous one's data.
* **Eliminated Duplicate Hardware Queries:** Removed duplicate capture triggers when clicking "Use current settings", cutting redundant display driver and COM queries in half.
* **Window Geometry Clamping:** Resolved startup resizing micro-stutters by aligning default dimensions with minimum layout constraints (650x550).
* **Audio Feedback Timing:** Guaranteed that confirmation beeps play strictly through the newly applied default audio device.

#### ⚙️ Performance & Architecture
* **GitHub Releases API:** Switched the update checker to the official GitHub API, displaying release notes and updates directly without auxiliary files.
* **Fast Log Viewer:** High-performance diagnostics window with lazy-loaded syntax highlighting and non-blocking incremental log reading.
* **Process Abort Safety:** Application launch timers are actively managed; switching workspaces immediately cancels pending delayed launches.
* **Differential Hotkey Registration:** Hotkeys are dynamically diffed on changes, avoiding complete OS hook re-registration cycles.

---

### 🇷🇺 Русский
Добро пожаловать в официальный релиз **ModeFlow v1.0.0**! Этот релиз приносит нативный Fluent-дизайн Windows 11, легковесную архитектуру фонового сервиса и интеграцию с GitHub Releases API.

#### ✨ Интерфейс и возможности
* **Fluent Design (Windows 11):** Нативная поддержка эффекта слюды (Mica), закруглённые углы и полупрозрачные карточки Fluent Cards.
* **Стилизованные проводники:** Окна выбора файлов и диалоги встроены в общую тему приложения (Light/Dark) с эффектом Mica.
* **Подтверждение переключения:** В настройки добавлен чекбокс «Запрашивать подтверждение перед переключением» для защиты от случайных кликов в трее.
* **Лаконичный системный трей:** Быстрое переключение рабочих пространств, мониторов и звуковых устройств прямо из компактного меню.
* **Индикатор активного профиля:** Текущий активный профиль в системе визуально выделяется акцентным маркером в списке.

#### 🐛 Исправления ошибок
* **Изоляция профилей при переключении:** Устранен критический баг, из-за которого быстрое переключение элементов в списке во время редактирования хоткея приводило к перезаписи данных целевого профиля.
* **Устранение двойного опроса железа:** Убран скрытый двойной вызов WinAPI/COM при клике по кнопке «Захватить текущие настройки».
* **Коррекция геометрии окна:** Устранены микро-рывки при открытии окна благодаря выравниванию базовых размеров с минимальными лимитами разметки (650x550).
* **Синхронизация звукового сигнала:** Звуковой сигнал подтверждения теперь воспроизводится строго на вновь применённом аудиоустройстве.

#### ⚙️ Производительность и Архитектура
* **Интеграция с GitHub Releases API:** Обновления проверяются напрямую через REST API GitHub с автоматическим показом чейнджлога.
* **Быстрый просмотр логов:** Высокопроизводительное окно диагностики с ленивой подсветкой синтаксиса и инкрементальным чтением файла.
* **Безопасный отложенный запуск:** Таймеры запуска программ контролируются; смена профиля мгновенно отменяет запланированные запуски.
* **Умный дифф хоткеев:** Перерегистрация глобальных клавиш затрагивает только изменённые комбинации, снижая нагрузку на систему.

---

## [0.9.0-rc] - 2026-07-15

### 🇬🇧 English
Welcome to the Release Candidate (**v0.9.0**) of **ModeFlow**! This release brings a massive architectural rewrite, visual polish, and performance optimization to ensure a native Windows 11 Fluent experience and lightweight background execution.

#### ✨ Features & UI/UX Upgrades:
* **Fluent Cards & Spacing:** Replaced legacy group box borders with modern, rounded, semi-transparent Fluent Cards (Mica layering). Enforced spacious minimal sizes for all dialogs to support high-DPI scaling.
* **Custom Themed File Dialogs:** All file pickers are now built-in Qt dialogs fully styled under the active theme (Light/Dark) with native Mica glass, removing unstyled white dialogs and console exceptions.
* **Opt-in Confirmation Prompts:** Added a new "Confirm before applying profiles" settings checkbox, allowing users to toggle confirmation popups for tray and context menu switches, while keeping global hotkeys instant.
* **Uncluttered System Tray:** Renamed the main tray menu item to "Open ModeFlow" (or "ModeFlow" in bold) and shortened the sub-menu name to "Profiles".
* **Active Profile Visual Feedback:** Added a subtle status dot next to the active profile name, ensuring clear feedback of the active system state.

#### ⚙️ Performance & Architecture:
* **Fast Log Viewer:** Integrated high-performance `QSyntaxHighlighter` (lazy loading) and zero-allocation `QStringView` parsing, completely eliminating main-thread HTML freezing.
* **Delayed Launch Abort Protection:** Switched to managed, cancelable timers. Switching away from a profile now instantly aborts any pending delayed application launches, preventing runaway background processes.
* **Smart Hotkey Diffing:** Rebuilt `HotkeyManager` to only update/re-register modified keys, eliminating redundant Win32 hook mutations and log spam.
* **Zero-Flicker Window Restoration:** Fixed Windows 11 DWM Mica "creeping" geometry bugs and startup resizing flickering.
* **DIP & Decoupling:** Extracted profile icons, details forms, and file transactions into isolated, lightweight presenter classes.
* **Modular CMake:** Restructured tests into a separate folder, compiling all common sources into a shared `ModeFlowCore` Object Library (reducing compilation times by 50%).

---

### 🇷🇺 Русский
Добро пожаловать в стабильный предрелиз (**v0.9.0**) **ModeFlow**! Этот релиз представляет собой масштабный цикл рефакторинга архитектуры, визуальной полировки и оптимизации производительности, обеспечивающий нативный Fluent-дизайн Windows 11 и легкую работу приложения в фоне.

#### ✨ Новые возможности и интерфейс:
* **Fluent-карточки (Windows 11):** Заменили устаревшие рамки групп параметров на закругленные полупрозрачные карточки (Fluent Cards). Задали просторные лимиты по ширине окон для поддержки масштабирования High-DPI.
* **Стилизованные проводники файлов:** Диалоги импорта/экспорта теперь встроены в тему приложения (Light/Dark) с нативным Mica-эффектом, убирая белые системные окна и ошибки COM.
* **Опциональные подтверждения:** Добавлен чекбокс «Запрашивать подтверждение перед переключением» в настройки. Контроль подтверждений централизован, а горячие клавиши по-прежнему работают мгновенно.
* **Лаконичный системный трей:** Главный пункт трей-меню заменен на лаконичное «Открыть ModeFlow» (Open ModeFlow), а заголовок подменю профилей сокращен до простого «Профили» (Profiles).
* **Маркер активности:** Добавлена аккуратная Fluent-точка активности слева от названия текущего профиля, обеспечивая четкий отклик о реальном состоянии системы.

#### ⚙️ Оптимизации и Архитектура:
* **Быстрый лог-вьювер:** Переведен на `QSyntaxHighlighter` с ленивой загрузкой и zero-allocation парсинг `QStringView` (загрузка логов без зависаний).
* **Безопасный отложенный запуск:** Уход с профиля теперь мгновенно отменяет запланированные отложенные запуски программ, предотвращая «беглые» фоновые процессы.
* **Умный дифф хоткеев:** Перерегистрация затрагивает только измененные или новые клавиши, исключая лишнюю нагрузку на Windows API и спам в консоли.
* **Устранение фликкеров окон:** Устранен баг сползания окон DWM Mica при перезапусках, окно восстанавливает размеры мгновенно и без мерцания.
* **Архитектурная чистота:** Вынесли логику иконок, формы параметров и диалогов файлов в изолированные классы-контроллеры.
* **Модульная CMake-сборка:** Перенесли тесты в отдельную папку и упаковали ядро в общую объектную библиотеку `ModeFlowCore` (сокращение времени компиляции в 2 раза).

---

[1.0.0]: https://github.com/goloveshko/ModeFlow/releases/tag/v1.0.0
[0.9.0-rc]: https://github.com/goloveshko/ModeFlow/releases/tag/v0.9.0