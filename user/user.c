#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <sys/ioctl.h>

#include "../constants.h"

char buff[4096];
#define DATA "ciao a tutti\n"
#define SIZE strlen(DATA)

void * thread_writer(void* filed){

	//char *device;
	char num[400];
	int i;
	int fd= *((int *)filed);

	//device = (char*)path;
	sleep(1);
	ioctl(fd,SET_SEND_TIMEOUT,2000);

	//printf("device %s successfully opened\n",device);
	//for(i=0;i<100;i++) write(fd,DATA,SIZE);
	i=0;
	while (1) {
		sprintf(num, "prova%d", i);
		printf("writing: %s\n", num);
		if (!write(fd, num, strlen(num)))
			sleep(1);
		i++;
		sleep(5);
	}
	printf("closing writing thread\n");
	return NULL;

}

void * thread_reader(void* filed){

	int fd= *((int *)filed);
	char num[4096];
	int red=0;

	//device = (char*)path;
	sleep(1);
	//ioctl(fd,5437,500);

	while(1) {
		while((red=read(fd, num, 4096))) {
			num[red]='\0';
			printf("%s\nread: %d\n", num, red);
			sleep(1);
		}
			printf("no data...\n");
				ioctl(fd, SET_RECV_TIMEOUT, 1000);

		sleep(5);
		ioctl(fd, REVOKE_DELAYED_MESSAGES);
		sleep(5);
	}
	return NULL;

}

int main(int argc, char** argv){

     //int ret;
     int major;
     int minors;
     int fd;
     char *path;
     int i,j;
     pthread_t tid[2];

     if(argc<4){
	printf("useg: prog pathname major minors");
	return -1;
     }

     path = argv[1];
     major = strtol(argv[2],NULL,10);
     minors = strtol(argv[3],NULL,10);
     printf("creating %d minors for device %s with major %d\n",minors,path,major);

     for(i=0;i<minors;i++){
	sprintf(buff,"mknod %s%d c %d %i\n",path,i,major,i);
	system(buff);

	sprintf(buff,"%s%d",path,i);
	printf("opening device %s\n",buff);
	fd = open(buff,O_RDWR);
	if(fd == -1) {
		printf("open error on device %s\n",buff);
		return -1;
	}
	for(j=0;j<1;j++)
		pthread_create(&(tid[j]),NULL,thread_writer, (void *) &fd);
	for(j=0;j<1;j++)
		pthread_create(&(tid[j]),NULL,thread_reader, (void *) &fd);


     }

     pause();
     return 0;

}
