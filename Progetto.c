
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

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 11, 0)
#include <linux/sched/signal.h>
#endif


MODULE_LICENSE("GPL");
MODULE_AUTHOR("Francesco Quaglia");

#define MODNAME "CHAR DEV"

#define NO (0)
#define YES (NO+1)


static int dev_open(struct inode *, struct file *);
static int dev_release(struct inode *, struct file *);
static ssize_t dev_write(struct file *, const char *, size_t, loff_t *);

#define DEVICE_NAME "my-new-dev"  /* Device file name in /dev/ - not mandatory  */


static int Major;            /* Major number assigned to broadcast device driver */

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 0, 0)
#define get_major(session)	MAJOR(session->f_inode->i_rdev)
#define get_minor(session)	MINOR(session->f_inode->i_rdev)
#else
#define get_major(session)	MAJOR(session->f_dentry->d_inode->i_rdev)
#define get_minor(session)	MINOR(session->f_dentry->d_inode->i_rdev)
#endif

/*typedef struct _object_state{
	struct mutex operation_synchronizer;
	int valid_bytes;
	char * stream_content;//the I/O node is a buffer in memory

} object_state;
*/

typedef struct _object_state {
        wait_queue_head_t inq, outq;       // read and write queues
        char *buffer, *end;                // begin of buf, end of buf
        char *rp, *wp;                     // where to read, where to write
        struct mutex sem;              	   // mutual exclusion semaphore
        int send_timeout, recv_timeout;
} object_state;


typedef struct _thread_data{
		struct file *t_filp;
		const char *t_buff;
		size_t t_len;
		loff_t *t_off;
	} thread_data;

#define MINORS 8
//object_state objects[MINORS];

#define OBJECT_MAX_SIZE  (28) //just one page

static int timeout = 1;// this can be configured at run time via the sys file system 
module_param(timeout,int,0660);

/* the actual driver */

static int dev_open(struct inode *inode, struct file *filp) {

   int minor;
   object_state *obj;
   minor = get_minor(filp);

   if(minor >= MINORS){
	return -ENODEV;
   }
   obj = kmalloc(sizeof(object_state), GFP_ATOMIC);
   if (!obj) 
   	return -1;
   obj->buffer=(char*)__get_free_page(GFP_KERNEL);
   if (obj->buffer==NULL) 
   	return -1;
   obj->end = obj->buffer + OBJECT_MAX_SIZE;
   obj->rp = obj->buffer;
   obj->wp = obj->buffer;
   mutex_init(&(obj->sem));
   init_waitqueue_head(&(obj->inq));
   init_waitqueue_head(&(obj->outq));
   obj->send_timeout=0;
   obj->recv_timeout=0;

   filp->private_data=obj;

   printk("%s: device file successfully opened for object with minor %d\n",MODNAME,minor);
//device opened by a default nop
   return 0;
}


static int dev_release(struct inode *inode, struct file *filp) {

	int minor;
	minor = get_minor(filp);

	free_page((unsigned long)((object_state *) filp->private_data)->buffer);
	kfree(filp->private_data);

	printk("%s: device file closed\n",MODNAME);
	//device closed by default nop
	return 0;

}

