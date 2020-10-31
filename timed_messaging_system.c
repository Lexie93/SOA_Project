#include <linux/module.h>
#include <linux/sched.h>
#include <linux/tty.h>		/* For the tty declarations */
#include <linux/version.h>	/* For LINUX_VERSION_CODE */
#include <linux/slab.h>
#include <linux/wait.h>
#include <linux/atomic.h>
#include <linux/list.h>
#include <linux/workqueue.h>

#include "timed_messaging_system.h"
#include "constants.h"
#include "expose_sys.h"
#include "two_locks_queue.h"


MODULE_LICENSE("GPL");
MODULE_AUTHOR("Alex Ponzo");


static int dev_open(struct inode *, struct file *);
static int dev_release(struct inode *, struct file *);
static ssize_t dev_write(struct file *, const char *, size_t, loff_t *);

static int Major;            /* Major number assigned to broadcast device driver */

typedef struct _object_state{
	atomic_t used_bytes;
	atomic_t pending_messages;
	unsigned int flushing;
	wait_queue_head_t readers_queue;
	struct list_head readers_list;
	spinlock_t readers_list_synchronizer;
	queue message_queue;
	struct list_head sess_list;
	spinlock_t list_sess_synchronizer;
} object_state;

object_state objects[MINORS];

typedef struct _session_data{
	unsigned long send_delay;
	unsigned long receive_timeout;
	int minor;
	struct list_head delayed_mess;
	spinlock_t delayed_message_synchronizer;
	struct list_head list;
} session_data;

typedef struct _delayed_message{
	struct delayed_work work;
	message *mess;
	session_data *sess;
	struct list_head list;
} delayed_message;


static int dev_open(struct inode *inode, struct file *filp) {

   int minor;
   session_data *sess;
   object_state *the_object;
   minor = get_minor(filp);
   the_object = objects + minor;

   if(minor >= MINORS){
	return -ENODEV;
   }

   sess = kmalloc(sizeof(session_data), GFP_KERNEL);
   if (!sess){
   	return -ENOMEM;
   }

   sess->send_delay=0;
   sess->receive_timeout=0;
   sess->minor= minor;
   spin_lock_init(&sess->delayed_message_synchronizer);
   INIT_LIST_HEAD(&sess->delayed_mess);

   spin_lock(&the_object->list_sess_synchronizer);
   list_add(&sess->list, &the_object->sess_list);
   spin_unlock(&the_object->list_sess_synchronizer);

   filp->private_data=sess;

   printk(KERN_INFO "%s: device file successfully opened for object with minor %d\n",MODNAME,minor);

   return 0;
}


static int dev_release(struct inode *inode, struct file *filp) {

	int minor = get_minor(filp);
	session_data *sess = (session_data *) filp->private_data;
	object_state *the_object = objects + minor;

   	spin_lock(&the_object->list_sess_synchronizer);
   	list_del(&sess->list);
   	spin_unlock(&the_object->list_sess_synchronizer);

	kfree(sess);

	printk(KERN_INFO "%s: device file closed\n",MODNAME);

	return 0;
}

