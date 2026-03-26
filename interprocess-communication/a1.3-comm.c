#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

#define P 4

int active_children = 0;

/* SIGINT handler */
void sighandler(int signum)
{
    char buf[256];
    int n = 0;

    const char *msg = "Active children: ";
    int len = 17;
    memcpy(buf, msg, len);
    n += len;

    int tmp = active_children;
    char num[10];
    int numlen = 0;

    if (tmp == 0)
        num[numlen++] = '0';
    else
    {
        char rev[10];
        int r = 0;
        while (tmp > 0)
        {
            rev[r++] = '0' + (tmp % 10);
            tmp /= 10;
        }
        while (r > 0)
            num[numlen++] = rev[--r];
    }

    memcpy(buf + n, num, numlen);
    n += numlen;

    buf[n++] = '\n';
    write(1, buf, n);
}

/* SIGUSR1 handler */
void sigusr1_handler(int signum)
{
    active_children++;
}

/* SIGUSR2 handler */
void sigusr2_handler(int signum)
{
    active_children--;
}

int main(int argc, char *argv[])
{
    int fpr, fpw;
    int oflags, mode;
    int pipes[P][2];
    char c2c;
    off_t file_size;
    pid_t p;

    if (argc != 4)
    {
        const char *err = "Usage: ./a1.3-comm <input_file> <output_file> <character>\n";
        write(2, err, strlen(err));
        return -1;
    }

    fpr = open(argv[1], O_RDONLY);
    if (fpr == -1)
    {
        const char *err = "Problem opening file to read\n";
        write(2, err, strlen(err));
        exit(1);
    }

    oflags = O_CREAT | O_WRONLY | O_TRUNC;
    mode = S_IRUSR | S_IWUSR;

    fpw = open(argv[2], oflags, mode);
    if (fpw == -1)
    {
        const char *err = "Problem opening file to write\n";
        write(2, err, strlen(err));
        close(fpr);
        exit(1);
    }

    c2c = argv[3][0];

    file_size = lseek(fpr, 0, SEEK_END);

    for (int i = 0; i < P; i++)
        if (pipe(pipes[i]) == -1)
        {
            const char *err = "Error creating pipe\n";
            write(2, err, strlen(err));
            close(fpr);
            close(fpw);
            exit(1);
        }

    /* install handlers */
    struct sigaction sa_int;
    sa_int.sa_handler = sighandler;
    sigemptyset(&sa_int.sa_mask);
    sa_int.sa_flags = SA_RESTART;
    sigaction(SIGINT, &sa_int, NULL);

    struct sigaction sa_usr;
    sigemptyset(&sa_usr.sa_mask);
    sa_usr.sa_flags = SA_RESTART;

    sa_usr.sa_handler = sigusr1_handler;
    sigaction(SIGUSR1, &sa_usr, NULL);

    sa_usr.sa_handler = sigusr2_handler;
    sigaction(SIGUSR2, &sa_usr, NULL);

    for (int i = 0; i < P; i++)
    {
        p = fork();
        if (p < 0)
        {
            const char *err = "Error creating child process\n";
            write(2, err, strlen(err));
            exit(1);
        }
        else if (p == 0)
        {
            signal(SIGINT, SIG_IGN);

            for (int j = 0; j < P; j++)
            {
                if (j != i)
                {
                    close(pipes[j][0]);
                    close(pipes[j][1]);
                }
                else
                    close(pipes[j][0]);
            }

            off_t chunk_size = (file_size + P - 1) / P;
            off_t start = i * chunk_size;
            off_t end = start + chunk_size;
            if (end > file_size)
                end = file_size;

            kill(getppid(), SIGUSR1);

            sleep(2 + i);

            char buff[1024];
            int count = 0;
            ssize_t rcnt;
            size_t total_read = 0;
            size_t to_read = end - start;

            off_t current_offset = start;

            for (;;)
            {
                size_t remaining = to_read - total_read;
                size_t chunk = remaining < sizeof(buff) ? remaining : sizeof(buff);

                rcnt = pread(fpr, buff, chunk, current_offset);

                if (rcnt == 0)
                    break;
                if (rcnt == -1)
                {
                    const char *err = "Error reading input file in child\n";
                    write(2, err, strlen(err));
                    close(pipes[i][1]);
                    exit(1);
                }

                for (ssize_t idx = 0; idx < rcnt; idx++)
                    if (buff[idx] == c2c)
                        count++;

                total_read += rcnt;
                current_offset += rcnt;

                if (total_read >= to_read)
                    break;
            }

            kill(getppid(), SIGUSR2);

            if (write(pipes[i][1], &count, sizeof(count)) != sizeof(count))
            {
                const char *err = "Error writing to pipe\n";
                write(2, err, strlen(err));
                close(pipes[i][1]);
                exit(1);
            }

            close(pipes[i][1]);
            exit(0);
        }
    }

    for (int i = 0; i < P; i++)
        close(pipes[i][1]);

    int total = 0;

    for (int i = 0; i < P; i++)
    {
        int child_count;
        read(pipes[i][0], &child_count, sizeof(child_count));
        total += child_count;
        close(pipes[i][0]);
    }

    close(fpr);

    /* build message */
    char msg[256];
    char numbuf[20];
    msg[0] = '\0';
    strcat(msg, "The character '");

    char tmpchar[2] = {c2c, '\0'};
    strcat(msg, tmpchar);
    strcat(msg, "' appears ");

    int tmp = total;
    int nlen = 0;
    if (tmp == 0)
        numbuf[nlen++] = '0';
    else
    {
        char rev[20];
        int revlen = 0;
        while (tmp > 0)
        {
            rev[revlen++] = '0' + (tmp % 10);
            tmp /= 10;
        }
        for (int i = revlen - 1; i >= 0; i--)
            numbuf[nlen++] = rev[i];
    }
    numbuf[nlen] = '\0';
    strcat(msg, numbuf);

    strcat(msg, " times in file ");
    strcat(msg, argv[1]);
    strcat(msg, ".\n");

    size_t widx = 0;
    ssize_t wcnt;
    size_t len = strlen(msg);

    do
    {
        wcnt = write(fpw, msg + widx, len - widx);
        if (wcnt == -1)
        {
            const char *err = "Error writing output file\n";
            write(2, err, strlen(err));
            close(fpw);
            exit(1);
        }
        widx += wcnt;
    } while (widx < len);

    close(fpw);

    for (int i = 0; i < P; i++)
        wait(NULL);

    return 0;
}