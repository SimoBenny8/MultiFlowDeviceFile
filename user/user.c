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
#define DATA "ciao a tutti\n"
#define SIZE strlen(DATA)

void * the_thread_write(void* path){

	char* device;
	int fd;

	device = (char*)path;
	sleep(1);

	printf("opening device %s\n",device);
	fd = open(device,O_RDWR|O_APPEND);
	if(fd == -1) {
		printf("open error on device %s\n",device);
		return NULL;
	}
	printf("device %s successfully opened\n",device);
	int32_t sec = 1;
	ioctl(fd,LP_B,(int32_t*) &sec);
	//for(i=0;i<5;i++) 
	write(fd,DATA,SIZE);
	close(fd);
	return NULL;

}

void * the_thread_read(void* path){

	char* device;
	int fd,retval;
	char *buffer;

	device = (char*)path;
	sleep(1);

	printf("opening device %s\n",device);
	fd = open(device,O_RDWR|O_APPEND);
	if(fd == -1) {
		printf("open error on device %s\n",device);
		return NULL;
	}
	printf("device %s successfully opened\n",device);
	int32_t sec = 1;
	ioctl(fd,LP_NB,(int32_t*) &sec);
	while ((retval = read(fd, buffer, 1)) > 0)
        printf("%c", *buffer);    
    if (retval < 0) {
        perror("\nRead failure");
        exit(1);
    }
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
	pthread_create(&tid,NULL,the_thread_write,strdup(buff));
	//pthread_create(&tid,NULL,the_thread_read,strdup(buff));
     }

     pause();
     return 0;

}
