#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

int main()
{
    pid_t p = fork();
    if (p < 0)
    {
        perror("fork");
        exit(1);
    }
    else if (p == 0)
    {
        pid_t mypid = getpid();
        pid_t ppid = getppid();
        printf("Child: PID=%d, PPIID=%d\n", mypid, ppid);
        exit(0);
    }
    else
    {
        pid_t child_pid = p;
        printf("Parent: child PID=%d\n", child_pid);
        wait(NULL);
    }

    return 0;
}