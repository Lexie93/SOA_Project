#include <linux/slab.h>

#include "two_locks_queue.h"

int initialize_queue(queue *q){

	//creating dummy node
	message *node=kmalloc(sizeof(message), GFP_KERNEL);
	if (!node)
		return -ENOMEM;

	node->next=NULL;
	q->head=node;
	q->tail=node;
	spin_lock_init(&q->h_lock);
	spin_lock_init(&q->t_lock);

	return 0;
}

void remove_queue(queue *q){

	message *tmp;

	//dummy node removal
	tmp=q->head;
	q->head=q->head->next;
	kfree(tmp);

	while(q->head){
		tmp=q->head;
		q->head=q->head->next;
		kfree(tmp->content);
		kfree(tmp);
	}
}

void enqueue(queue *q, message *node){

	node->next=NULL;

	spin_lock(&q->t_lock);
	q->tail->next=node;
	q->tail=node;
	spin_unlock(&q->t_lock);

	printk(KERN_DEBUG "enqueue: %.10s\n", q->tail->content);
}

//do not modify or free a node after dequeue, it's still used as dummy!
int dequeue(queue *q, message *node){

	message *new_head;
	message *dummy;

	spin_lock(&q->h_lock);
	dummy=q->head;
	new_head=dummy->next;
	if (new_head==NULL){
		spin_unlock(&q->h_lock);
		return -ENODATA;
	}
	*node=*new_head;

	printk(KERN_DEBUG "dequeue: %.10s\n", node->content);

	q->head=new_head;
	spin_unlock(&q->h_lock);

	kfree(dummy);

	return 0;
}