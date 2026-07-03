#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pwd.h>
#include <shadow.h>
#include <crypt.h>
#include <sys/types.h>
#include <errno.h>
#include <stdbool.h>

#define CONF_PATH "/etc/kamputa.conf"
#define VERSION "1.3.0"
#define PASS_SIZE 256

int is_ru = 0;

static void print_usage(void)
{
    if (is_ru) {
        printf("kamputa v" VERSION "\n");
        printf("Применение: kamputa [флаги] команда [аргументы]\n\n");
        printf("Флаги:\n");
        printf("  -h, --help         Показать эту справку\n");
        printf("  -V, --version      Показать версию\n");
        printf("  --list             Список разрешенных пользователей\n");
        printf("  -env, -E           Сохранить переменные окружения текущего пользователя\n");
        printf("  -val, -v           Только проверить паспорт (без выполнения команды)\n");
        printf("  -usr, -u <user>    Запустить команду от имени указанного пользователя\n");
    } else {
        printf("kamputa v" VERSION "\n");
        printf("Usage: kamputa [flags] command [arguments]\n\n");
        printf("Flags:\n");
        printf("  -h, --help         Show this help and exit\n");
        printf("  -V, --version      Show version and exit\n");
        printf("  --list             List allowed users\n");
        printf("  -env, -E           Preserve current user's environment variables\n");
        printf("  -val, -v           Validate passport only (do not execute command)\n");
        printf("  -usr, -u <user>    Run the command as specified user\n");
    }
}

static void print_version(void)
{
    printf("kamputa v" VERSION "\n");
}

static void print_list(void)
{
    FILE *fp = fopen(CONF_PATH, "r");
    if (!fp) {
        if (errno == ENOENT) {
            fprintf(stderr, is_ru ? "Файл %s не найден.\n" : "File %s not found.\n", CONF_PATH);
        } else {
            perror(is_ru ? "Ошибка открытия файла конфигурации" : "Error opening config file");
        }
        return;
    }

    if (is_ru)
        printf("Разрешенные пользователи (%s):\n", CONF_PATH);
    else
        printf("Allowed users (%s):\n", CONF_PATH);

    char line[256];
    int found = 0;
    while (fgets(line, sizeof(line), fp)) {
        line[strcspn(line, "\n")] = 0;
        if (line[0] != '\0') {
            printf("  %s\n", line);
            found = 1;
        }
    }
    if (!found)
        printf(is_ru ? "  (пусто)\n" : "  (empty)\n");

    fclose(fp);
}

