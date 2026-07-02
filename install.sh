#!/bin/bash

if [ "$EUID" -ne 0 ]; then
  echo "Please run this installer as root (su - or sudo)"
  echo "Пожалуйста, запустите установщик с правами root (su - или sudo)"
  exit 1
fi

if ! command -v dialog &> /dev/null; then
    echo "Utility 'dialog' not found. Please install it before running the installer:"
    echo "Ubuntu/Debian: apt install dialog"
    echo "Arch Linux: pacman -S dialog"
    exit 1
fi

tempfile=$(mktemp 2>/dev/null) || tempfile=/tmp/kamputa_ans$$

dialog --clear --title "Language / Язык" \
       --menu "Choose your language / Выберите язык:" 10 50 2 \
       "RU" "Русский" \
       "EN" "English" 2> $tempfile

lang=$(cat $tempfile)
if [ -z "$lang" ]; then
    clear
    echo "Canceled / Отменено."
    rm -f $tempfile
    exit 0
fi

if [ "$lang" = "RU" ]; then
    T_MENU="Главное меню"
    T_ACTION="Что вы хотите сделать?"
    O_INSTALL="Установить kamputa"
    O_REMOVE="Удалить kamputa"
    T_CONFIRM="Подтверждение установки"
    M_CONFIRM="Вы действительно хотите установить kamputa на ваш компьютер?"
    T_USER_INPUT="Настройка пользователя"
    M_USER_ASK="Введите имя пользователя, которому разрешено использовать kamputa:"
    T_SYMLINK="Создание ссылок"
    M_SUDO="Создать симлинк для sudo?\n(При вводе 'sudo' будет запускаться kamputa)"
    M_DOAS="Создать симлинк для doas?\n(При вводе 'doas' будет запускаться kamputa)"
    M_SUCCESS="Установка kamputa успешно завершена!"
    M_REMOVED="kamputa и все её симлинки полностью удалены с компьютера."
    M_CANCEL="Действие отменено."
    L_YES="Да"
    L_NO="Нет"
else
    T_MENU="Main Menu"
    T_ACTION="What do you want to do?"
    O_INSTALL="Install kamputa"
    O_REMOVE="Remove kamputa"
    T_CONFIRM="Installation Confirmation"
    M_CONFIRM="Do you really want to install kamputa on your computer?"
    T_USER_INPUT="User Setup"
    M_USER_ASK="Enter the username allowed to use kamputa:"
    T_SYMLINK="Create Symlinks"
    M_SUDO="Create a symlink for sudo?\n(Typing 'sudo' will run kamputa)"
    M_DOAS="Create a symlink for doas?\n(Typing 'doas' will run kamputa)"
    M_SUCCESS="kamputa has been successfully installed!"
    M_REMOVED="kamputa and its symlinks have been completely removed."
    M_CANCEL="Action canceled."
    L_YES="Yes"
    L_NO="No"
fi

dialog --clear --title "$T_MENU" \
       --menu "$T_ACTION" 12 55 2 \
       "1" "$O_INSTALL" \
       "2" "$O_REMOVE" 2> $tempfile

action=$(cat $tempfile)
if [ -z "$action" ]; then
    clear
    echo "$M_CANCEL"
    rm -f $tempfile
    exit 0
fi

if [ "$action" = "2" ]; then
    rm -f /usr/local/bin/kamputa
    rm -f /etc/kamputa.conf
    [ -L /usr/local/bin/sudo ] && rm -f /usr/local/bin/sudo
    [ -L /usr/local/bin/doas ] && rm -f /usr/local/bin/doas
    
    dialog --clear --title "$T_MENU" --msgbox "$M_REMOVED" 8 50
    clear
    rm -f $tempfile
    exit 0
fi

if [ "$action" = "1" ]; then
    dialog --clear --title "$T_CONFIRM" \
           --yes-label "$L_YES" \
           --no-label "$L_NO" \
           --yesno "$M_CONFIRM" 8 55
    if [ $? -ne 0 ]; then
        clear
        echo "$M_CANCEL"
        rm -f $tempfile
        exit 0
    fi

    cp kamputa /usr/local/bin/
    chown root:root /usr/local/bin/kamputa
    chmod 4755 /usr/local/bin/kamputa
    
    if [ ! -f /etc/kamputa.conf ]; then
        touch /etc/kamputa.conf
        chmod 600 /etc/kamputa.conf
    fi

    DETECTED_USER="${SUDO_USER:-$USER}"
    if [ "$DETECTED_USER" = "root" ]; then
        DETECTED_USER=""
    fi

    dialog --clear --title "$T_USER_INPUT" \
           --inputbox "$M_USER_ASK" 8 60 "$DETECTED_USER" 2> $tempfile
    
    CHOSEN_USER=$(cat $tempfile)
    
    if [ -n "$CHOSEN_USER" ]; then
        if ! grep -q "^$CHOSEN_USER$" /etc/kamputa.conf; then
            echo "$CHOSEN_USER" >> /etc/kamputa.conf
        fi
    fi

    dialog --clear --title "$T_SYMLINK" \
           --yes-label "$L_YES" \
           --no-label "$L_NO" \
           --yesno "$M_SUDO" 8 60
    if [ $? -eq 0 ]; then
        ln -sf /usr/local/bin/kamputa /usr/local/bin/sudo
    fi

    dialog --clear --title "$T_SYMLINK" \
           --yes-label "$L_YES" \
           --no-label "$L_NO" \
           --yesno "$M_DOAS" 8 60
    if [ $? -eq 0 ]; then
        ln -sf /usr/local/bin/kamputa /usr/local/bin/doas
    fi

    dialog --clear --title "$T_MENU" --msgbox "$M_SUCCESS" 8 50
    clear
fi

rm -f $tempfile