#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include "config.h"
#include "util.h"

#define MAX_WORKERS 20

int fpr;
char c2c[2];
int total_count = 0;
int completed_chunks = 0;
int num_chunks = 0;
Task *pool = NULL;
Worker workers[MAX_WORKERS];

/* send message to frontend helper function */
void frontend_msg(const char *msg)
{
    write(1, msg, strlen(msg));
    write(1, "\n", 1);
}

/* signal handlers */
void sig_add_handler(int signo)
{
    /* add worker if possible */
    for (int i = 0; i < MAX_WORKERS; i++)
    {
        if (!workers[i].active)
        {
            spawn_worker(&workers[i], fpr, c2c);
            break;
        }
    }
}

void sig_rem_handler(int signo)
{
    /* remove last active worker if more than one exists */
    for (int i = MAX_WORKERS - 1; i >= 0; i--)
    {
        if (workers[i].active)
        {
            kill(workers[i].pid, SIGTERM);
            workers[i].active = 0;
            break;
        }
    }
}

void sig_inf_handler(int signo)
{
    int active_count = 0;
    for (int i = 0; i < MAX_WORKERS; i++)
        if (workers[i].active)
            active_count++;

    char buf[128];
    snprintf(buf, sizeof(buf), "Dispatcher: active workers %d", active_count);
    frontend_msg(buf);
}

void sig_prg_handler(int signo)
{
    char buf[128];
    snprintf(buf, sizeof(buf), "Dispatcher: progress %d/%d chunks, total count %d",
             completed_chunks, num_chunks, total_count);
    frontend_msg(buf);
}

int spawn_worker(Worker *w, int fpr_local, char *c2c_local)
{
    if (pipe2(w->pipe_to, O_NONBLOCK) == -1)
    {
        perror("pipe2 pipe_to");
        return -1;
    }
    if (pipe2(w->pipe_from, O_NONBLOCK) == -1)
    {
        perror("pipe2 pipe_from");
        close(w->pipe_to[0]);
        close(w->pipe_to[1]);
        return -1;
    }

    pid_t p = fork();
    if (p < 0)
    {
        perror("fork");
        close(w->pipe_to[0]);
        close(w->pipe_to[1]);
        close(w->pipe_from[0]);
        close(w->pipe_from[1]);
        return -1;
    }
    else if (p == 0)
    {
        dup2(w->pipe_to[0], 0);
        dup2(w->pipe_from[1], 1);

        close(w->pipe_to[0]);
        close(w->pipe_to[1]);
        close(w->pipe_from[0]);
        close(w->pipe_from[1]);

        char fpr_str[10];
        if (fpr_local >= 0)
            int_to_padded_string(fpr_local, fpr_str, 0);

        char *args[] = {"./a1.4-worker", fpr_local >= 0 ? fpr_str : NULL, c2c_local, NULL};
        execv(args[0], args);
        perror("execv");
        exit(1);
    }
    else
    {
        w->pid = p;
        w->active = 1;
        w->busy = 0;

        close(w->pipe_to[0]);
        close(w->pipe_from[1]);
        return 0;
    }
}

