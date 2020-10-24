obj-m += timed-messaging-system.o
timed-messaging-system-objs := timed_messaging_system.o expose_sys.o two_locks_queue.o

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules 

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
