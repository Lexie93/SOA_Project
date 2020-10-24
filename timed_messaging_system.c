
#define EXPORT_SYMTAB
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/sched.h>	
#include <linux/pid.h>		/* For pid types */
#include <linux/tty.h>		/* For the tty declarations */
#include <linux/version.h>	/* For LINUX_VERSION_CODE */
#include <linux/kthread.h>
#include <linux/slab.h>
#include <linux/wait.h>
#include <linux/atomic.h>
#include <linux/list.h>
#include <linux/workqueue.h>
#include <linux/rbtree.h>

#include "timed_messaging_system.h"
#include "constants.h"
#include "expose_sys.h"


MODULE_LICENSE("GPL");
MODULE_AUTHOR("Alex Ponzo");


static int dev_open(struct inode *, struct file *);
static int dev_release(struct inode *, struct file *);
static ssize_t dev_write(struct file *, const char *, size_t, loff_t *);

static int Major;            /* Major number assigned to broadcast device driver */

typedef struct _object_state{
	unsigned long used_bytes;
	unsigned long sessions;
	unsigned int flushing;
	wait_queue_head_t readers_queue;
	struct list_head readers_list;
	struct mutex readers_list_synchronizer;
	struct list_head msg_list;
	struct mutex list_msg_synchronizer;
	struct list_head sess_list;
	struct mutex list_sess_synchronizer;
} object_state;

object_state objects[MINORS];

typedef struct _message{
	size_t size;
	char *content;
	struct list_head list;
} message;

typedef struct _session_data{
	unsigned long session_number;
	unsigned long tgid;
	unsigned long pid;
	unsigned long send_delay;
	unsigned long receive_timeout;
	unsigned long delayed_messages;
	int minor;
	struct list_head delayed_mess;
	struct mutex delayed_message_synchronizer;
	struct list_head list;
} session_data;

typedef struct _delayed_message{
	struct delayed_work work;
	message *mess;
	unsigned long message_number;
	session_data *sess;
	struct list_head list;
} delayed_message;

typedef struct _reader{
	unsigned long count;
	struct list_head list;
} reader;

static int sess_num=0;

/* the actual driver */

static int dev_open(struct inode *inode, struct file *filp) {

   int minor;
   session_data *sess;
   object_state *the_object;
   session_data *pos = NULL;
   minor = get_minor(filp);
   the_object = objects + minor;

   if(minor >= MINORS){
	return -ENODEV;
   }

   sess = kmalloc(sizeof(session_data), GFP_ATOMIC);
   if (!sess){
   	return -ENOMEM;
   }

   sess->send_delay=0;
   sess->receive_timeout=0;
   sess->tgid=current->tgid;
   sess->pid=current->pid;
   sess->delayed_messages=0;
   //sess->device= the_object;
   sess->minor= minor;
   mutex_init(&(sess->delayed_message_synchronizer));
   INIT_LIST_HEAD(&(sess->delayed_mess));
   mutex_lock(&(the_object->list_sess_synchronizer));
   sess->session_number= (the_object->sessions)++;
   list_add(&(sess->list), &(the_object->sess_list));
   sess_num++;
   mutex_unlock(&(the_object->list_sess_synchronizer));

   filp->private_data=sess;

   printk("%s: device file successfully opened for object with minor %d\n",MODNAME,minor);
   mutex_lock(&(the_object->list_sess_synchronizer));
   list_for_each_entry(pos, &(the_object->sess_list), list) {
   		printk("session tgid: %lu, pid: %lu\n", pos->tgid, pos->pid);
   }
   mutex_unlock(&(the_object->list_sess_synchronizer));
	//device opened by a default nop
   return 0;
}


static int dev_release(struct inode *inode, struct file *filp) {

	int minor;
	session_data *sess;
	object_state *the_object;
	minor = get_minor(filp);
	the_object = objects + minor;

	sess= (session_data *) filp->private_data;

	mutex_lock(&(the_object->list_sess_synchronizer));
	list_del(&(sess->list));
	sess_num--;
	mutex_unlock(&(the_object->list_sess_synchronizer));

	kfree(sess);

	printk("%s: device file closed\n",MODNAME);
	//device closed by default nop
	return 0;
}