int main(int argc, char *argv[])
{
    if (argc != 3)
    {
        const char *err = "Usage: ./a1.4-dispatcher <input_file> <character>\n";
        write(2, err, strlen(err));
        exit(1);
    }

    /* install signal handlers */
    struct sigaction sa;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;

    /* add worker */
    sa.sa_handler = sig_add_handler;
    sigaction(SIGUSR1, &sa, NULL);

    /* remove worker */
    sa.sa_handler = sig_rem_handler;
    sigaction(SIGUSR2, &sa, NULL);

    /* worker info */
    sa.sa_handler = sig_inf_handler;
    sigaction(SIGINT, &sa, NULL);

    /* progress status */
    sa.sa_handler = sig_prg_handler;
    sigaction(SIGTERM, &sa, NULL);

    fpr = open(argv[1], O_RDONLY);
    if (fpr == -1)
    {
        const char *err = "Problem opening file to read\n";
        write(2, err, strlen(err));
        exit(1);
    }

    c2c[0] = argv[2][0];
    c2c[1] = '\0';

    off_t total_size = lseek(fpr, 0, SEEK_END);
    lseek(fpr, 0, SEEK_SET);
    num_chunks = (total_size + CHUNK_SIZE - 1) / CHUNK_SIZE;

    pool = malloc(sizeof(Task) * num_chunks);
    for (int i = 0; i < num_chunks; i++)
    {
        pool[i].offset = i * CHUNK_SIZE;
        pool[i].status = PENDING;
    }

    for (int i = 0; i < MAX_WORKERS; i++)
        workers[i].active = 0;

    completed_chunks = 0;
    total_count = 0;

    /* spawn 1 worker by default */
    spawn_worker(&workers[0], fpr, c2c);

    while (completed_chunks < num_chunks)
    {
        /* handle any dead workers */
        int status;
        pid_t dead_pid;
        while ((dead_pid = waitpid(-1, &status, WNOHANG)) > 0)
        {
            for (int i = 0; i < MAX_WORKERS; i++)
            {
                if (workers[i].active && workers[i].pid == dead_pid)
                {
                    workers[i].active = 0;

                    /* if worker died while processing a task, mark that task as PENDING */
                    if (workers[i].busy)
                        pool[workers[i].last_task_idx].status = PENDING;
                    break;
                }
            }
        }

        /* assign tasks to workers */
        for (int i = 0; i < MAX_WORKERS; i++)
        {
            /* check available workers */
            if (workers[i].active && !workers[i].busy)
            {
                /* iterate through work pool */
                for (int j = 0; j < num_chunks; j++)
                {
                    /* assign worker i with PENDING task j */
                    if (pool[j].status == PENDING)
                    {
                        char msg[MSG_SIZE + 1];
                        int_to_padded_string(pool[j].offset, msg, MSG_SIZE);

                        ssize_t written = 0;
                        while (written < MSG_SIZE)
                        {
                            ssize_t n = write(workers[i].pipe_to[1], msg + written, MSG_SIZE - written);
                            if (n > 0)
                                written += n;
                        }

                        pool[j].status = ASSIGNED;
                        workers[i].busy = 1;
                        workers[i].last_task_idx = j;
                        break;
                    }
                }
            }
        }

        /* collect results */
        for (int i = 0; i < MAX_WORKERS; i++)
        {
            /* check busy workers */
            if (workers[i].active && workers[i].busy)
            {
                /* read the result from worker */
                char res_msg[MSG_SIZE + 1];
                ssize_t total_read = 0;
                while (total_read < MSG_SIZE)
                {
                    ssize_t n = read(workers[i].pipe_from[0], res_msg + total_read, MSG_SIZE - total_read);
                    if (n > 0)
                        total_read += n;
                    else if (n == -1 && errno != EAGAIN && errno != EWOULDBLOCK)
                    {
                        perror("read");
                        exit(1);
                    }
                    else if (n == 0)
                        break;
                }

                /* validate the result */
                if (total_read == MSG_SIZE)
                {
                    res_msg[MSG_SIZE] = '\0';
                    /* update total_count accordingly */
                    total_count += string_to_int(res_msg);
                    /* mark task as COMPLETED */
                    pool[workers[i].last_task_idx].status = COMPLETED;
                    /* free worker */
                    workers[i].busy = 0;
                    /* update progress accordingly */
                    completed_chunks++;
                }
            }
        }
    }

    /* send final total_count to frontend */
    char final_msg[128];
    snprintf(final_msg, sizeof(final_msg), "The character %c appears %d times in file %s\n", c2c[0], total_count, argv[1]);
    write(1, final_msg, strlen(final_msg));

    close(fpr);
    return 0;
}