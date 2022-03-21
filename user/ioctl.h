#ifndef QUERY_IOCTL_H
#define QUERY_IOCTL_H

/* _IO, _IOW, _IOR, _IORW are helper macros to create a unique ioctl identifier and 
   add the required R/W needed features (direction).
   These can take the following params: magic number, the command id, and the data
   type that will be passed (if any)
*/
#define IOCTL_APP_TYPE 80

#define HP_NB _IOWR(IOCTL_APP_TYPE,1,int32_t*)
#define HP_B _IOWR(IOCTL_APP_TYPE,2,int32_t*)
#define LP_NB _IOWR(IOCTL_APP_TYPE,3,int32_t*)
#define LP_B _IOWR(IOCTL_APP_TYPE,4,int32_t*)

#define EN_DIS _IOWR(IOCTL_APP_TYPE,5,int32_t*)

#endif
