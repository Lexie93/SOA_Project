#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>

int main(int argc, char** argv){

     int size, fd, red;
     char *path;
     char buff[4096];

     if(argc<3){
	printf("useg: prog pathname size");
	return -1;
     }

     path = argv[1];
     size=strtol(argv[2], NULL, 10);

     fd=open(path, O_RDWR);
     if(fd == -1) {
		printf("open error on device %s\n",path);
		return -1;
	}
	red=read(fd, buff, size);
	printf("%s\nred: %d\n", buff, red);

	pause();
     return 0;

}