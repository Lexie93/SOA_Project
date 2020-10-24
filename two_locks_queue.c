#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>

struct node_t{
	int value;
	struct node_t *next;
};

struct queue_t{
	struct node_t *Head;
	struct node_t *Tail;
	struct mutex H_lock;
	struct mutex T_lock;
};

int initialize_queue(struct queue_t *Q){

	struct node_t *node=kmalloc(sizeof(struct node_t), GFP_ATOMIC);
	if (!node)
		return -ENOMEM;

	node->next=NULL;
	Q->Head=node;
	Q->Tail=node;
	mutex_init(&Q->H_lock);
	mutex_init(&Q->T_lock);

	return 0;
}

//called only when queue is empty to remove dummy node
void remove_queue(struct queue_t *Q){
	kfree(Q->Head);
}

int enqueue(struct queue_t *Q, int value){

	struct node_t *node=kmalloc(sizeof(struct node_t), GFP_ATOMIC);
	if (!node)
		return -ENOMEM;

	node->value=value;
	node->next=NULL;

	mutex_lock(&Q->T_lock);
	Q->Tail->next=node;
	Q->Tail=node;
	mutex_unlock(&Q->T_lock);

	return 0;
}

int dequeue(struct queue_t *Q, int *pvalue){

	struct node_t *new_head;
	struct node_t *node;

	mutex_lock(&Q->H_lock);
	node=Q->Head;
	new_head=node->next;
	if (new_head==NULL){
		mutex_unlock(&Q->T_lock);
		return -ENODATA;
	}
	*pvalue=new_head->value;
	Q->Head=new_head;
	mutex_unlock(&Q->H_lock);

	kfree(node);

	return 0;
}