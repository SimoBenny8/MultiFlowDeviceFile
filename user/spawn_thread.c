#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <sys/ioctl.h>
#include "ioctl.h"

#define PATH "/dev/multiflowdev"
#define MSG "Hello World"

char buff[4096];

void * the_thread_read(void* path){

	char* device;
	int ret;
	int fd;
	char* msg;
	device = (char*)path;

	msg = malloc(strlen(MSG));
	if(msg == NULL){
		printf("Allocate buffer didn't work\n");
		return NULL;
	}

	printf("Opening device %s\n",device);
	fd = open(device,O_RDWR|O_APPEND);
	if(fd == -1) {
		printf("open error on device %s\n",device);
		return NULL;
	}
	printf("device %s successfully opened\n",device);
	int ms = 50;
	ioctl(fd,LP_B,ms);
	ret = read(fd,msg,strlen(MSG));
	if (ret != 0){
		printf("buffer read: %s\n", msg);  
	}
	free(msg);
	close(fd);
	return NULL;

}



void * the_thread_write(void* path){

	char* device;
	int fd;
	int ret;
	device = (char*)path;

	printf("Opening device %s\n",device);
	fd = open(device,O_RDWR|O_APPEND);
	if(fd == -1) {
		printf("open error on device %s\n",device);
		return NULL;
	}
	printf("device %s successfully opened\n",device);
	int ms = 50;
	ioctl(fd,LP_B,ms);
	write(fd,MSG,strlen(MSG));

	close(fd);
	return NULL;

}


int main(int argc, char** argv){

     int ret;
     int major;
     int minor;
	 int numThreads;
	 char* msg;
	 int fd;
	 int i,j;
	 pthread_t tid;

     if(argc<4){
		printf("Usage: Prog Major Minor NumberThreads\n");
		return -1;
     }

    major = strtol(argv[1],NULL,10);
	minor = strtol(argv[2],NULL,10);
	numThreads = strtol(argv[3],NULL,10);
     
    for(i=0; i<minor; i++){
		sprintf(buff,"mknod %s%d c %d %i\n",PATH,i,major,i);
		system(buff);
		sprintf(buff,"%s%d",PATH,i);
		for(j=0; j<numThreads;j++){
			pthread_create(&tid,NULL,the_thread_write,strdup(buff));
			//pthread_create(&tid,NULL,the_thread_read,strdup(buff));
		}
		
	} 
	pause();
    return 0;

}
