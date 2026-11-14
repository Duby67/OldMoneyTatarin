# OldMoneyTatarin Launcher

Minecraft-лаунчер на основе [PineconeMC](https://github.com/ElyPrismLauncher/Launcher), форка Prism Launcher. Платформа — Windows 10/11 x64; C++, Qt6 и CMake.

Локальная Release-сборка и установка проверены. Авторизация и игровые сценарии пока не проверялись. Версия продукта хранится в [VERSION](VERSION), патчноуты — в [CHANGELOG.md](CHANGELOG.md). Собственные релизы ещё не опубликованы.

## Сборка

После подготовки зависимостей:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build.ps1 -Jobs 6 -Install
```

Приложение появится в `dist/app/windows_msvc/elyprismlauncher.exe`. Бинарники собираются локально и исключены из Git; на GitHub хранятся код и документация.

## Документация

- [Разработка](docs/DEVELOPMENT.md) — окружение, сборка и обновление upstream.
- [Выпуск](docs/RELEASING.md) — версия и патчноуты.
- [AGENTS.md](AGENTS.md) — правила работы агентов.
- [LICENSE](LICENSE) и [COPYING.md](COPYING.md) — лицензия и уведомления об авторах.

## Дальнейшая работа

1. Проверить Ely.by и скины, Vanilla/Forge/NeoForge/Fabric, каталоги сборок, Java и настройки памяти.
2. Определить MVP: управляемый модпак, его синхронизацию и обновление лаунчера с собственного хостинга.
3. Подготовить оформление, установщик и первый выпуск после проверки условий распространения.

Реализация этих задач начинается по отдельному заданию.
