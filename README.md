# Terminal-Render

Рендерит любое окно X11 прямо в терминал. Даунскейлит до 640x480 и выводит цветными ANSI-символами.

## Установка

**Через curl:**
```bash
curl -sSL https://raw.githubusercontent.com/tedex228/Terminal-Render/main/install.sh | bash
```

**Или скачать вручную:**
```bash
curl -L https://github.com/tedex228/Terminal-Render/releases/latest/download/terminal-render-linux-amd64 -o terminal-render
chmod +x terminal-render
sudo mv terminal-render /usr/local/bin/
```

**Сборка из исходников:**
```bash
git clone https://github.com/tedex228/Terminal-Render.git
cd Terminal-Render
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
sudo make install
```

Зависимости: `cmake g++ libx11-dev libxext-dev`

## Использование

```bash
terminal-render
```

Кликни по любому окну — его содержимое появится в терминале.

**Опции:**
- `-r 320x240` — разрешение вывода (по умолчанию 640x480)
- `-f` — захват всего экрана (без клика по окну)
- `-h` — справка

Выход: `Ctrl+C`

## Как это работает

1. Захват окна через X11/XShm (shared memory, быстро)
2. Даунскейл до 640x480 (box filter)
3. Конвертация пикселей в ANSI true-color escape-последовательности
4. Вывод в терминал с diff-based рендером (только изменённые строки)