static ssize_t dev_read (struct file *filp, char *buf, size_t count, loff_t *f_pos)
{
    object_state *dev = filp->private_data;
    int ret;
    size_t message_size;

    mutex_lock(&(dev->sem));

    while (dev->rp==dev->wp) { /* nothing to read */
        mutex_unlock(&(dev->sem)); /* release the lock */
        if (filp->f_flags & O_NONBLOCK)
            return -EAGAIN;

        printk("\"%s\" nothing to read\nsession: %p\n", current->comm, filp);
        return 0;
        //printk("\"%s\" reading: going to sleep\n", current->comm);
        //if (wait_event_interruptible(dev->inq, (dev->rp != dev->wp)))
        //    return -ERESTARTSYS; /* signal: tell the fs layer to handle it */
        /* otherwise loop, but first reacquire the lock */

        mutex_lock(&(dev->sem));
    }
    /* ok, data is there, return something */
    if (dev->wp > dev->rp)
        count = min(count, (size_t)(dev->wp - dev->rp));
    else /* the write pointer has wrapped, return data up to dev->end */
        count = min(count, (size_t)(dev->end - dev->rp));
    printk("message to read: %s size: %d\n", dev->rp, (int) strlen(dev->rp));
    message_size=strlen(dev->rp);
    ret = copy_to_user(buf, dev->rp, min(count, message_size)); 
    dev->rp += message_size+1;
    if (dev->rp==dev->end)
        dev->rp = dev->buffer; /* wrapped */
    mutex_unlock(&(dev->sem));

    /* finally, awake any writers and return */
    //wake_up_interruptible(&dev->outq);
    printk("\"%s\" did read %li bytes of %li\n",current->comm, (long) count - ret -1, (long) message_size);
    return min(count, message_size);
}


/* How much space is free? */
static int spacefree(object_state *dev)
{
    if (dev->rp==dev->wp)
        return OBJECT_MAX_SIZE - 1;
    return ((dev->rp + OBJECT_MAX_SIZE - dev->wp) % OBJECT_MAX_SIZE) - 1;
}

static ssize_t dev_write(struct file *filp, const char *buf, size_t count, loff_t *f_pos)
{
    object_state *dev = filp->private_data;

    if (count<=0)
    	return 0;
    count++;			//space for \0

    mutex_lock(&(dev->sem));

    /* ok, space is there, accept something */
    count = min(count, (size_t)spacefree(dev));
	if (count==0) {
		printk("\"%s\" device [%d,%d] file is full\n", current->comm, get_major(filp), get_minor(filp));
		mutex_unlock(&(dev->sem));
		return 0;
	}
    if (dev->wp >= dev->rp)
        count = min(count, (size_t)(dev->end - dev->wp)); /* to end-of-buf */
    else /* the write pointer has wrapped, fill up to rp-1 */
        count = min(count, (size_t)(dev->rp - dev->wp - 1));
    printk("Going to accept %li bytes to %p from %p\nsession: %p\n", (long)count-1, dev->wp, buf, filp);
    if (copy_from_user(dev->wp, buf, count-1)) {
        mutex_unlock(&(dev->sem));
        return -EFAULT;
    }
    *((dev->wp)+count-1)='\0';
    dev->wp += count;
    if (dev->wp==dev->end)
        dev->wp = dev->buffer; /* wrapped */
    mutex_unlock(&(dev->sem));

    /* finally, awake any reader */
    wake_up_interruptible(&dev->inq);  /* blocked in read(  ) and select(  ) */

    printk("\"%s\" did write %li bytes; new message: %s\n",current->comm, (long)count-1, (dev->wp)-count);
    count--;
    return count;
}