/*
static ssize_t dev_read (struct file *filp, char *buf, size_t count, loff_t *f_pos)	{

	size_t read_size;
	session_data *sess= (session_data *) filp->private_data;
	unsigned long timeout= sess->receive_timeout;
 	object_state *the_object;
 	message *mess;
 	unsigned long res_timeout;
 	int minor = get_minor(filp);
	the_object = objects + minor;

	mutex_lock(&(the_object->list_msg_synchronizer));
	while (list_empty(&(the_object->msg_list))){
		mutex_unlock(&(the_object->list_msg_synchronizer));
		if (timeout==0){
			printk("no data...\n");
			return 0;
		}

		res_timeout= wait_event_interruptible_timeout(the_object->readers_queue, (!list_empty(&(the_object->msg_list)) || the_object->flushing==YES), timeout);

		if (res_timeout==-ERESTARTSYS) {
        	printk("reader process killed\n");
        	return -ERESTARTSYS;
        }

        if (the_object->flushing==YES){			//va cambiato
        	printk("reader flushed\n");
        	return 0;
        }

        timeout=res_timeout;
        printk("reader timeout: %li\n", timeout);

        mutex_lock(&(the_object->list_msg_synchronizer));

	}
	mess= list_first_entry(&(the_object->msg_list), message, list);
	read_size= min(count, mess->size);
	copy_to_user(buf, mess->content, read_size);
	printk("reading %s\n", buf);
	list_del(&(mess->list));
	kfree(mess->content);
	kfree(mess);
	mutex_unlock(&(the_object->list_msg_synchronizer));

    return read_size;
}
*/
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
	unsigned long timeout= sess->receive_timeout;
 	object_state *the_object;
 	message *mess;
 	unsigned long res_timeout;
 	struct list_head list_element;
 	int minor = get_minor(filp);
	the_object = objects + minor;

	mutex_lock(&(the_object->readers_list_synchronizer));
	list_add_tail(&list_element, &(the_object->readers_list));
	mutex_unlock(&(the_object->readers_list_synchronizer));

	res_timeout= wait_event_interruptible_timeout(the_object->readers_queue, ( (list_element.prev==&(the_object->readers_list) && !list_empty(&(the_object->msg_list))) || the_object->flushing==YES), timeout);
	//printk("1) %s    2) %s   3)  %d    4)  %d\n", list_element.prev, &(the_object->readers_list), (list_element.prev==&(the_object->readers_list)), !list_empty(&(the_object->msg_list)));

	if (res_timeout==-ERESTARTSYS) {
       	printk("reader process killed\n");
       	mutex_lock(&(the_object->readers_list_synchronizer));
		list_del(&list_element);
		mutex_unlock(&(the_object->readers_list_synchronizer));
       	return -ERESTARTSYS;
    }
    if (timeout==0){
    	mutex_lock(&(the_object->readers_list_synchronizer));
		list_del(&list_element);
		mutex_unlock(&(the_object->readers_list_synchronizer));
		printk("no data...\n");
		return -ENODATA;
	}
	
    timeout=res_timeout;
    printk("reader timeout: %li\n", timeout);

    mutex_lock(&(the_object->list_msg_synchronizer));

    if (!list_empty(&(the_object->msg_list))) {
		mess= list_first_entry(&(the_object->msg_list), message, list);
		the_object->used_bytes -= mess->size;
		read_size= min(count, mess->size);
		copy_to_user(buf, mess->content, read_size);
		printk("reading %s\n", buf);
		list_del(&(mess->list));
		kfree(mess->content);
		kfree(mess);
	} else {
		mutex_unlock(&(the_object->list_msg_synchronizer));
		return -ENODATA;
	}
	mutex_unlock(&(the_object->list_msg_synchronizer));

	mutex_lock(&(the_object->readers_list_synchronizer));
	list_del(&list_element);
	mutex_unlock(&(the_object->readers_list_synchronizer));

	printk("used_bytes= %lu\n", the_object->used_bytes);

    return read_size;
}


