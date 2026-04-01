#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>

int main(int argc, char *argv[])
{
    if (argc != 4)
    {
        fprintf(stderr, "Usage: %s <input_file> <output_file> <character>\n", argv[0]);
        return -1;
    }
    pid_t p = fork();
    if (p < 0)
    {
        perror("fork");
        exit(1);
    }
    else if (p == 0)
    {
        char *args[] = {"./a1.1-system_calls", argv[1], argv[2], argv[3], NULL};
        execv(args[0], args);
        perror("execv");
        exit(1);
    }
    else
        wait(NULL);

    return 0;
}