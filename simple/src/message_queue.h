/*
 * message_queue.h - Message passing between the parent and the child
 * processes, using POSIX message queues (<mqueue.h>).
 *
 * Two queues are used:
 *
 *   task queue : parent -> children   "please sort data[start..end)"
 *   done queue : children -> parent   "I finished chunk X, it took Y s"
 */
#ifndef MESSAGE_QUEUE_H
#define MESSAGE_QUEUE_H

#include <mqueue.h>
#include <stddef.h>

/*
 * Maximum messages waiting in one queue. Linux allows at most 10 without
 * root (see /proc/sys/fs/mqueue/msg_max). A full queue is not an error:
 * mq_send() simply waits until someone takes a message out.
 */
#define QUEUE_DEPTH 10

/* Sent by the parent: the chunk boundaries a child must sort. */
typedef struct {
    int    chunk_id;
    size_t start;
    size_t end;
} task_msg_t;

/* Sent back by a child when its chunk is sorted. */
typedef struct {
    int    chunk_id;
    int    pid;
    int    ok;          /* 1 = sorted correctly, 0 = something went wrong */
    double seconds;     /* how long the sort took */
} done_msg_t;

typedef struct {
    mqd_t task_queue;
    mqd_t done_queue;
    char  task_name[64];
    char  done_name[64];
} queues_t;

/* Parent: create both queues. Returns 0 on success, -1 on error. */
int  queues_create(queues_t *q);

/* Child: close its copies of the queue descriptors (before exiting). */
void queues_close(queues_t *q);

/* Parent: close the queues and delete them from the system. */
void queues_remove(queues_t *q);

/* Send / receive one message. Return 0 on success, -1 on error. */
int send_task(queues_t *q, const task_msg_t *msg);
int receive_task(queues_t *q, task_msg_t *msg);
int send_done(queues_t *q, const done_msg_t *msg);
int receive_done(queues_t *q, done_msg_t *msg);

#endif