void sanitize_env(void)
{
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

int check_config(const char *username)
{
    FILE *fp = fopen(CONF_PATH, "r");
    if (!fp)
        return 0;

    char line[256];
    while (fgets(line, sizeof(line), fp)) {
        size_t len = strlen(line);
        if (len > 0 && line[len - 1] == '\n') {
            line[len - 1] = '\0';
        }
        /* Skip lines that might be truncated */
        if (!(len < sizeof(line) - 1 || (len > 0 && line[len - 1] == '\n'))) {
            continue;
        }
        if (strcmp(line, username) == 0) {
            fclose(fp);
            return 1;
        }
    }
    fclose(fp);
    return 0;
}

static bool is_flag(const char *arg)
{
    return arg[0] == '-';
}

int main(int argc, char *argv[])
{
    char *env_lang = getenv("LANG");
    is_ru = (env_lang && strncmp(env_lang, "ru", 2) == 0) ? 1 : 0;

    /* Check for flags that don't require authentication */
    if (argc == 2) {
        if (strcmp(argv[1], "-V") == 0 || strcmp(argv[1], "--version") == 0) {
            print_version();
            return 0;
        }
        if (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) {
            print_usage();
            return 0;
        }
        if (strcmp(argv[1], "--list") == 0) {
            print_list();
            return 0;
        }
    }

    /* Allow --list even when combined with -E or other non-op flags */
    int i;
    int has_list_flag = 0;
    int has_help_flag = 0;
    int has_version_flag = 0;
    char *first_non_flag = NULL;
    int first_non_flag_idx = -1;
    for (i = 1; i < argc; i++) {
        if (!is_flag(argv[i])) {
            first_non_flag = argv[i];
            first_non_flag_idx = i;
            break;
        }
        if (strcmp(argv[i], "--list") == 0) has_list_flag = 1;
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) has_help_flag = 1;
        if (strcmp(argv[i], "-V") == 0 || strcmp(argv[i], "--version") == 0) has_version_flag = 1;
    }

    if (has_help_flag) {
        print_usage();
        return 0;
    }
    if (has_version_flag) {
        print_version();
        return 0;
    }
    if (has_list_flag) {
        if (first_non_flag) {
            fprintf(stderr, is_ru ? "Флаг --list нельзя совмещать с командой.\n" : "The --list flag cannot be combined with a command.\n");
            return 1;
        }
        print_list();
        return 0;
    }

    /* Determine the current user and check config */
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

    /* Parse flags */
    int keep_env = 0;
    int validate_only = 0;
    int exit_after_list = 0;
    char *target_user = "root";
    int arg_offset = 1;

    while (arg_offset < argc && is_flag(argv[arg_offset])) {
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
        } else if (strcmp(argv[arg_offset], "--list") == 0) {
            exit_after_list = 1;
            arg_offset++;
        } else if (strcmp(argv[arg_offset], "-h") == 0 || strcmp(argv[arg_offset], "--help") == 0) {
            print_usage();
            return 0;
        } else if (strcmp(argv[arg_offset], "-V") == 0 || strcmp(argv[arg_offset], "--version") == 0) {
            print_version();
            return 0;
        } else {
            fprintf(stderr, is_ru ? "Неизвестный флаг: %s\n" : "Unknown flag: %s\n", argv[arg_offset]);
            return 1;
        }
    }

    if (exit_after_list) {
        if (arg_offset < argc) {
            fprintf(stderr, is_ru ? "Флаг --list нельзя совмещать с командой.\n" : "The --list flag cannot be combined with a command.\n");
            return 1;
        }
        print_list();
        return 0;
    }

    if (!validate_only && arg_offset >= argc) {
        print_usage();
        return 0;
    }

    /* Authenticate user using shadow password */
    struct spwd *sp = getspnam(pw->pw_name);
    if (!sp) {
        fprintf(stderr, is_ru ? "Ошибка доступа к shadow (проверьте SUID флаг).\n" : "Shadow access error (check SUID flag).\n");
        return 1;
    }

    char pass[PASS_SIZE];
    char *ret = getpass(is_ru ? "[kamputa] Введите ваш паспорт: " : "[kamputa] Enter your passport: ");
    if (!ret) {
        fprintf(stderr, is_ru ? "Ошибка чтения пароля.\n" : "Error reading password.\n");
        return 1;
    }
    strncpy(pass, ret, PASS_SIZE - 1);
    pass[PASS_SIZE - 1] = '\0';

    char *encrypted = crypt(pass, sp->sp_pwdp);
    if (!encrypted) {
        memset(pass, 0, sizeof(pass));
        fprintf(stderr, is_ru ? "Ошибка шифрования пароля.\n" : "Password encryption error.\n");
        return 1;
    }

    memset(pass, 0, sizeof(pass));

    if (strcmp(encrypted, sp->sp_pwdp) != 0) {
        fprintf(stderr, is_ru ? "Неверный паспорт.\n" : "Incorrect passport.\n");
        return 1;
    }

    if (validate_only) {
        return 0;
    }

    /* Lookup target user */
    struct passwd *tpw = getpwnam(target_user);
    if (!tpw) {
        fprintf(stderr, is_ru ? "Целевой пользователь %s не найден.\n" : "Target user %s not found.\n", target_user);
        return 1;
    }

    /* Privilege escalation */
    if (setgid(tpw->pw_gid) != 0 || setuid(tpw->pw_uid) != 0) {
        fprintf(stderr, is_ru ? "Ошибка смены привилегий.\n" : "Privilege escalation error.\n");
        return 1;
    }

    /* Sanitize environment unless keep_env is set */
    if (!keep_env) {
        sanitize_env();
        setenv("HOME", tpw->pw_dir, 1);
        setenv("USER", tpw->pw_name, 1);
        setenv("LOGNAME", tpw->pw_name, 1);
    }

    /* Execute the command */
    execvp(argv[arg_offset], &argv[arg_offset]);
    perror(is_ru ? "Ошибка выполнения команды" : "Command execution error");
    return 1;
}