ssize_t actual_write(const char *buf, size_t count, int minor){

		char *message_content;
		object_state *the_object;
		message *mess;
		unsigned long ret;

		the_object= objects + minor;

		mess= kmalloc(sizeof(message), GFP_ATOMIC);
		if (!mess){
   			return -ENOMEM;
   		}
   		message_content= kmalloc(count, GFP_ATOMIC);
   		if (!message_content){
   			return -ENOMEM;
   		}
   		if ((ret=copy_from_user(message_content, buf, count))) {
    		printk("error in copy_from_user, ret=%li for %li bytes\n", ret, count);
        	return -EFAULT;
    	}
    	mess->content= message_content;
    	mess->size= count;

    	mutex_lock(&(the_object->list_msg_synchronizer));

    	if (the_object->used_bytes + count > max_storage_size[minor]){
    		mutex_unlock(&(the_object->list_msg_synchronizer));
    		return -ENOSPC;
    	}

    	the_object->used_bytes += count;
    	list_add_tail(&(mess->list), &(the_object->msg_list));
    	printk("writing %s\n", list_last_entry(&(the_object->msg_list), message, list)->content);

    	mutex_unlock(&(the_object->list_msg_synchronizer));

    	wake_up_interruptible(&(the_object->readers_queue));

    	printk("used_bytes= %lu\n", the_object->used_bytes);

    	return count-ret;
}

void my_work_handler(struct work_struct *work){

	struct delayed_work *d_work;
	delayed_message *delayed;
	session_data *sess;
	object_state *the_object;
	int minor;

	//delayed_message *temp= NULL;

	d_work= container_of(work, struct delayed_work, work);
	delayed= container_of(d_work, delayed_message, work);
	sess= delayed->sess;
	minor= sess->minor;
	the_object= objects + minor;

	mutex_lock(&(sess->delayed_message_synchronizer));
	//list_for_each_entry_safe(temp, &(sess->delayed_mess), list) {
	//	if (temp->message_number==delayed->message_number){
   			printk("handling write...\n");
   			list_del(&(delayed->list));
   	//		break;
   	//	}
   	//}
	mutex_unlock(&(sess->delayed_message_synchronizer));
	
	mutex_lock(&(the_object->list_msg_synchronizer));

	if (the_object->used_bytes + delayed->mess->size > max_storage_size[minor]){
    	mutex_unlock(&(the_object->list_msg_synchronizer));
    	return;
    }

    the_object->used_bytes += delayed->mess->size;
    list_add_tail(&(delayed->mess->list), &(the_object->msg_list));
    printk("writing in handler %s\n", list_last_entry(&(the_object->msg_list), message, list)->content);

    mutex_unlock(&(the_object->list_msg_synchronizer));

    wake_up_interruptible(&(the_object->readers_queue));
    
    kfree(delayed);

    printk("used_bytes= %lu\n", the_object->used_bytes);

}

void delayed_write(const char *buf, size_t count, object_state *the_object, session_data *sess){
	
	unsigned long ret;
	delayed_message *delayed;
	message *mess;
	char *content;
	delayed= kmalloc(sizeof(delayed_message), GFP_ATOMIC);
	if (!delayed)
		return;
	mess= kmalloc(sizeof(message), GFP_ATOMIC);
	if (!mess){
		kfree(delayed);
		return;
	}
	delayed->mess= mess;
	content= kmalloc(count, GFP_ATOMIC);
	if (!content){
		kfree(delayed);
		kfree(mess);
		return;
	}
	delayed->mess->size= count;
	delayed->mess->content= content;
	if ((ret=copy_from_user(mess->content, buf, count))) {
    	printk("error in copy_from_user, ret=%li for %li bytes\n", ret, count);
    	kfree(mess);
    	kfree(delayed);
    	kfree(content);
        return;
    }
    delayed->sess= sess;
    mutex_lock(&(sess->delayed_message_synchronizer));
    delayed->message_number= (sess->delayed_messages)++;
    list_add_tail(&(delayed->list), &(sess->delayed_mess));
    printk("delaying %s\n", list_last_entry(&(sess->delayed_mess), delayed_message, list)->mess->content);

	INIT_DELAYED_WORK(&(delayed->work), my_work_handler);
	schedule_delayed_work(&(delayed->work), sess->send_delay);

    mutex_unlock(&(sess->delayed_message_synchronizer));

}

