# Changelog - ModeFlow v{VERSION}

### 🇬🇧 English
Welcome to the official **v{VERSION}** release of **ModeFlow**! This milestone release introduces a modernized Windows 11 Fluent experience, a robust background execution model, and seamless GitHub Releases integration.

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
Добро пожаловать в официальный релиз **ModeFlow v{VERSION}**! Этот релиз приносит нативный Fluent-дизайн Windows 11, легковесную архитектуру фонового сервиса и интеграцию с GitHub Releases API.

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