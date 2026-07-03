#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pwd.h>
#include <shadow.h>
#include <crypt.h>
#include <sys/types.h>

#define CONF_PATH "/etc/kamputa.conf"

int is_ru = 0;

void sanitize_env() {
    char *lang = getenv("LANG");
    char *term = getenv("TERM");
    
    char *saved_lang = lang ? strdup(lang) : NULL;
    char *saved_term = term ? strdup(term) : NULL;
    
    clearenv();
    
    setenv("PATH", "/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin", 1);
    
    if (saved_term) {
        setenv("TERM", saved_term, 1);
        free(saved_term);
    } else {
        setenv("TERM", "xterm-256color", 1);
    }
    
    if (saved_lang) {
        setenv("LANG", saved_lang, 1);
        free(saved_lang);
    }
}

int check_config(const char *username) {
    FILE *fp = fopen(CONF_PATH, "r");
    if (!fp) return 0;

    char line[128];
    while (fgets(line, sizeof(line), fp)) {
        line[strcspn(line, "\n")] = 0;
        if (strcmp(line, username) == 0) {
            fclose(fp);
            return 1;
        }
    }
    fclose(fp);
    return 0;
}

int main(int argc, char *argv[]) {
    char *env_lang = getenv("LANG");
    if (env_lang && strncmp(env_lang, "ru", 2) == 0) {
        is_ru = 1;
    }

    uid_t uid = getuid();
    struct passwd *pw = getpwuid(uid);
    if (!pw) {
        fprintf(stderr, is_ru ? "Ошибка: Не удалось определить пользователя.\n" : "Error: Failed to determine user.\n");
        return 1;
    }

    if (!check_config(pw->pw_name)) {
        fprintf(stderr, is_ru ? "Пользователь %s отсутствует в %s. Доступ запрещен.\n" : "User %s is not in %s. Access denied.\n", pw->pw_name, CONF_PATH);
        return 1;
    }

    int keep_env = 0;
    int validate_only = 0;
    char *target_user = "root";
    int arg_offset = 1;

    while (arg_offset < argc && argv[arg_offset][0] == '-') {
        if (strcmp(argv[arg_offset], "-env") == 0 || strcmp(argv[arg_offset], "-E") == 0) {
            keep_env = 1;
            arg_offset++;
        } else if (strcmp(argv[arg_offset], "-val") == 0 || strcmp(argv[arg_offset], "-v") == 0) {
            validate_only = 1;
            arg_offset++;
        } else if (strcmp(argv[arg_offset], "-usr") == 0 || strcmp(argv[arg_offset], "-u") == 0) {
            if (arg_offset + 1 < argc) {
                target_user = argv[arg_offset + 1];
                arg_offset += 2;
            } else {
                fprintf(stderr, is_ru ? "Ошибка: Флаг -usr требует указания пользователя.\n" : "Error: The -usr flag requires a username.\n");
                return 1;
            }
        } else {
            arg_offset++;
        }
    }

    if (!validate_only && arg_offset >= argc) {
        if (is_ru) {
            printf("kamputa v1.2.0\n");
            printf("Применение: kamputa [флаги] команда [аргументы]\n\n");
            printf("Флаги:\n");
            printf("  -env, -E      Сохранить переменные окружения текущего пользователя\n");
            printf("  -val, -v      Только проверить паспорт (без выполнения команды)\n");
            printf("  -usr, -u      Запустить команду от имени указанного пользователя\n");
        } else {
            printf("kamputa v1.2.0\n");
            printf("Usage: kamputa [flags] command [arguments]\n\n");
            printf("Flags:\n");
            printf("  -env, -E      Preserve current user's environment variables\n");
            printf("  -val, -v      Validate passport only (do not execute command)\n");
            printf("  -usr, -u      Run the command as the specified user\n");
        }
        return 0;
    }

    struct spwd *sp = getspnam(pw->pw_name);
    if (!sp) {
        fprintf(stderr, is_ru ? "Ошибка доступа к shadow (проверьте SUID флаг).\n" : "Shadow access error (check SUID flag).\n");
        return 1;
    }

    char *pass = getpass(is_ru ? "[kamputa] Введите ваш паспорт: " : "[kamputa] Enter your passport: ");
    char *encrypted = crypt(pass, sp->sp_pwdp);
    
    memset(pass, 0, strlen(pass));

    if (strcmp(encrypted, sp->sp_pwdp) != 0) {
        fprintf(stderr, is_ru ? "Неверный паспорт.\n" : "Incorrect passport.\n");
        return 1;
    }

    if (validate_only) {
        return 0;
    }

    struct passwd *tpw = getpwnam(target_user);
    if (!tpw) {
        fprintf(stderr, is_ru ? "Целевой пользователь %s не найден.\n" : "Target user %s not found.\n", target_user);
        return 1;
    }

    if (setgid(tpw->pw_gid) != 0 || setuid(tpw->pw_uid) != 0) {
        fprintf(stderr, is_ru ? "Ошибка смены привилегий.\n" : "Privilege escalation error.\n");
        return 1;
    }

    if (!keep_env) {
        sanitize_env();
        setenv("HOME", tpw->pw_dir, 1);
        setenv("USER", tpw->pw_name, 1);
        setenv("LOGNAME", tpw->pw_name, 1);
    }

    execvp(argv[arg_offset], &argv[arg_offset]);
    perror(is_ru ? "Ошибка выполнения команды" : "Command execution error");
    return 1;
}