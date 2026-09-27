/*
 * message_queue.c - POSIX message queue code.
 *
 * POSIX calls used:
 *   mq_open()    create (or open) a named queue, like open() for a file
 *   mq_send()    put one message into the queue (waits if the queue is full)
 *   mq_receive() take the oldest message out (waits if the queue is empty)
 *   mq_close()   close this process's descriptor for the queue
 *   mq_unlink()  delete the queue name from the system (like unlink())
 */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "message_queue.h"

/* Create one queue that holds messages of exactly `msg_size` bytes. */
static mqd_t create_queue(const char *name, size_t msg_size)
{
    struct mq_attr attr;
    memset(&attr, 0, sizeof(attr));
    attr.mq_maxmsg  = QUEUE_DEPTH;
    attr.mq_msgsize = (long)msg_size;

    /* Delete a leftover queue with the same name (e.g. from a crash). */
    mq_unlink(name);

    /* O_CREAT = create it, O_RDWR = we want to send and receive,
     * 0600 = only our user may use it. */
    return mq_open(name, O_CREAT | O_RDWR, 0600, &attr);
}

int queues_create(queues_t *q)
{
    /* Put our process id in the names so two runs never share a queue. */
    snprintf(q->task_name, sizeof(q->task_name), "/sort_tasks_%d", (int)getpid());
    snprintf(q->done_name, sizeof(q->done_name), "/sort_done_%d", (int)getpid());

    q->task_queue = create_queue(q->task_name, sizeof(task_msg_t));
    if (q->task_queue == (mqd_t)-1) {
        perror("mq_open (task queue)");
        return -1;
    }

    q->done_queue = create_queue(q->done_name, sizeof(done_msg_t));
    if (q->done_queue == (mqd_t)-1) {
        perror("mq_open (done queue)");
        mq_close(q->task_queue);
        mq_unlink(q->task_name);
        return -1;
    }
    return 0;
}

void queues_close(queues_t *q)
{
    mq_close(q->task_queue);
    mq_close(q->done_queue);
}

void queues_remove(queues_t *q)
{
    queues_close(q);
    mq_unlink(q->task_name);
    mq_unlink(q->done_name);
}

/*
 * Send one message. If a signal interrupts the call (errno == EINTR)
 * nothing was sent, so we simply try again.
 */
static int send_message(mqd_t queue, const void *msg, size_t size)
{
    while (mq_send(queue, (const char *)msg, size, 0) == -1) {
        if (errno != EINTR) {
            perror("mq_send");
            return -1;
        }
    }
    return 0;
}

/* Receive one message of exactly `size` bytes, retrying on EINTR. */
static int receive_message(mqd_t queue, void *msg, size_t size)
{
    for (;;) {
        ssize_t got = mq_receive(queue, (char *)msg, size, NULL);
        if (got == (ssize_t)size)
            return 0;
        if (got == -1 && errno == EINTR)
            continue;
        perror("mq_receive");
        return -1;
    }
}

int send_task(queues_t *q, const task_msg_t *msg)
{
    return send_message(q->task_queue, msg, sizeof(*msg));
}

int receive_task(queues_t *q, task_msg_t *msg)
{
    return receive_message(q->task_queue, msg, sizeof(*msg));
}

int send_done(queues_t *q, const done_msg_t *msg)
{
    return send_message(q->done_queue, msg, sizeof(*msg));
}

int receive_done(queues_t *q, done_msg_t *msg)
{
    return receive_message(q->done_queue, msg, sizeof(*msg));
}