static ssize_t dev_write(struct file *filp, const char *buf, size_t count, loff_t *f_pos) {

	session_data *sess= (session_data *) filp->private_data;
	unsigned long delay= sess->send_delay;
 	object_state *the_object;
 	int minor = get_minor(filp);
	the_object = objects + minor;

	if (count > max_message_size[minor] || count<=0){
		return -EMSGSIZE;
	} else if (delay==0){
		return actual_write(buf, count, minor);
	} else {
		delayed_write(buf, count, the_object, sess);
		return count;
	}

}

void revoke_delayed_messages(session_data *sess){

	delayed_message *d_mess= NULL;
	delayed_message *temp= NULL;

	printk("revoke_delayed_messages called\n");
	mutex_lock(&(sess->delayed_message_synchronizer));
   	list_for_each_entry_safe(d_mess, temp, &(sess->delayed_mess), list) {
   		if (cancel_delayed_work_sync(&(d_mess->work))){
   			list_del(&(d_mess->list));
   			printk("revoking %s\n", d_mess->mess->content);
   			kfree(d_mess->mess->content);
   			kfree(d_mess->mess);
   			kfree(d_mess);
   		}
   	}
   	mutex_unlock(&(sess->delayed_message_synchronizer));
}

static long dev_ioctl(struct file *filp, unsigned int command, unsigned long param) {

	session_data *sess= (session_data *) filp->private_data;
 	object_state *the_object;
 	int minor = get_minor(filp);
	the_object = objects + minor;

	printk("%s: somebody called an ioctl on dev with [major,minor] number [%d,%d] and command %u and param %li\n",MODNAME,get_major(filp),get_minor(filp),command,param);

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

	printk("flush called on device %d\n", minor);

	mutex_lock(&(the_object->list_sess_synchronizer));
	the_object->flushing= YES;
	list_for_each_entry(sess, &(the_object->sess_list), list){
		revoke_delayed_messages(sess);
	}
	wake_up_interruptible_all(&(the_object->readers_queue));
	the_object->flushing= NO;
	mutex_unlock(&(the_object->list_sess_synchronizer));

	return 0;
}

static struct file_operations fops = {
  .owner = THIS_MODULE,//do not forget this
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

	//initialize the drive internal state
	for(i=0;i<MINORS;i++){
		objects[i].used_bytes = 0;
		objects[i].sessions=0;
		objects[i].flushing= NO;
		//atomic_set(objects[i].active_readers, 0);
		INIT_LIST_HEAD(&(objects[i].msg_list));
		mutex_init(&(objects[i].list_msg_synchronizer));
		INIT_LIST_HEAD(&(objects[i].sess_list));
		mutex_init(&(objects[i].list_sess_synchronizer));
		init_waitqueue_head(&(objects[i].readers_queue));
		INIT_LIST_HEAD(&(objects[i].readers_list));
		mutex_init(&(objects[i].readers_list_synchronizer));
	}

	Major = __register_chrdev(0, 0, 256, DEVICE_NAME, &fops);
	//actually allowed minors are directly controlled within this driver

	if (Major < 0) {
	  printk("%s: registering device failed\n",MODNAME);
	  return Major;
	}

	printk(KERN_INFO "%s: new device registered, it is assigned major number %d\n",MODNAME, Major);

	return 0;
}


void cleanup_module(void) {

	cleanup_sys();

	unregister_chrdev(Major, DEVICE_NAME);

	printk(KERN_INFO "%s: new device unregistered, it was assigned major number %d\n",MODNAME, Major);

}