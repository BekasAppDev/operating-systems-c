#ifndef CONFIG_H
#define CONFIG_H

/* command size */
#define CMD_BUF 16
/* message (pipe input and output) size*/
#define MSG_SIZE 5
/* chunk size */
#define CHUNK_SIZE 1024

/* sdd worker */
#define CMD_ADD 'a'
/* remove worker */
#define CMD_REM 'r'
/* worker info */
#define CMD_INF 'i'
/* progress status */
#define CMD_PRG 'p'

#endif