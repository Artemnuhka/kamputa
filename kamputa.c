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
#include <syslog.h>
#include <termios.h>
#include <sys/stat.h>
#include <time.h>

#define CONF_PATH "/etc/kamputa.conf"
#define VERSION "1.4.0"
#define PASS_SIZE 256
#define SETTINGS_FILE "/etc/kamputa/settings"
#define AUTH_CACHE_DIR "/var/run/kamputa"

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
        printf("  -S, --set          Настройки kamputa\n");
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
        printf("  -S, --set          Kamputa settings\n");
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

static char *my_getpass(const char *prompt, char *buf, size_t size)
{
    FILE *tty = fopen("/dev/tty", "r+");
    if (!tty)
        return NULL;

    struct termios old, new;
    tcgetattr(fileno(tty), &old);
    new = old;
    new.c_lflag &= ~ECHO;
    tcsetattr(fileno(tty), TCSAFLUSH, &new);

    fputs(prompt, tty);
    fflush(tty);

    if (!fgets(buf, size, tty)) {
        tcsetattr(fileno(tty), TCSAFLUSH, &old);
        fclose(tty);
        return NULL;
    }

    tcsetattr(fileno(tty), TCSAFLUSH, &old);
    fclose(tty);

    size_t len = strlen(buf);
    if (len > 0 && buf[len - 1] == '\n')
        buf[len - 1] = '\0';

    return buf;
}

static void filter_dangerous_env(void)
{
    static const char *blocklist[] = {
        "LD_PRELOAD", "LD_LIBRARY_PATH", "LD_AUDIT",
        "LD_DEBUG", "GCONV_PATH", "GCONV_MODULES",
        "HOSTALIASES", "LOCPATH", NULL
    };
    for (int i = 0; blocklist[i]; i++)
        unsetenv(blocklist[i]);
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

static int read_setting(const char *key, int def)
{
    FILE *fp = fopen(SETTINGS_FILE, "r");
    if (!fp) return def;

    char line[128];
    int value = def;
    size_t klen = strlen(key);

    while (fgets(line, sizeof(line), fp)) {
        if (strncmp(line, key, klen) == 0 && line[klen] == '=') {
            value = atoi(line + klen + 1);
            break;
        }
    }
    fclose(fp);
    return value;
}

static void write_setting(const char *key, int value)
{
    char tmpfile[288];
    snprintf(tmpfile, sizeof(tmpfile), "%s.tmp", SETTINGS_FILE);

    FILE *in = fopen(SETTINGS_FILE, "r");
    FILE *out = fopen(tmpfile, "w");
    if (!out) return;

    int found = 0;
    if (in) {
        char line[128];
        size_t klen = strlen(key);
        while (fgets(line, sizeof(line), in)) {
            if (strncmp(line, key, klen) == 0 && line[klen] == '=') {
                fprintf(out, "%s=%d\n", key, value);
                found = 1;
            } else {
                fputs(line, out);
            }
        }
        fclose(in);
    }

    if (!found)
        fprintf(out, "%s=%d\n", key, value);

    fclose(out);
    rename(tmpfile, SETTINGS_FILE);
}

static int check_auth_cache(uid_t uid, int timeout_min)
{
    if (timeout_min <= 0) return 0;

    char path[288];
    snprintf(path, sizeof(path), "%s/%d", AUTH_CACHE_DIR, uid);

    struct stat st;
    if (stat(path, &st) != 0) return 0;
    if (st.st_uid != uid) return 0;

    time_t now = time(NULL);
    return (now - st.st_mtime < timeout_min * 60) ? 1 : 0;
}

static void write_auth_cache(uid_t uid)
{
    char path[288];
    snprintf(path, sizeof(path), "%s/%d", AUTH_CACHE_DIR, uid);

    mkdir(AUTH_CACHE_DIR, 0700);
    FILE *fp = fopen(path, "w");
    if (fp) {
        fprintf(fp, "%ld", (long)time(NULL));
        fclose(fp);
    }
}

static void settings_timeout(void)
{
    int current = read_setting("timeout", 5);
    char input[16];

    if (is_ru) {
        printf("Тайм-аут пароля\n");
        printf("Текущий: %d мин.\n", current);
        printf("Введите новое значение в минутах (0 = отключить): ");
    } else {
        printf("Password timeout\n");
        printf("Current: %d min.\n", current);
        printf("Enter new value in minutes (0 = disable): ");
    }
    fflush(stdout);

    struct termios told, tnew;
    int have_t = (tcgetattr(STDIN_FILENO, &told) == 0);
    if (have_t) {
        tnew = told;
        tnew.c_lflag |= ICANON | ECHO;
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &tnew);
    }

    if (fgets(input, sizeof(input), stdin)) {
        size_t l = strlen(input);
        if (l > 0 && input[l - 1] == '\n') input[l - 1] = '\0';
        int val = atoi(input);
        if (val >= 0) {
            write_setting("timeout", val);
            if (is_ru)
                printf("Тайм-аут установлен: %d мин.\n", val);
            else
                printf("Timeout set: %d min.\n", val);
        }
    }

    if (have_t)
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &told);
}

