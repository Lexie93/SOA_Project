#ifndef INC_TWO_LOCKS_QUEUE_H
#define INC_TWO_LOCKS_QUEUE_H

#include <linux/spinlock_types.h>

typedef struct node_t{
	size_t size;
	char *content;
	struct node_t *next;
} message;

typedef struct queue_t{
	message *head;
	message *tail;
	spinlock_t h_lock;
	spinlock_t t_lock;
} queue;

int initialize_queue(queue *q);

void remove_queue(queue *q);

void enqueue(queue *q, message *node);

//do not modify or free a node after dequeue, it's still used as dummy!
int dequeue(queue *q, message *node);

#endif /* INC_TWO_LOCKS_QUEUE_H */