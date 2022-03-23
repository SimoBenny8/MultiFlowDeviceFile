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
#define DATA "Hello"
#define SIZE strlen(DATA)

void * the_thread_write(void* path){

	char* device;
	int fd;

	device = (char*)path;

	printf("opening device %s\n",device);
	fd = open(device,O_RDWR|O_APPEND);
	if(fd == -1) {
		printf("open error on device %s\n",device);
		return NULL;
	}
	printf("device %s successfully opened\n",device);
	int ms = 100;
	ioctl(fd,HP_B,ms);
	for(int i= 0; i<5; i++) write(fd,DATA,SIZE);
	close(fd);
	return NULL;

}

void * the_thread_read(void* path){

	char* device;
	int fd,retval;
	char *buffer = malloc(5);

	if(buffer == NULL){
		printf("Error alloc buffer\n");
		return NULL;
	}

	device = (char*)path;

	printf("opening device %s\n",device);
	fd = open(device,O_RDWR|O_APPEND);
	if(fd == -1) {
		printf("open error on device %s\n",device);
		return NULL;
	}
	printf("device %s successfully opened\n",device);
	int ms = 50;
	ioctl(fd,HP_B, ms);
	int ret = read(fd, buffer, 5);
	if (ret != 0){
		printf("buffer read: %s\n", buffer);  
	}
	free(buffer);
	close(fd);
	return NULL;

}

void * the_thread_disable_device(void* path){

	char* device;
	int fd,retval;
	

	device = (char*)path;

	printf("opening device %s\n",device);
	fd = open(device,O_RDWR|O_APPEND);
	if(fd == -1) {
		printf("open error on device %s\n",device);
		return NULL;
	}
	printf("device %s successfully opened\n",device);
	int dis = 0;
	ioctl(fd,EN_DIS,dis); 
    
	close(fd);
	return NULL;

}


int main(int argc, char** argv){

     int ret;
     int major;
     int minors;
     char *path;
     pthread_t tid;

     if(argc<4){
		printf("useg: prog pathname major minors\n");
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
		//pthread_create(&tid,NULL,the_thread_write,strdup(buff));
		//pthread_create(&tid,NULL,the_thread_read,strdup(buff));
		pthread_create(&tid,NULL,the_thread_disable_device,strdup(buff));
     }
	pause();
    return 0;

}