static int settings_uninstall(void)
{
    char input[64];

    printf("\n%s\n", is_ru
        ? "/!\\ ВНИМАНИЕ /!\\"
        : "/!\\ WARNING /!\\");
    printf("\n%s\n", is_ru
        ? "Это безвозвратно удалит kamputa с вашей системы."
        : "This will permanently remove kamputa from your system.");
    printf("\n%s: ", is_ru
        ? "Напишите \"Да, подтверждаю\" для продолжения"
        : "Type \"Yes, confirm\" to proceed");
    fflush(stdout);

    struct termios t2old, t2new;
    int have_t2 = (tcgetattr(STDIN_FILENO, &t2old) == 0);
    if (have_t2) {
        t2new = t2old;
        t2new.c_lflag |= ICANON | ECHO;
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &t2new);
    }

    if (!fgets(input, sizeof(input), stdin)) {
        if (have_t2) tcsetattr(STDIN_FILENO, TCSAFLUSH, &t2old);
        return 0;
    }
    {
        size_t l = strlen(input);
        if (l > 0 && input[l - 1] == '\n') input[l - 1] = '\0';
    }

    if (have_t2) tcsetattr(STDIN_FILENO, TCSAFLUSH, &t2old);

    int confirmed = 0;
    if (is_ru) {
        if (strcmp(input, "Да, подтверждаю") == 0)
            confirmed = 1;
    } else {
        if (strcmp(input, "Yes, confirm") == 0)
            confirmed = 1;
    }

    if (!confirmed) {
        printf("\n%s\n", is_ru ? "Отменено." : "Canceled.");
        return 0;
    }

    unlink("/usr/local/bin/kamputa");
    unlink(SETTINGS_FILE);
    unlink("/etc/kamputa.conf");

    char link_target[256];
    ssize_t n = readlink("/usr/local/bin/sudo", link_target, sizeof(link_target) - 1);
    if (n > 0) {
        link_target[n] = '\0';
        if (strstr(link_target, "kamputa"))
            unlink("/usr/local/bin/sudo");
    }
    n = readlink("/usr/local/bin/doas", link_target, sizeof(link_target) - 1);
    if (n > 0) {
        link_target[n] = '\0';
        if (strstr(link_target, "kamputa"))
            unlink("/usr/local/bin/doas");
    }

    printf("\n%s\n", is_ru ? "kamputa удалена." : "kamputa removed.");
    return 1;
}

static void show_settings_menu(void)
{
    int selected = 0;
    int items = 2;
    int running = 1;

    struct termios old, new;
    int has_termios = (tcgetattr(STDIN_FILENO, &old) == 0);
    if (has_termios) {
        new = old;
        new.c_lflag &= ~(ICANON | ECHO);
        new.c_cc[VMIN] = 1;
        new.c_cc[VTIME] = 0;
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &new);
    }

    while (running) {
        printf("\033[2J\033[H");

        if (is_ru)
            printf("=== Настройки kamputa ===\n\n");
        else
            printf("=== Kamputa Settings ===\n\n");

        int timeout = read_setting("timeout", 5);
        if (is_ru)
            printf("%s 1) Тайм-аут пароля [%d мин.]\n", selected == 0 ? ">" : " ", timeout);
        else
            printf("%s 1) Password timeout [%d min.]\n", selected == 0 ? ">" : " ", timeout);

        if (is_ru)
            printf("%s 2) Удалить kamputa\n", selected == 1 ? ">" : " ");
        else
            printf("%s 2) Uninstall kamputa\n", selected == 1 ? ">" : " ");

        if (is_ru)
            printf("\n[↑↓ — навигация, Enter — выбор, q — выход]");
        else
            printf("\n[↑↓ to navigate, Enter to select, q to quit]");
        fflush(stdout);

        char c;
        if (read(STDIN_FILENO, &c, 1) <= 0)
            break;

        if (c == 'q' || c == 'Q') {
            running = 0;
            break;
        }

        if (c == '\033') {
            char seq[2];
            if (read(STDIN_FILENO, &seq[0], 1) <= 0) break;
            if (seq[0] == '[') {
                if (read(STDIN_FILENO, &seq[1], 1) <= 0) break;
                if (seq[1] == 'A')
                    selected = (selected - 1 + items) % items;
                else if (seq[1] == 'B')
                    selected = (selected + 1) % items;
            }
        } else if (c == '\n' || c == '\r') {
            if (selected == 0) {
                settings_timeout();
                printf("\n%s", is_ru ? "Нажмите Enter..." : "Press Enter...");
                fflush(stdout);
                char dummy;
                while (read(STDIN_FILENO, &dummy, 1) <= 0);
            } else if (selected == 1) {
                if (settings_uninstall()) {
                    running = 0;
                } else {
                    printf("\n%s", is_ru ? "Нажмите Enter..." : "Press Enter...");
                    fflush(stdout);
                    char dummy;
                    while (read(STDIN_FILENO, &dummy, 1) <= 0);
                }
            }
        }
    }

    if (has_termios)
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &old);

    printf("\033[2J\033[H");
}

