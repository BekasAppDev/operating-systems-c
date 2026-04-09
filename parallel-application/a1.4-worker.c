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

    /* workers ignore the signals associated with frontend-dispatcher communication */
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
                const char *err = "read";
                write(2, err, strlen(err));
                exit(1);
            }
        }

        /* no more tasks */
        if (total_read == 0)
            break;

        /* only proceed when a full MSG_SIZE message has been read */
        if (total_read < MSG_SIZE)
            continue;

        /* artificial delay */
        sleep(2);

        /* format and get the offset */
        message[MSG_SIZE] = '\0';
        int start_offset = string_to_int(message);

        /* read exactly CHUNK_SIZE bytes from designated offset */
        char buff[1024];
        int count = 0;
        size_t total_read_chunk = 0;
        off_t current_offset = (off_t)start_offset;

        for (;;)
        {
            size_t remaining = (size_t)CHUNK_SIZE - total_read_chunk;
            size_t chunk = remaining;

            ssize_t rcnt = pread(fpr, buff, chunk, current_offset);

            if (rcnt == 0)
            {
                break;
            }
            else if (rcnt == -1)
            {
                const char *err = "Error reading input file\n";
                write(2, err, strlen(err));
                exit(1);
            }

            /* ensure we do not exceed chunk */
            if (total_read_chunk + rcnt > (size_t)CHUNK_SIZE)
            {
                rcnt = (size_t)CHUNK_SIZE - total_read_chunk;
            }

            /* count occurences of c2c */
            for (ssize_t idx = 0; idx < rcnt; idx++)
                if (buff[idx] == c2c)
                    count++;

            total_read_chunk += rcnt;
            current_offset += rcnt;

            if (total_read_chunk >= (size_t)CHUNK_SIZE)
                break;
        }

        /* write exactly MSG_SIZE bytes back to dispatcher */
        sprintf(message, "%0*d", MSG_SIZE, count);
        ssize_t total_written = 0;
        while (total_written < MSG_SIZE)
        {
            ssize_t wcnt = write(1, message + total_written, MSG_SIZE - total_written);
            if (wcnt > 0)
                total_written += wcnt;
        }
    }

    return 0;
}