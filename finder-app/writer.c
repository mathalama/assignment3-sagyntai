#include <stdio.h>
#include <stdlib.h>
#include <syslog.h>
#include <errno.h>
#include <string.h>

int main(int argc, char *argv[]) {
    // 1. Инициализация логирования с параметром LOG_USER
    openlog("writer-a2", LOG_PID, LOG_USER);

    // 2. Проверка наличия двух аргументов (путь к файлу и строка)
    if (argc != 3) {
        syslog(LOG_ERR, "Error: Two arguments required. Usage: %s <file> <string>", argv[0]);
        closelog();
        return 1;
    }

    const char *writefile = argv[1];
    const char *writestr = argv[2];

    // 3. Запись LOG_DEBUG сообщения
    syslog(LOG_DEBUG, "Writing %s to %s", writestr, writefile);

    // 4. Попытка открыть и записать файл
    FILE *fp = fopen(writefile, "w");
    if (fp == NULL) {
        syslog(LOG_ERR, "Failed to open file %s: %s", writefile, strerror(errno));
        closelog();
        return 1;
    }

    if (fputs(writestr, fp) == EOF) {
        syslog(LOG_ERR, "Failed to write to file %s: %s", writefile, strerror(errno));
        fclose(fp);
        closelog();
        return 1;
    }

    fclose(fp);
    closelog();
    return 0;
}