/*
int thread_function(void* data){

	thread_data *t= (thread_data *) data;

  DECLARE_WAIT_QUEUE_HEAD(the_queue);
  int minor = get_minor(t->t_filp);
  int ret;
  ktime_t ktime_interval;
  object_state *the_object;
  the_object = objects + minor;

  ktime_interval = ktime_set( 0, 1000*1000 );
  wait_event_interruptible_hrtimeout(the_queue, 0, ktime_interval);

  //need to lock in any case
  mutex_lock(&(the_object->operation_synchronizer));
  if(*(t->t_off) >= OBJECT_MAX_SIZE) {//offset too large
 	 mutex_unlock(&(the_object->operation_synchronizer));
 	 kfree(t);
	 return -ENOSPC;//no space left on device
  } 
  if(*(t->t_off) > the_object->valid_bytes) {//offset bwyond the current stream size
 	 mutex_unlock(&(the_object->operation_synchronizer));
 	 kfree(t);
	 return -ENOSR;//out of stream resources
  } 
  if((OBJECT_MAX_SIZE - *(t->t_off)) < t->t_len) t->t_len = OBJECT_MAX_SIZE - *(t->t_off);
  ret = copy_from_user(&(the_object->stream_content[*(t->t_off)]),t->t_buff,t->t_len);
  
  *(t->t_off) += (t->t_len - ret);
  the_object->valid_bytes = *(t->t_off);
  mutex_unlock(&(the_object->operation_synchronizer));
  kfree(t);
  return ret;
}


static ssize_t dev_write(struct file *filp, const char *buff, size_t len, loff_t *off) {

	struct task_struct *the_new_daemon;
  	thread_data *t=kmalloc(sizeof(thread_data), GFP_KERNEL);
  	char name[128]="write_thread";
  	t->t_off=off;
  	t->t_len=len;
  	t->t_buff=buff;
  	t->t_filp=filp;

  	printk("%s: somebody called a write on dev with [major,minor] number [%d,%d]\n",MODNAME,get_major(filp),get_minor(filp));

  	the_new_daemon = kthread_create(thread_function,(void *) t,name);
  	if(the_new_daemon) {
		wake_up_process(the_new_daemon);
		return len;
	}


  return -1;

}

static ssize_t dev_read(struct file *filp, char *buff, size_t len, loff_t *off) {

  int minor = get_minor(filp);
  int ret;
  object_state *the_object;

  the_object = objects + minor;
  printk("%s: somebody called a read on dev with [major,minor] number [%d,%d]\n",MODNAME,get_major(filp),get_minor(filp));

  //need to lock in any case
  mutex_lock(&(the_object->operation_synchronizer));
  if(*off > the_object->valid_bytes) {
 	 mutex_unlock(&(the_object->operation_synchronizer));
	 return -ENOSR;
  } 
  if((the_object->valid_bytes - *off) < len) len = the_object->valid_bytes - *off;
  ret = copy_to_user(buff,&(the_object->stream_content[*off]),len);
  
  *off += (len - ret);
  mutex_unlock(&(the_object->operation_synchronizer));

  return len - ret;

}
*/

static long dev_ioctl(struct file *filp, unsigned int command, unsigned long param) {

 /* int minor = get_minor(filp);
  object_state *the_object;

  the_object = objects + minor;
  printk("%s: somebody called an ioctl on dev with [major,minor] number [%d,%d] and command %u \n",MODNAME,get_major(filp),get_minor(filp),command);
*/
  //do here whathever you would like to cotrol the state of the device
  return 0;

}

static struct file_operations fops = {
  .owner = THIS_MODULE,//do not forget this
  .write = dev_write,
  .read = dev_read,
  .open =  dev_open,
  .release = dev_release,
  .unlocked_ioctl = dev_ioctl
};



int init_module(void) {

	//int i;

	//initialize the drive internal state
	/*for(i=0;i<MINORS;i++){
		mutex_init(&(objects[i].operation_synchronizer));
		objects[i].valid_bytes = 0;
		objects[i].stream_content = NULL;
		objects[i].stream_content = (char*)__get_free_page(GFP_KERNEL);
		if(objects[i].stream_content == NULL) goto revert_allocation;


	}*/

	Major = __register_chrdev(0, 0, 256, DEVICE_NAME, &fops);
	//actually allowed minors are directly controlled within this driver

	if (Major < 0) {
	  printk("%s: registering device failed\n",MODNAME);
	  return Major;
	}

	printk(KERN_INFO "%s: new device registered, it is assigned major number %d\n",MODNAME, Major);

	return 0;

/*revert_allocation:
	for(;i>=0;i--){
		free_page((unsigned long)objects[i].stream_content);
	}
	return -ENOMEM;*/
}

void cleanup_module(void) {

	/*int i;
	for(i=0;i<MINORS;i++){
		free_page((unsigned long)objects[i].stream_content);
	}*/

	unregister_chrdev(Major, DEVICE_NAME);

	printk(KERN_INFO "%s: new device unregistered, it was assigned major number %d\n",MODNAME, Major);

	return;

}