//old version working with exclusive wait, NOT UP TO DATE
/*
static ssize_t dev_read_exclusive (struct file *filp, char *buf, size_t count, loff_t *f_pos)	{

	size_t read_size;
	session_data *sess= (session_data *) filp->private_data;
	unsigned long timeout= sess->receive_timeout;
 	object_state *the_object;
 	message *mess;
 	DEFINE_WAIT(wait);
 	//unsigned long res_timeout;
 	int minor = get_minor(filp);
	the_object = objects + minor;

	//res_timeout= exclusive_timeout_wait(&(the_object->readers_queue), timeout);

	mutex_lock(&(the_object->list_msg_synchronizer));
	//atomic_inc(the_object->active_readers);
	while (list_empty(&(the_object->msg_list))){
		mutex_unlock(&(the_object->list_msg_synchronizer));
		if (timeout==0){
			printk("no data...\n");
			//atomic_dec(the_object->active_readers);
			return 0;
		}
		if (the_object->flushing==YES){
			printk("flushing\n");
			//atomic_dec(the_object->active_readers);
			return 0;
		}
		prepare_to_wait_exclusive(&(the_object->readers_queue), &wait, TASK_INTERRUPTIBLE);
		if (!list_empty(&(the_object->msg_list)) || the_object->flushing==YES){
			finish_wait(&(the_object->readers_queue), &wait);
			mutex_lock(&(the_object->list_msg_synchronizer));
			continue;
		}
		if (!signal_pending(current)) {
			timeout = schedule_timeout(timeout);
			if (timeout==0){
				finish_wait(&(the_object->readers_queue), &wait);
			}
			mutex_lock(&(the_object->list_msg_synchronizer));
			continue;
		}
		abort_exclusive_wait(&(the_object->readers_queue), &wait, TASK_INTERRUPTIBLE, NULL);
		//atomic_dec(the_object->active_readers);
		return -ERESTARTSYS;
	}


	mess= list_first_entry(&(the_object->msg_list), message, list);
	read_size= min(count, mess->size);
	copy_to_user(buf, mess->content, read_size);
	printk("reading %s\n", buf);
	list_del(&(mess->list));
	kfree(mess->content);
	kfree(mess);
	mutex_unlock(&(the_object->list_msg_synchronizer));

	atomic_dec(active_readers);
    return read_size;
}
*/

static ssize_t dev_read_ordered (struct file *filp, char *buf, size_t count, loff_t *f_pos)	{

	size_t read_size= 0;
	session_data *sess= (session_data *) filp->private_data;
	unsigned long err;
	unsigned long timeout= sess->receive_timeout;
 	object_state *the_object;
 	message mess;
 	struct list_head list_element;
 	int minor = get_minor(filp);
	the_object = objects + minor;

	spin_lock(&the_object->readers_list_synchronizer);
	list_add_tail(&list_element, &the_object->readers_list);
	spin_unlock(&the_object->readers_list_synchronizer);

	if (sess->receive_timeout>0){

		//wake up condition: (first in the queue AND pending messages) OR flushing
		timeout= wait_event_interruptible_timeout(the_object->readers_queue, ( (list_element.prev==&the_object->readers_list && atomic_read(&the_object->pending_messages)) || the_object->flushing==YES), timeout);
		
		if (timeout==-ERESTARTSYS) {
    	   	printk(KERN_DEBUG "reader process killed\n");
	       	err=-ERESTARTSYS;
       		goto error_handling;
    	}

    	if (timeout==0){
			printk(KERN_DEBUG "read timeout expired\n");
			err=-ENODATA;
       		goto error_handling;
		}

	}

	if (dequeue(&the_object->message_queue, &mess)){
		printk(KERN_DEBUG "no data...\n");
		err=-ENODATA;
       	goto error_handling;
	}

	atomic_sub(mess.size, &the_object->used_bytes);

	read_size= min(count, mess.size);
	copy_to_user(buf, mess.content, read_size);

	//new dummy node
	kfree(mess.content);

	atomic_dec(&the_object->pending_messages);

	spin_lock(&the_object->readers_list_synchronizer);
	list_del(&list_element);
	spin_unlock(&the_object->readers_list_synchronizer);

	//a wake up could have happened before reader was removed from the queue
	if (!atomic_read(&the_object->pending_messages))
		wake_up_interruptible(&the_object->readers_queue);

    return read_size;


    error_handling:

	spin_lock(&the_object->readers_list_synchronizer);
	list_del(&list_element);
	spin_unlock(&the_object->readers_list_synchronizer);

	if (!atomic_read(&the_object->pending_messages))
		wake_up_interruptible(&the_object->readers_queue);

    return err;
}


