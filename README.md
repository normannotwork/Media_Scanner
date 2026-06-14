# Media Scanner Daemon

Высокопроизводительная системная служба для Linux, предназначенная для рекурсивного сканирования директорий на наличие медиафайлов (аудио, видео, изображения) и предоставления результатов через встроенный HTTP-сервер в формате JSON.

Архитектура сервера построена на базе epoll и неблокирующих сокетов, что позволяет выдерживать 10 000+. Журналирование интегрировано напрямую с syslog

---

## Требования

* ОС: Linux (из-за использования `epoll` и `systemd`)
* Компилятор с поддержкой **C++17** (GCC 8+ / Clang 7+)
* **CMake** 3.14 или выше
* Права `root` (sudo) для установки системной службы

---

## Сборка проекта

Проект использует CMake для управления зависимостями (nlohmann/json подтягивается автоматически).

1. Клонируйте репозиторий и перейдите в его директорию:
```bash
git clone <URL_репозитория>
cd Media_Scanner

```


2. Сгенерируйте файлы сборки и скомпилируйте релизную версию:
```bash
cmake -B build
cmake --build build --config Release

```

После успешной сборки исполняемый файл `media_scanner` появится в директории `build/`.

---

## Конфигурация

Служба настраивается через конфигурационный файл.

1. Скопируйте файл конфигурации `include/media_scanner.conf` и переместите туда, откуда служба будет подтягивать файл. Например: `/etc/media_scanner.conf`

```bash
sudo cp include/media_scanner.conf /etc/media_scanner.conf

```

2. Отредактируйте следующие параметры (обязательно используйте **абсолютный путь** для директории сканирования):
```ini
# Абсолютный путь к директории с медиафайлами
path = /var/media_storage

# Интервал пересканирования директории (в секундах)
interval = 60

# Включить HTTP-сервер (1 - включен, 0 - выключен)
http_mode = 1

# Порт для входящих HTTP-подключений
port = 8080

```
---

## Установка и запуск (Systemd)

Чтобы программа работала непрерывно в фоне и автоматически запускалась при старте системы, ее необходимо зарегистрировать как службу `systemd`.

**Шаг 1: Копирование бинарного файла**
Перенесите собранный файл в стандартную директорию для пользовательских бинарников:

```bash
sudo cp build/media_scanner /usr/local/bin/

```

**Шаг 2: Создание Systemd-юнита**
Скопируйте файл службы:

```bash
sudo cp include/media_scanner.service /etc/systemd/system/media_scanner.service
```

Отредактируйте следующие параметры:

```ini
[Unit]
Description=Media Scanner HTTP Service (epoll)
After=network.target

[Service]
Type=simple
User=root
ExecStart=/usr/local/bin/media_scanner --config /etc/media_scanner.conf
Restart=on-failure
RestartSec=5
# Увеличиваем лимит файловых дескрипторов для поддержки 10000+ сессий
LimitNOFILE=65535

[Install]
WantedBy=multi-user.target

```

**Шаг 3: Запуск службы**
Перезагрузите конфигурацию `systemd`, активируйте и запустите службу:

```bash
sudo systemctl daemon-reload
sudo systemctl enable media_scanner
sudo systemctl start media_scanner

```

Проверьте, что служба успешно запустилась:

```bash
sudo systemctl status media_scanner

```

---

## Мониторинг и Логирование

Служба не выводит данные в консоль, а отправляет их напрямую в системный журнал Linux через `syslog`.

Чтобы посмотреть логи в реальном времени, используйте утилиту `journalctl`:

```bash
sudo journalctl -u media_scanner -f

```

---

## Использование API

Когда служба запущена и HTTP-режим включен, вы можете получить список найденных медиафайлов с помощью простого GET-запроса.

Пример через `curl`:

```bash
curl http://localhost:8080/media_files

```

**Ожидаемый ответ (JSON):**

```json
{
  "audio": [
    "music/track1.mp3",
    "podcasts/episode2.wav"
  ],
  "images": [
    "vacation/photo.jpg",
    "design/logo.png"
  ],
  "video": [
    "movies/film.mp4"
  ]
}

```

*Примечание: Все пути в JSON-ответе являются относительными (отталкиваясь от базовой папки, указанной в конфиге).*
