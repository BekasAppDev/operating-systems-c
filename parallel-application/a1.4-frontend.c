#define _GNU_SOURCE
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/wait.h>
#include <errno.h>
#include <fcntl.h>
#include "config.h"

int pipe_from_disp[2];

int main(int argc, char *argv[])
{
    if (argc != 3)
    {
        const char *msg = "Usage: ./a1.4-frontend <input_file> <character>\n";
        write(2, msg, strlen(msg));
        exit(1);
    }

    /* installing pipe from dispatcher */
    if (pipe2(pipe_from_disp, O_NONBLOCK) == -1)
    {
        const char *msg = "pipe error\n";
        write(2, msg, strlen(msg));
        exit(1);
    }

    pid_t p = fork();
    if (p < 0)
    {
        const char *msg = "fork error\n";
        write(2, msg, strlen(msg));
        exit(1);
    }
    else if (p == 0)
    {
        /* child => dispatcher */
        close(pipe_from_disp[0]);

        dup2(pipe_from_disp[1], 1);

        char *args[] = {"./a1.4-dispatcher", argv[1], argv[2], NULL};
        execv(args[0], args);

        const char *msg = "execv error\n";
        write(2, msg, strlen(msg));
        exit(1);
    }
    else
    {
        /* parent => frontend */
        close(pipe_from_disp[1]);

        /* set stdin to non-blocking mode using fcntl */
        int flags = fcntl(0, F_GETFL, 0);
        fcntl(0, F_SETFL, flags | O_NONBLOCK);

        char cmd[CMD_BUF];
        char buf[256];

        /* non-blocking state of frontend */
        while (1)
        {
            /* read user command */
            ssize_t n = read(0, cmd, CMD_BUF - 1);
            if (n > 0)
            {
                cmd[n] = '\0';

                if (cmd[0] == CMD_ADD)
                    kill(p, SIGUSR1);
                else if (cmd[0] == CMD_REM)
                    kill(p, SIGUSR2);
                else if (cmd[0] == CMD_INF)
                    kill(p, SIGINT);
                else if (cmd[0] == CMD_PRG)
                    kill(p, SIGTERM);
                else if (cmd[0] != '\n')
                {
                    const char *msg = "Unknown command\n";
                    write(1, msg, strlen(msg));
                }
            }

            ssize_t total_read = 0;

            /* hear from dispatcher */
            while (1)
            {
                ssize_t r = read(pipe_from_disp[0], buf + total_read, sizeof(buf) - 1 - total_read);

                if (r > 0)
                {
                    total_read += r;
                }
                else if (r == -1 && (errno == EAGAIN || errno == EWOULDBLOCK))
                {
                    break;
                }
                else if (r == 0)
                {
                    break;
                }
                else
                {
                    const char *msg = "read error\n";
                    write(2, msg, strlen(msg));
                    exit(1);
                }
            }

            if (total_read > 0)
            {
                buf[total_read] = '\0';
                write(1, buf, total_read);
            }

            /* check dispatcher termination */
            int status;
            pid_t dead_pid = waitpid(p, &status, WNOHANG);
            if (dead_pid == p)
            {
                /* read total_count from dispatcher */
                while (1)
                {
                    ssize_t result = read(pipe_from_disp[0], buf, sizeof(buf));
                    if (result > 0)
                        write(1, buf, result);
                    else
                        break;
                }
                break;
            }
        }
    }

    return 0;
}