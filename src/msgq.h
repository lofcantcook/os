/*
 * msgq.h - POSIX message queue channel between the parent and workers.
 *
 * Two queues are used:
 *   task queue   : parent -> workers, carries chunk boundaries to sort
 *   status queue : workers -> parent, carries completion status
 */
#ifndef MSGQ_H
#define MSGQ_H

#include <mqueue.h>
#include <stdint.h>
#include <sys/types.h>

#define MQ_NAME_LEN 64
#define MQ_DEPTH    8   /* must be <= /proc/sys/fs/mqueue/msg_max (default 10) */

enum task_type {
    TASK_SORT = 1,  /* sort data[offset .. offset+len) */
    TASK_EXIT = 2   /* no more work, worker should exit */
};

typedef struct {
    int32_t  type;
    int32_t  chunk_id;
    uint64_t offset;
    uint64_t len;
} task_msg_t;

typedef struct {
    int32_t  chunk_id;
    int32_t  status;    /* 0 on success, otherwise an errno value */
    pid_t    pid;
    double   sort_sec;
} status_msg_t;

typedef struct {
    mqd_t task_q;
    mqd_t status_q;
    char  task_name[MQ_NAME_LEN];
    char  status_name[MQ_NAME_LEN];
} msgq_t;

/* Parent: create both queues (names are made unique with the parent pid). */
int  msgq_create(msgq_t *q);

/* Parent: close descriptors and remove the queue names. Safe to call twice. */
void msgq_destroy(msgq_t *q);

/* Worker: close inherited descriptors before exiting. */
void msgq_close(msgq_t *q);

/* All send/receive calls retry on EINTR. Return 0 on success, -1 on error. */
int msgq_send_task(msgq_t *q, const task_msg_t *t);
int msgq_recv_task(msgq_t *q, task_msg_t *t);
int msgq_send_status(msgq_t *q, const status_msg_t *s);
int msgq_recv_status(msgq_t *q, status_msg_t *s);

#endif /* MSGQ_H */
