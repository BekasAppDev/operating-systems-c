#ifndef UTIL_H
#define UTIL_H
#include <sys/types.h>
#include "config.h"

typedef enum
{
    PENDING,
    ASSIGNED,
    COMPLETED
} status_t;

typedef struct
{
    int offset;
    status_t status;
} Task;

typedef struct
{
    pid_t pid;
    int pipe_to[2];
    int pipe_from[2];
    int busy;
    int last_task_idx;
    int active;
    char read_buf[MSG_SIZE + 1];
    ssize_t read_bytes;
} Worker;

/* utility functions */
int string_to_int(char *s);
void int_to_padded_string(int num, char *str, int pad_len);

#endif