int main(int argc, char *argv[])
{
    char *env_lang = getenv("LANG");
    is_ru = (env_lang && strncmp(env_lang, "ru", 2) == 0) ? 1 : 0;

    /* Open settings menu immediately if requested */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-S") == 0 || strcmp(argv[i], "-set") == 0 || strcmp(argv[i], "--set") == 0) {
            show_settings_menu();
            return 0;
        }
    }

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

    openlog("kamputa", LOG_PID | LOG_CONS, LOG_AUTHPRIV);

    /* Check auth cache (password timeout feature) */
    int auth_timeout = read_setting("timeout", 5);
    if (check_auth_cache(uid, auth_timeout)) {
        syslog(LOG_DEBUG, "User %s authenticated from cache (uid %d)", pw->pw_name, uid);
    } else {
        char hostname[256] = "localhost";
        gethostname(hostname, sizeof(hostname));
        hostname[sizeof(hostname) - 1] = '\0';

        char prompt[512];
        snprintf(prompt, sizeof(prompt), is_ru ? "[kamputa: %s@%s] Паспорт: " : "[kamputa: %s@%s] Passport: ", pw->pw_name, hostname);

        char pass[PASS_SIZE];
        if (!my_getpass(prompt, pass, sizeof(pass))) {
            fprintf(stderr, is_ru ? "Ошибка чтения пароля.\n" : "Error reading password.\n");
            return 1;
        }

        char *encrypted = crypt(pass, sp->sp_pwdp);
        if (!encrypted) {
            memset(pass, 0, sizeof(pass));
            fprintf(stderr, is_ru ? "Ошибка шифрования пароля.\n" : "Password encryption error.\n");
            return 1;
        }

        memset(pass, 0, sizeof(pass));

        if (strcmp(encrypted, sp->sp_pwdp) != 0) {
            fprintf(stderr, is_ru ? "Неверный паспорт.\n" : "Incorrect passport.\n");
            syslog(LOG_WARNING, "Failed authentication for %s from uid %d", pw->pw_name, uid);
            return 1;
        }

        write_auth_cache(uid);
        syslog(LOG_NOTICE, "User %s authenticated with password", pw->pw_name);
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
    if (setgid(tpw->pw_gid) != 0) {
        fprintf(stderr, is_ru ? "Ошибка смены группы.\n" : "Group change error.\n");
        return 1;
    }
    if (setuid(tpw->pw_uid) != 0) {
        fprintf(stderr, is_ru ? "Ошибка смены пользователя.\n" : "User change error.\n");
        return 1;
    }
    /* Verify that privileges are fully dropped */
    if (tpw->pw_uid != 0 && setuid(0) == 0) {
        fprintf(stderr, is_ru ? "Ошибка: не удалось сбросить привилегии.\n" : "Error: failed to drop privileges.\n");
        return 1;
    }

    /* Sanitize environment unless keep_env is set */
    if (!keep_env) {
        sanitize_env();
        setenv("HOME", tpw->pw_dir, 1);
        setenv("USER", tpw->pw_name, 1);
        setenv("LOGNAME", tpw->pw_name, 1);
    } else {
        filter_dangerous_env();
    }

    syslog(LOG_INFO, "User %s executed command as %s: %s", pw->pw_name, target_user, argv[arg_offset]);

    /* Execute the command */
    execvp(argv[arg_offset], &argv[arg_offset]);
    perror(is_ru ? "Ошибка выполнения команды" : "Command execution error");
    return 1;
}
