#ifndef QUERY_IOCTL_H
#define QUERY_IOCTL_H

/* _IO, _IOW, _IOR, _IORW are helper macros to create a unique ioctl identifier and 
   add the required R/W needed features (direction).
   These can take the following params: magic number, the command id, and the data
   type that will be passed (if any)
*/
#define IOCTL_APP_TYPE 80

#define HP_NB _IORW(IOCTL_APP_TYPE,1,unsigned long)
#define HP_B _IORw(IOCTL_APP_TYPE,2,unsigned long)
#define LP_NB _IORW(IOCTL_APP_TYPE,3,unsigned long)
#define LP_B _IORw(IOCTL_APP_TYPE,4,unsigned long)

#endif