ssize_t direct_write(const char *buf, size_t count, int minor){

		char *message_content;
		message *mess;
		unsigned long ret;
		object_state *the_object = objects + minor;

		if (atomic_add_return(count, &the_object->used_bytes) > max_storage_size[minor]){
			atomic_sub(count,  &the_object->used_bytes);
			printk(KERN_DEBUG "device full\n");
			return -ENOSPC;
		}

		mess= kmalloc(sizeof(message), GFP_KERNEL);
		if (!mess){
   			return -ENOMEM;
   		}
   		message_content= kmalloc(count, GFP_KERNEL);
   		if (!message_content){
   			return -ENOMEM;
   		}
   		if ((ret=copy_from_user(message_content, buf, count))) {
    		printk(KERN_ERR "error in copy_from_user, ret=%li for %li bytes\n", ret, count);
        	return -EFAULT;
    	}
    	mess->content= message_content;
    	mess->size= count;

    	enqueue(&the_object->message_queue, mess);
    	atomic_inc(&the_object->pending_messages);

    	wake_up_interruptible(&the_object->readers_queue);

    	return count-ret;
}

void my_work_handler(struct work_struct *work){

	struct delayed_work *d_work;
	delayed_message *delayed;
	session_data *sess;
	object_state *the_object;
	int minor;

	d_work= container_of(work, struct delayed_work, work);
	delayed= container_of(d_work, delayed_message, work);
	sess= delayed->sess;
	minor= sess->minor;
	the_object= objects + minor;

	printk(KERN_DEBUG "handling write...\n");

	spin_lock(&sess->delayed_message_synchronizer);
   	list_del(&delayed->list);
	spin_unlock(&sess->delayed_message_synchronizer);

	if (atomic_add_return(delayed->mess->size, &the_object->used_bytes) > max_storage_size[minor]){
		atomic_sub(delayed->mess->size,  &the_object->used_bytes);
		printk("device full\n");
		return;
	}

    enqueue(&the_object->message_queue, delayed->mess);
    atomic_inc(&the_object->pending_messages);

    wake_up_interruptible(&the_object->readers_queue);
    
    kfree(delayed);
}

void delayed_write(const char *buf, size_t count, object_state *the_object, session_data *sess){
	
	unsigned long ret;
	delayed_message *delayed;
	message *mess;
	char *content;

	delayed= kmalloc(sizeof(delayed_message), GFP_KERNEL);
	if (!delayed)
		return;
	mess= kmalloc(sizeof(message), GFP_KERNEL);
	if (!mess){
		kfree(delayed);
		return;
	}
	delayed->mess= mess;
	content= kmalloc(count, GFP_KERNEL);
	if (!content){
		kfree(delayed);
		kfree(mess);
		return;
	}
	delayed->mess->size= count;
	delayed->mess->content= content;
	if ((ret=copy_from_user(mess->content, buf, count))) {
    	printk(KERN_ERR "error in copy_from_user, ret=%li for %li bytes\n", ret, count);
    	kfree(mess);
    	kfree(delayed);
    	kfree(content);
        return;
    }
    delayed->sess= sess;

    spin_lock(&sess->delayed_message_synchronizer);
    list_add_tail(&delayed->list, &sess->delayed_mess);
    spin_unlock(&sess->delayed_message_synchronizer);

    printk(KERN_DEBUG "delaying %s\n", list_last_entry(&sess->delayed_mess, delayed_message, list)->mess->content);

    INIT_DELAYED_WORK(&delayed->work, my_work_handler);
	schedule_delayed_work(&delayed->work, sess->send_delay);
}

static ssize_t dev_write(struct file *filp, const char *buf, size_t count, loff_t *f_pos) {

	session_data *sess= (session_data *) filp->private_data;
	unsigned long delay= sess->send_delay;
	int minor = get_minor(filp);
 	object_state *the_object = objects + minor;

	if (count > max_message_size[minor] || count<=0){
		return -EMSGSIZE;
	} else if (delay==0){
		return direct_write(buf, count, minor);
	} else {
		delayed_write(buf, count, the_object, sess);
		return count;
	}

}

