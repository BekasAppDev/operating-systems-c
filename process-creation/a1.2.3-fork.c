#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char *argv[])
{
    if (argc != 4)
    {
        const char *err = "Usage: program <input_file> <output_file> <character>\n";
        write(2, err, strlen(err));
        return -1;
    }

    int fpr, fpw;
    int oflags = O_CREAT | O_WRONLY | O_TRUNC;
    int mode = S_IRUSR | S_IWUSR;
    char c2c = argv[3][0];

    fpr = open(argv[1], O_RDONLY);
    if (fpr == -1)
    {
        const char *err = "Problem opening file to read\n";
        write(2, err, strlen(err));
        exit(1);
    }

    fpw = open(argv[2], oflags, mode);
    if (fpw == -1)
    {
        const char *err = "Problem opening file to write\n";
        write(2, err, strlen(err));
        close(fpw);
        exit(1);
    }

    int pipefd[2];
    if (pipe(pipefd) == -1)
    {
        const char *err = "Error creating pipe\n";
        write(2, err, strlen(err));
        close(fpw);
        close(fpr);
        exit(1);
    }

    pid_t pid = fork();

    if (pid < 0)
    {
        const char *err = "Error creating child process\n";
        write(2, err, strlen(err));
        close(fpr);
        close(fpw);
        close(pipefd[0]);
        close(pipefd[1]);
        exit(1);
    }
    else if (pid == 0)
    {
        close(pipefd[0]);

        char buff[1024];
        int count = 0;
        ssize_t rcnt;
        size_t idx;

        for (;;)
        {
            rcnt = read(fpr, buff, sizeof(buff));
            if (rcnt == 0)
                break;
            if (rcnt == -1)
            {
                const char *err = "Error reading input file in child\n";
                write(2, err, strlen(err));
                close(fpr);
                close(pipefd[1]);
                exit(1);
            }
            for (idx = 0; idx < rcnt; idx++)
                if (buff[idx] == c2c)
                    count++;
        }

        close(fpr);

        if (write(pipefd[1], &count, sizeof(count)) != sizeof(count))
        {
            const char *err = "Error writing to pipe\n";
            write(2, err, strlen(err));
            close(pipefd[1]);
            exit(1);
        }
        close(pipefd[1]);
        exit(0);
    }
    else
    {
        close(pipefd[1]);

        int child_count;
        size_t r = read(pipefd[0], &child_count, sizeof(child_count));
        if (r != sizeof(child_count))
        {
            const char *err = "Error reading from pipe\n";
            write(2, err, strlen(err));
            close(fpr);
            close(fpw);
            exit(1);
        }
        close(pipefd[0]);
        close(fpr);

        char msg[256];
        char numbuf[20];
        msg[0] = '\0';
        strcat(msg, "The character ");
        char tmpchar[2] = {c2c, '\0'};
        strcat(msg, tmpchar);
        strcat(msg, " appears ");

        int tmp = child_count;
        int nlen = 0;
        if (tmp == 0)
            numbuf[nlen++] = '\0';
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
        strcat(msg, "\n");

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
        wait(NULL);

        return 0;
    }
}