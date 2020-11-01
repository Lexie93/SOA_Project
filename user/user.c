#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <sys/ioctl.h>
#include <errno.h>

#include "../constants.h"

#define WRITES 4
#define READS 2
#define REVOKES 1

void * thread_writer(void* filed){

	char num[400];
	int i=0;
	int fd= *((int *)filed);

	sleep(1);
	ioctl(fd, SET_SEND_TIMEOUT,1000);

	while (1) {
		sprintf(num, "prova%d", i);
		printf("writing: %s\n", num);
		if (!write(fd, num, strlen(num)))
			sleep(1);
		i++;
		sleep(2);
	}

	return NULL;

}

void * thread_reader(void* filed){

	int fd= *((int *)filed);
	char num[4096];

	sleep(1);

	while(1) {
		while(read(fd, num, 4096)>0) {
			num[10]='\0';
			printf("reading: %s\n", num);
			sleep(1);
		}
		printf("no data...\n");
		ioctl(fd, SET_RECV_TIMEOUT, 1000);

		sleep(5);
	}

	return NULL;

}

void * thread_revoke(void *filed){

	int fd= *((int *)filed);

	while(1){
		printf("revoking pending writes\n");
		ioctl(fd, REVOKE_DELAYED_MESSAGES);
		sleep(10);
	}

	return NULL;
}

int main(int argc, char** argv){

	int major;
	int minors;
	int fd=0;
	char *path;
	int i,j;
	pthread_t tid_write[WRITES], tid_read[READS], tid_revoke[REVOKES];

	char buff[4096];

	if(argc<4){
		printf("usage: <prog> <pathname> <major> <minors>\n");
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

		for(j=0;j<WRITES;j++)
			pthread_create(&(tid_write[j]),NULL,thread_writer, (void *) &fd);
		for(j=0;j<READS;j++)
			pthread_create(&(tid_read[j]),NULL,thread_reader, (void *) &fd);
		for(j=0;j<REVOKES;j++)
			pthread_create(&(tid_revoke[j]),NULL,thread_revoke, (void *) &fd);

	}

	pause();
	return 0;

}
