#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <pthread.h>
#include "nativemsg.h"

#define FIFO_OUT "/tmp/dwm-pipn.fifo"
#define FIFO_IN  "/tmp/dwm-pipn-in.fifo"

static void *fifo_to_stdout(void *arg) {
    (void)arg;
    int fd = open(FIFO_OUT, O_RDWR);
    if (fd < 0) {
        fprintf(stderr, "[pipn_bridge] failed to open %s\n", FIFO_OUT);
        return NULL;
    }

    FILE *fifo = fdopen(fd, "r");
    if (!fifo) {
        fprintf(stderr, "[pipn_bridge] failed to fdopen %s\n", FIFO_OUT);
        close(fd);
        return NULL;
    }

    char line[4096];
    while (fgets(line, sizeof(line), fifo)) {
        size_t len = strlen(line);
        if (len > 0 && line[len - 1] == '\n')
            line[len - 1] = '\0';

        char msg[4096];
        int n = snprintf(msg, sizeof(msg), "{\"text\":\"%s\"}", line);
        if (n > 0)
            nativemsg_write((uint8_t *)msg, n);
    }

    fclose(fifo);
    return NULL;
}

static void *stdin_to_fifo(void *arg) {
    (void)arg;
    int fd = open(FIFO_IN, O_RDWR);
    if (fd < 0) {
        fprintf(stderr, "[pipn_bridge] failed to open %s\n", FIFO_IN);
        return NULL;
    }

    FILE *fifo = fdopen(fd, "w");
    if (!fifo) {
        fprintf(stderr, "[pipn_bridge] failed to fdopen %s\n", FIFO_IN);
        close(fd);
        return NULL;
    }

    uint32_t len;
    while (1) {
        uint8_t *buf = nativemsg_read(&len);
        if (!buf)
            break;

        fwrite(buf, 1, len, fifo);
        fputc('\n', fifo);
        fflush(fifo);
        free(buf);
    }

    fclose(fifo);
    return NULL;
}

int main(void) {
    mkfifo(FIFO_OUT, 0666);
    mkfifo(FIFO_IN,  0666);

    fprintf(stderr, "[pipn_bridge] started\n");

    pthread_t t1, t2;
    pthread_create(&t1, NULL, fifo_to_stdout, NULL);
    pthread_create(&t2, NULL, stdin_to_fifo, NULL);

    pthread_join(t1, NULL);
    pthread_join(t2, NULL);

    return 0;
}
