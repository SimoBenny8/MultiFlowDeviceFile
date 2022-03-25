#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <sys/ioctl.h>
#include "ioctl.h"

int i;
char buff[4096];
#define PATH "/dev/multiflowdev"
#define DATA "Hello"
#define SIZE strlen(DATA)

void * the_thread_write_and_read(void* path){

	char* device;
	int fd;
	int ret;
	char buff1[5];
	char buff2[5];
	char* msg1 = "Hello";
	char* msg2 = "World";
	device = (char*)path;

	printf("Opening device %s\n",device);
	fd = open(device,O_RDWR|O_APPEND);
	if(fd == -1) {
		printf("open error on device %s\n",device);
		return NULL;
	}
	printf("device %s successfully opened\n",device);
	int ms = 50;
	ioctl(fd,HP_B,ms);
	write(fd,msg1,strlen(msg1));
	write(fd,msg2,strlen(msg2));

	ret = read(fd, buff1, strlen(msg1));
	if (ret != 0){
		printf("First read: %s\n", buff1);  
	}

	ret = read(fd, buff2, strlen(msg2));
	if (ret != 0){
		printf("Second read: %s\n", buff2);  
	}
	

	close(fd);
	return NULL;

}



void * the_thread_disable_device(void* path){

	char* device;
	int fd,retval;
	

	device = (char*)path;

	//printf("opening device %s\n",device);
	fd = open(device,O_RDWR|O_APPEND);
	if(fd == -1) {
		printf("open error on device %s\n",device);
		return NULL;
	}
	int dis = 0;
	ioctl(fd,EN_DIS,dis);
	close(fd); 

	//Second try
	fd = open(device,O_RDWR|O_APPEND);
	if(fd == -1) {
		printf("open error on device %s\n",device);
		ioctl(fd,HP_B,10);
		retval = write(fd, DATA,SIZE); //not valid if disable
		if(retval == -1){
			printf("Write doesn't work\n");
		}
		return NULL;
	}
	//printf("device %s successfully opened\n",device);
	return NULL;

}


int main(int argc, char** argv){

     int ret;
     int major;
     int minors;
     pthread_t tid;

     if(argc<3){
		printf("usage: prog major minors\n");
		return -1;
     }

    
     major = strtol(argv[1],NULL,10);
     minors = strtol(argv[2],NULL,10);
     printf("creating %d minors for device %s with major %d\n",minors,PATH,major);

     for(i=0;i<minors;i++){
		sprintf(buff,"mknod %s%d c %d %i\n",PATH,i,major,i);
		system(buff);
		sprintf(buff,"%s%d",PATH,i);
		pthread_create(&tid,NULL,the_thread_disable_device,strdup(buff));
		//pthread_create(&tid,NULL,the_thread_write_and_read,strdup(buff));
     }
	pause();
    return 0;

}
