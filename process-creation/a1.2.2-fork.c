#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

int main()
{
    int x = 0;
    pid_t p = fork();

    if (p < 0)
    {
        perror("fork");
        exit(1);
    }
    else if (p == 0)
    {
        x = 1;
        pid_t mypid = getpid();
        pid_t ppid = getppid();
        printf("Child: PID=%d, PPIID=%d, x=%d\n", mypid, ppid, x);
        exit(0);
    }
    else
    {
        x = 2;
        pid_t child_pid = p;
        printf("Parent: child PID=%d, x=%d\n", child_pid, x);
        wait(NULL);
    }

    return 0;
}