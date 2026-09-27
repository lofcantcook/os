/*
 * msgq.c - POSIX message queue implementation (mq_open/mq_send/mq_receive).
 */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "msgq.h"

static mqd_t open_queue(const char *name, long msgsize)
{
    struct mq_attr attr;

    memset(&attr, 0, sizeof(attr));
    attr.mq_maxmsg = MQ_DEPTH;
    attr.mq_msgsize = msgsize;

    /* A stale queue from a crashed run with the same pid would be reused
     * with unknown contents, so remove it first. */
    mq_unlink(name);
    return mq_open(name, O_CREAT | O_EXCL | O_RDWR, 0600, &attr);
}

int msgq_create(msgq_t *q)
{
    pid_t pid = getpid();

    q->task_q = (mqd_t)-1;
    q->status_q = (mqd_t)-1;
    snprintf(q->task_name, sizeof(q->task_name), "/psort_task_%d", (int)pid);
    snprintf(q->status_name, sizeof(q->status_name), "/psort_status_%d", (int)pid);

    q->task_q = open_queue(q->task_name, sizeof(task_msg_t));
    if (q->task_q == (mqd_t)-1) {
        perror("mq_open(task)");
        return -1;
    }
    q->status_q = open_queue(q->status_name, sizeof(status_msg_t));
    if (q->status_q == (mqd_t)-1) {
        perror("mq_open(status)");
        mq_close(q->task_q);
        mq_unlink(q->task_name);
        q->task_q = (mqd_t)-1;
        return -1;
    }
    return 0;
}

void msgq_close(msgq_t *q)
{
    if (q->task_q != (mqd_t)-1) {
        mq_close(q->task_q);
        q->task_q = (mqd_t)-1;
    }
    if (q->status_q != (mqd_t)-1) {
        mq_close(q->status_q);
        q->status_q = (mqd_t)-1;
    }
}

void msgq_destroy(msgq_t *q)
{
    msgq_close(q);
    if (q->task_name[0]) {
        mq_unlink(q->task_name);
        q->task_name[0] = '\0';
    }
    if (q->status_name[0]) {
        mq_unlink(q->status_name);
        q->status_name[0] = '\0';
    }
}

static int send_retry(mqd_t mq, const void *msg, size_t len, const char *what)
{
    while (mq_send(mq, (const char *)msg, len, 0) == -1) {
        if (errno == EINTR)
            continue;
        perror(what);
        return -1;
    }
    return 0;
}

static int recv_retry(mqd_t mq, void *msg, size_t len, const char *what)
{
    for (;;) {
        ssize_t r = mq_receive(mq, (char *)msg, len, NULL);
        if (r == (ssize_t)len)
            return 0;
        if (r == -1 && errno == EINTR)
            continue;
        if (r == -1)
            perror(what);
        else
            fprintf(stderr, "%s: unexpected message size %zd\n", what, r);
        return -1;
    }
}

int msgq_send_task(msgq_t *q, const task_msg_t *t)
{
    return send_retry(q->task_q, t, sizeof(*t), "mq_send(task)");
}

int msgq_recv_task(msgq_t *q, task_msg_t *t)
{
    return recv_retry(q->task_q, t, sizeof(*t), "mq_receive(task)");
}

int msgq_send_status(msgq_t *q, const status_msg_t *s)
{
    return send_retry(q->status_q, s, sizeof(*s), "mq_send(status)");
}

int msgq_recv_status(msgq_t *q, status_msg_t *s)
{
    return recv_retry(q->status_q, s, sizeof(*s), "mq_receive(status)");
}
