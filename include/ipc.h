#ifndef IPC_SHARED_H
#define IPC_SHARED_H

#include <pthread.h>
#include <stdint.h>

#define SHM_DATA_SIZE 65536
#define SHM_SIZE SHM_DATA_SIZE
#define PAW_IPC_MAX_ARGS 15

#define CMD_IDLE 0
#define CMD_LOAD_PLUGIN 1
#define CMD_EXEC_FUNC 2
#define CMD_EXIT 3

#define STATUS_IDLE 0
#define STATUS_SUCCESS 1
#define STATUS_ERROR 2

typedef struct {
    pthread_mutex_t mutex;
    pthread_cond_t cond_host;
    pthread_cond_t cond_helper;
    int command;
    int status;
    char error_buf[256];
    uint32_t data_size;
    unsigned char data[SHM_DATA_SIZE];
} SharedIPC;

#endif

