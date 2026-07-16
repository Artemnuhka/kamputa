<p align="center">
  <img src="kamputalogo.png" width="420" height="1040">
</p>

# WARNING!
!!! Windows version not ready !!!

# kamputa EN
!!! "kamputa" created by AI !!!

A custom, lightweight privilege escalation utility for Linux (an alternative to `sudo`/`doas`).  
Created as an independent component for the VorsentOS distribution (an upcoming project).  
kamputa was inspired by memes from users such as mixap52 and cursedlinuxuser.

## Features
- Written in pure C with no heavy dependencies.
- Checks access permissions via the `/etc/kamputa.conf` configuration file.
- Securely clears the environment before executing commands.

## Quick installation

You can download the ready-made self-extracting .run installer from the [Releases](https://github.com/Artemnuhka/kamputa/releases) page and run it:
```bash
sudo ./install_kamputa.run
```
OR
```bash
su -
./install_kamputa.run
```
# kamputa RU
!!! "kamputa" создана с помощью ИИ !!!

Кастомная, легковесная утилита для повышения привилегий в Linux (альтернатива sudo/doas).
Создана как независимый компонент для дистрибутива VorsentOS (будущий проект) 
kamputa создана по мему от таких людей как mixap52 и cursedlinuxuser.

## Особенности

- Написана на чистом C без тяжелых зависимостей.
- Проверяет доступ по файлу конфигурации /etc/kamputa.conf.
- Безопасная очистка окружения перед выполнением команд.

## Быстрая установка

Вы можете скачать готовый самораспаковывающийся установщик .run со страницы [Releases](https://github.com/Artemnuhka/kamputa/releases) и запустить его:
```bash
sudo ./install_kamputa.run
```
ИЛИ
```bash
su -
./install_kamputa.run
```  
## Сборка и установка из исходников || Building and installing from source
```bash
make
sudo make install
