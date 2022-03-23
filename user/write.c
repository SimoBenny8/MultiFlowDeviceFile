#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <sys/ioctl.h>
#include "ioctl.h"

char buff[4096];

int main(int argc, char** argv){

     int ret;
     int major;
     int ioctl_value1, ioctl_value2;
     char *path;
	 char* msg;
	 int fd;

     if(argc<6){
		printf("Usage: Prog Pathname Major HighVsLow BlockVsNonBlock Message\n");
		printf("Values: High = 1\n");
		printf("Values: Low = 0\n");
		printf("Values: Block = 1\n");
		printf("Values: NonBlock = 0\n");
		return -1;
     }

    path = argv[1];
    major = strtol(argv[2],NULL,10);
	ioctl_value1 = strtol(argv[3],NULL,10);
	ioctl_value2 = strtol(argv[4],NULL,10);
	msg = argv[5];
     
     
	sprintf(buff,"mknod %s%d c %d %i\n",path,0,major,0);
	system(buff);
	sprintf(buff,"%s%d",path,0);
	fd = open(buff,O_RDWR|O_APPEND);
	if (fd < 0) {
		printf("Could not open device\n");
	}
	int ms = 100;
	if (ioctl_value1 == 1 && ioctl_value2 == 1){
		ioctl(fd,HP_B,ms);
	}else if(ioctl_value1 == 0 && ioctl_value2 == 1){
		ioctl(fd,LP_B,ms);
	}else if(ioctl_value1 == 1 && ioctl_value2 == 0){
		ioctl(fd,HP_NB,ms);
	}else if(ioctl_value1 == 0 && ioctl_value2 == 0){
		ioctl(fd,LP_NB,ms);
	}else{
		printf("Value ioctl not valid\n");
		return -1;
	}
	
	write(fd, msg, strlen(msg));

	if (close(fd) < 0) {
		printf("Could not close\n");
	}
		
     
	
    return 0;

}