void revoke_delayed_messages(session_data *sess){

	delayed_message *d_mess= NULL;
	delayed_message *temp= NULL;

	printk(KERN_DEBUG "revoke_delayed_messages called\n");

	spin_lock(&sess->delayed_message_synchronizer);
   	list_for_each_entry_safe(d_mess, temp, &(sess->delayed_mess), list) {
   		if (cancel_delayed_work_sync(&d_mess->work)){
   			list_del(&d_mess->list);
   			kfree(d_mess->mess->content);
   			kfree(d_mess->mess);
   			kfree(d_mess);
   		}
   	}
   	spin_unlock(&sess->delayed_message_synchronizer);
}

static long dev_ioctl(struct file *filp, unsigned int command, unsigned long param) {

	session_data *sess= (session_data *) filp->private_data;

	printk(KERN_DEBUG "%s: somebody called an ioctl on dev with [major,minor] number [%d,%d] and command %u and param %li\n",MODNAME,get_major(filp),get_minor(filp),command,param);

	switch(command){
		case SET_SEND_TIMEOUT:
			sess->send_delay=param;
			break;
		case SET_RECV_TIMEOUT:
			sess->receive_timeout=param;
			break;
		case REVOKE_DELAYED_MESSAGES:
			revoke_delayed_messages(sess);
			break;
		default:
			return -ENOTTY;
	}

	return 0;

}

static int dev_flush(struct file *filp, fl_owner_t id){

	session_data *sess;
 	object_state *the_object;
 	int minor = get_minor(filp);
	the_object = objects + minor;

	printk(KERN_DEBUG "flush called on device %d\n", minor);

	spin_lock(&the_object->list_sess_synchronizer);

	list_for_each_entry(sess, &the_object->sess_list, list){
		revoke_delayed_messages(sess);
	}
	the_object->flushing= YES;
	wake_up_interruptible_all(&the_object->readers_queue);
	the_object->flushing= NO;

	spin_unlock(&the_object->list_sess_synchronizer);

	return 0;
}

static struct file_operations fops = {
  .owner = THIS_MODULE,
  .write = dev_write,
  .read = dev_read_ordered,
  .open =  dev_open,
  .release = dev_release,
  .unlocked_ioctl = dev_ioctl,
  .flush = dev_flush
};


int init_module(void) {

	int i;
	int error;

	if ((error=init_sys()))
		return error;

	for(i=0; i<MINORS; i++){

		atomic_set(&objects[i].used_bytes, 0);
		atomic_set(&objects[i].pending_messages, 0);
		objects[i].flushing= NO;
		INIT_LIST_HEAD(&objects[i].sess_list);
		spin_lock_init(&objects[i].list_sess_synchronizer);
		init_waitqueue_head(&objects[i].readers_queue);
		INIT_LIST_HEAD(&objects[i].readers_list);
		spin_lock_init(&objects[i].readers_list_synchronizer);

		if (initialize_queue(&objects[i].message_queue))
			goto revert;
	}

	Major = __register_chrdev(0, 0, 256, DEVICE_NAME, &fops);

	if (Major < 0)
		goto revert;

	printk(KERN_INFO "%s: new device registered, it is assigned major number %d\n",MODNAME, Major);

	return 0;


	revert:

	printk(KERN_ERR "%s: registering device failed\n",MODNAME);

	for (; i>=0; i--)
		remove_queue(&objects[i].message_queue);

	cleanup_sys();

	return -ENOMEM;
}


void cleanup_module(void) {

	int i;

	for (i=0; i<MINORS; i++)
		remove_queue(&objects[i].message_queue);

	cleanup_sys();

	unregister_chrdev(Major, DEVICE_NAME);

	printk(KERN_INFO "%s: device unregistered, it was assigned major number %d\n",MODNAME, Major);

}