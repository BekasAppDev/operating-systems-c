#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <errno.h>
#include "config.h"
#include "util.h"

int main(int argc, char *argv[])
{
    int fpr;
    char c2c;
    char message[MSG_SIZE + 1];

    if (argc != 3)
    {
        const char *err = "Usage: ./a1.4-worker <fd_num> <character>\n";
        write(2, err, strlen(err));
        exit(1);
    }

    fpr = string_to_int(argv[1]);
    c2c = argv[2][0];

    signal(SIGINT, SIG_IGN);
    signal(SIGTERM, SIG_IGN);
    signal(SIGUSR1, SIG_IGN);
    signal(SIGUSR2, SIG_IGN);

    for (;;)
    {
        /* read exactly MSG_SIZE bytes for a task */
        ssize_t total_read = 0;
        while (total_read < MSG_SIZE)
        {
            ssize_t rcnt = read(0, message + total_read, MSG_SIZE - total_read);
            if (rcnt > 0)
            {
                total_read += rcnt;
            }
            else if (rcnt == 0)
            {
                break;
            }
            else if (rcnt == -1 && errno != EAGAIN && errno != EWOULDBLOCK)
            {
                perror("read");
                exit(1);
            }
        }

        /* no more tasks */
        if (total_read == 0)
            break;

        /* only proceed when a full MSG_SIZE message has been read */
        if (total_read < MSG_SIZE)
        {
            /* wait a little and retry in next loop iteration */
            usleep(2000);
            continue;
        }

        message[MSG_SIZE] = '\0';
        int start_offset = string_to_int(message);

        char buff[1024];
        int count = 0;
        size_t total_read_chunk = 0;
        off_t current_offset = (off_t)start_offset;

        for (;;)
        {
            size_t remaining = (size_t)CHUNK_SIZE - total_read_chunk;
            size_t chunk = remaining < sizeof(buff) ? remaining : sizeof(buff);

            ssize_t frcnt = pread(fpr, buff, chunk, current_offset);
            if (frcnt == 0)
                break;
            if (frcnt == -1)
            {
                const char *err = "Error reading input file\n";
                write(2, err, strlen(err));
                exit(1);
            }

            for (ssize_t idx = 0; idx < frcnt; idx++)
                if (buff[idx] == c2c)
                    count++;

            total_read_chunk += frcnt;
            current_offset += frcnt;

            if (total_read_chunk >= (size_t)CHUNK_SIZE)
                break;
        }

        /* write exactly MSG_SIZE bytes back to dispatcher */
        sprintf(message, "%0*d", MSG_SIZE, count);
        ssize_t written = 0;
        while (written < MSG_SIZE)
        {
            ssize_t wcnt = write(1, message + written, MSG_SIZE - written);
            if (wcnt > 0)
                written += wcnt;
        }
    }

    return 0;
}