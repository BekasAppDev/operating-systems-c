#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char *argv[])
{

    int fpr, fpw;
    int oflags, mode;
    char buff[1024];
    char c2c;
    int count = 0;
    ssize_t rcnt;
    size_t idx;

    /* open file for reading */
    fpr = open(argv[1], O_RDONLY);
    if (fpr == -1)
    {
        const char *err = "Problem opening file to read\n";
        write(2, err, strlen(err));
        exit(1);
    }

    /* open file for writing the result */
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

    /* character to search for (third parameter in command line) */
    c2c = argv[3][0];

    /* count the occurences of the given character */
    for (;;)
    {
        rcnt = read(fpr, buff, sizeof(buff));
        if (rcnt == 0)
            break;
        if (rcnt == -1)
        {
            const char *err = "Error reading input file\n";
            write(2, err, strlen(err));
            close(fpr);
            close(fpw);
            exit(1);
        }

        for (idx = 0; idx < rcnt; idx++)
            if (buff[idx] == c2c)
                count++;
    }

    /* close the file for reading */
    close(fpr);

    /* write the result in the output file */
    char msg[256];
    char numbuf[20];
    size_t widx;
    ssize_t wcnt;

    /* build message without using fprintf */
    msg[0] = '\0';
    strcat(msg, "The character '");

    char tmpchar[2] = {c2c, '\0'};
    strcat(msg, tmpchar);

    strcat(msg, "' appears ");

    int tmp = count;
    int nlen = 0;
    if (tmp == 0)
    {
        numbuf[nlen++] = '0';
    }
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

    /* write the result in the output file */
    widx = 0;
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

    /* close the output file */
    close(fpw);

    exit(0);
}
