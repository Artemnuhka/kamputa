#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <shadow.h>
#include <string.h>
#include <pwd.h>

#define CONFIG_FILE "/etc/kamputa.conf"

int is_russian = 0;

void check_locale() {
    char *lang = getenv("LANG");
    if (lang != NULL && strncmp(lang, "ru", 2) == 0) {
        is_russian = 1;
    }
}


int check_config(const char *username) {
    FILE *file = fopen(CONFIG_FILE, "r");
    if (!file) {
       
        return 0; 
    }

    char line[128];
    int allowed = 0;

    while (fgets(line, sizeof(line), file)) {
        
        line[strcspn(line, "\n")] = 0;
        
        if (strcmp(line, username) == 0) {
            allowed = 1;
            break;
        }
    }

    fclose(file);
    return allowed;
}

int main(int argc, char *argv[]) {
    check_locale();

    if (argc < 2) {
        if (is_russian) {
            fprintf(stderr, "Использование: kamputa <команда> [аргументы]\n");
        } else {
            fprintf(stderr, "Usage: kamputa <command> [arguments]\n");
        }
        return 1;
    }

    
    uid_t uid = getuid();
    struct passwd *pw = getpwuid(uid);
    if (!pw) {
        if (is_russian) perror("Не удалось получить данные пользователя");
        else perror("Failed to get user data");
        return 1;
    }

    
    if (!check_config(pw->pw_name)) {
        if (is_russian) {
            fprintf(stderr, "Пользователь %s отсутствует в %s. Доступ запрещен.\n", pw->pw_name, CONFIG_FILE);
        } else {
            fprintf(stderr, "User %s is not in %s. Access denied.\n", pw->pw_name, CONFIG_FILE);
        }
        return 1;
    }

    
    char *prompt = is_russian ? "[kamputa] Введите ваш passport: " : "[kamputa] Enter your passport: ";
    char *password = getpass(prompt);
    if (!password) return 1;

    
    struct spwd *sp = getspnam(pw->pw_name);
    if (!sp) {
        if (is_russian) fprintf(stderr, "Ошибка доступа к shadow (проверьте SUID флаг).\n");
        else fprintf(stderr, "Shadow access error (check SUID flag).\n");
        return 1;
    }

    
    char *encrypted = crypt(password, sp->sp_pwdp);
    if (strcmp(encrypted, sp->sp_pwdp) != 0) {
        if (is_russian) fprintf(stderr, "Неверный passport!\n");
        else fprintf(stderr, "Incorrect passport!\n");
        return 1;
    }


    if (setuid(0) != 0 || setgid(0) != 0) {
        if (is_russian) perror("Ошибка повышения привилегий");
        else perror("Privilege escalation error");
        return 1;
    }


    clearenv();
    setenv("PATH", "/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin", 1);
    setenv("USER", "root", 1);
    setenv("HOME", "/root", 1);

    
    execvp(argv[1], &argv[1]);

    if (is_russian) perror("Ошибка выполнения команды");
    else perror("Command execution error");
    return 1;
}