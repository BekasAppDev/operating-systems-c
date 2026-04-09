#define _GNU_SOURCE
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

int fpr;
char c2c[2];
volatile sig_atomic_t total_count = 0;
volatile sig_atomic_t completed_chunks = 0;
volatile sig_atomic_t num_chunks = 0;
Task *pool = NULL;
Worker workers[MAX_WORKERS];

int spawn_worker(Worker *w)
{
    if (pipe2(w->pipe_to, O_NONBLOCK) == -1)
    {
        const char *err = "pipe2 pipe_to";
        write(2, err, strlen(err));
        return -1;
    }

    if (pipe2(w->pipe_from, O_NONBLOCK) == -1)
    {
        const char *err = "pipe2 pipe_from";
        write(2, err, strlen(err));
        close(w->pipe_to[0]);
        close(w->pipe_to[1]);
        return -1;
    }

    pid_t p = fork();
    if (p < 0)
    {
        const char *msg = "fork error\n";
        write(2, msg, strlen(msg));
        return -1;
    }
    else if (p == 0)
    {
        /* child => worker */
        dup2(w->pipe_to[0], 0);
        dup2(w->pipe_from[1], 1);

        close(w->pipe_to[0]);
        close(w->pipe_to[1]);
        close(w->pipe_from[0]);
        close(w->pipe_from[1]);

        char fpr_str[10];
        if (fpr >= 0)
            int_to_padded_string(fpr, fpr_str, 0);

        char *args[] = {"./a1.4-worker",
                        fpr_str,
                        c2c,
                        NULL};

        execv(args[0], args);
        const char *msg = "execv";
        write(2, msg, strlen(msg));
        exit(1);
    }
    else
    {
        /* parent => dispatcher */
        w->pid = p;
        w->active = 1;
        w->busy = 0;
        w->read_bytes = 0;

        close(w->pipe_to[0]);
        close(w->pipe_from[1]);
        return 0;
    }
}

/* send message to frontend helper function */
void frontend_msg(const char *msg)
{
    size_t len = strlen(msg);
    size_t idx = 0;
    ssize_t wcnt;

    do
    {
        wcnt = write(1, msg + idx, len - idx);
        if (wcnt == -1)
        {
            const char *err = "write";
            write(2, err, strlen(err));
            return;
        }
        idx += wcnt;
    } while (idx < len);
}

/* signal handlers */
void sig_add_handler(int signo)
{
    /* add worker if possible */
    for (int i = 0; i < MAX_WORKERS; i++)
    {
        if (!workers[i].active)
        {
            spawn_worker(&workers[i]);
            break;
        }
    }
}

void sig_rem_handler(int signo)
{
    int active_count = 0;
    for (int i = 0; i < MAX_WORKERS; i++)
        if (workers[i].active)
            active_count++;

    if (active_count <= 1)
        return;

    /* remove last worker */
    for (int i = MAX_WORKERS - 1; i >= 0; i--)
    {
        if (workers[i].active)
        {
            kill(workers[i].pid, SIGKILL);
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
    snprintf(buf, sizeof(buf), "Dispatcher: active workers %d\n", active_count);
    frontend_msg(buf);
}

void sig_prg_handler(int signo)
{
    char buf[128];
    snprintf(buf, sizeof(buf),
             "Dispatcher: progress %d/%d chunks, current count %d\n",
             completed_chunks, num_chunks, total_count);
    frontend_msg(buf);
}

int main(int argc, char *argv[])
{
    if (argc != 3)
    {
        const char *err = "Usage: ./a1.4-dispatcher <input_file> \n";
        write(2, err, strlen(err));
        exit(1);
    }

    /* install signal handlers */
    struct sigaction sa;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;

    sa.sa_handler = sig_add_handler;
    sigaction(SIGUSR1, &sa, NULL);

    sa.sa_handler = sig_rem_handler;
    sigaction(SIGUSR2, &sa, NULL);

    sa.sa_handler = sig_inf_handler;
    sigaction(SIGINT, &sa, NULL);

    sa.sa_handler = sig_prg_handler;
    sigaction(SIGTERM, &sa, NULL);

    fpr = open(argv[1], O_RDONLY);
    if (fpr == -1)
    {
        const char *msg = "Problem opening file\n";
        write(2, msg, strlen(msg));
        exit(1);
    }

    /* format c2c as string */
    c2c[0] = argv[2][0];
    c2c[1] = '\0';

    /* calculate number of chunks (tasks) */
    off_t total_size = lseek(fpr, 0, SEEK_END);
    lseek(fpr, 0, SEEK_SET);

    num_chunks = (total_size == 0) ? 0 : (int)((total_size + CHUNK_SIZE - 1) / CHUNK_SIZE);

    /* create pool of tasks */
    pool = malloc(sizeof(Task) * num_chunks);
    for (int i = 0; i < num_chunks; i++)
    {
        pool[i].offset = i * CHUNK_SIZE;
        pool[i].status = PENDING;
    }

    /* initially only 1 worker */
    for (int i = 0; i < MAX_WORKERS; i++)
        workers[i].active = 0;

    spawn_worker(&workers[0]);

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
                    if (workers[i].busy)
                        pool[workers[i].last_task_idx].status = PENDING;
                }
            }
        }

        /* assign tasks */
        for (int i = 0; i < MAX_WORKERS; i++)
        {
            if (workers[i].active && !workers[i].busy)
            {
                for (int j = 0; j < num_chunks; j++)
                {
                    if (pool[j].status == PENDING)
                    {
                        char msg[MSG_SIZE];
                        int_to_padded_string(pool[j].offset, msg, MSG_SIZE);

                        size_t idx = 0;
                        while (idx < MSG_SIZE)
                        {
                            ssize_t w = write(workers[i].pipe_to[1],
                                              msg + idx,
                                              MSG_SIZE - idx);
                            if (w > 0)
                                idx += w;
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
            if (workers[i].active && workers[i].busy)
            {
                ssize_t rcnt = read(workers[i].pipe_from[0],
                                    workers[i].read_buf + workers[i].read_bytes,
                                    MSG_SIZE - workers[i].read_bytes);

                if (rcnt > 0)
                    workers[i].read_bytes += rcnt;

                if (workers[i].read_bytes == MSG_SIZE)
                {
                    workers[i].read_buf[MSG_SIZE] = '\0';

                    /* update total_count accordingly */
                    total_count += string_to_int(workers[i].read_buf);

                    /* mark task as COMPLETED and free the worker */
                    pool[workers[i].last_task_idx].status = COMPLETED;
                    workers[i].busy = 0;
                    workers[i].read_bytes = 0;

                    /* update completed_chunks accordingly */
                    completed_chunks++;
                }
            }
        }
    }

    /* send final message with total_count to frontend */
    char final_msg[128];
    snprintf(final_msg, sizeof(final_msg),
             "The character %c appears %d times in file %s\n",
             c2c[0], total_count, argv[1]);

    frontend_msg(final_msg);

    close(fpr);
    return 0;
}