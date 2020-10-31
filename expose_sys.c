#include <linux/kobject.h>

#include "expose_sys.h"
#include "constants.h"

static struct kobject *device;
static struct kobject *device_kobject[MINORS];

static struct kobj_attribute max_message_attribute[MINORS];
static struct kobj_attribute max_storage_attribute[MINORS];

int max_message_size[MINORS];
int max_storage_size[MINORS];

static ssize_t show_value(struct kobject *kobj, struct kobj_attribute *attribute, char *buf){

	long minor;

	kstrtol(kobj->name, 10, &minor);
    
    if (strcmp(attribute->attr.name, "max_message_size")==0)
		return sprintf(buf, "%d\n", max_message_size[minor]);
	else
		return sprintf(buf, "%d\n", max_storage_size[minor]);
}

static ssize_t store_value(struct kobject *kobj, struct kobj_attribute *attribute, const char *buf, size_t count){

	long minor;

	kstrtol(kobj->name, 10, &minor);
	
	if (strcmp(attribute->attr.name, "max_message_size")==0)
		sscanf(buf, "%du", &max_message_size[minor]);
	else
		sscanf(buf, "%du", &max_storage_size[minor]);

	return count;
}

int init_sys(void){

	int i;
	int error;
	char name[3];

	device = kobject_create_and_add(MODNAME, kernel_kobj);
	if(!device)
		return -ENOMEM;

	for(i=0;i<MINORS;i++){

		sprintf(name, "%d", i);
		device_kobject[i] = kobject_create_and_add(name, device);
		if(!device_kobject[i]){ 
			i--;
			error = -ENOMEM;
			goto put_kbjects;
		}

		max_message_size[i]=256;

		max_message_attribute[i].attr.name="max_message_size";
		max_message_attribute[i].attr.mode=0660;
		max_message_attribute[i].show=show_value;
		max_message_attribute[i].store=store_value;

		error = sysfs_create_file(device_kobject[i], &max_message_attribute[i].attr);
    	if (error) {
    		printk(KERN_ERR "failed to create max_message_size file in /sys/kernel/%s/%d\n", MODNAME, i);
    		goto put_kbjects;
    	}

    	max_storage_size[i]=4096;

    	max_storage_attribute[i].attr.name="max_storage_size";
		max_storage_attribute[i].attr.mode=0660;
		max_storage_attribute[i].show=show_value;
		max_storage_attribute[i].store=store_value;

    	error = sysfs_create_file(device_kobject[i], &max_storage_attribute[i].attr);
    	if (error) {
    		printk(KERN_ERR "failed to create max_storage_size file in /sys/kernel/%s/%d\n", MODNAME, i);
    		goto put_kbjects;
    	}
	}

	return 0;
	

	put_kbjects:

	for(; i>=0; i--)
		kobject_put(device_kobject[i]);
	kobject_put(device);

	return error;
}

void cleanup_sys(void){

	int i;

	for(i=0; i<MINORS; i++){
		kobject_put(device_kobject[i]);
	}

	kobject_put(device);
}