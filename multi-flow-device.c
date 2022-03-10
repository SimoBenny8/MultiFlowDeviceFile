
/*
 *  multi-flow device driver with limitation on minor numbers (128)
 */

#define EXPORT_SYMTAB
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/sched.h>
#include <linux/pid.h>     /* For pid types */
#include <linux/tty.h>     /* For the tty declarations */
#include <linux/version.h> /* For LINUX_VERSION_CODE */
#include <linux/spinlock.h>
#include <linux/ioctl.h>
#include <linux/wait.h>
#include <linux/jiffies.h>
#include <linux/workqueue.h> 
#include <linux/errno.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <linux/delay.h>

#include "ioctl.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Simone Benedetti");

#define MODNAME "MULTI FLOW DEVICE"
#define DEVICE_NAME "multiflowdev" /* Device file name in /dev/ - not mandatory  */

static int Major; /* Major number assigned to broadcast device driver */

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 0, 0)
#define get_major(session) MAJOR(session->f_inode->i_rdev)
#define get_minor(session) MINOR(session->f_inode->i_rdev)
#else
#define get_major(session) MAJOR(session->f_dentry->d_inode->i_rdev)
#define get_minor(session) MINOR(session->f_dentry->d_inode->i_rdev)
#endif

typedef struct _object_state
{
  struct mutex lp_operation_synchronizer;
  struct mutex hp_operation_synchronizer;
  wait_queue_head_t hp_queue;
  wait_queue_head_t lp_queue;
  struct workqueue_struct* lp_workqueue;
  int low_prior_valid_bytes;
  int high_prior_valid_bytes;
  char *low_prior_stream_content;
  char *high_prior_stream_content; // the I/O node is a buffer in memory
  int is_in_high_prior;
  int blocking;
  int32_t timeout;
  char flag;
} object_state;

typedef struct _packed_work{
        struct file *filp;
        char *buffer;
        size_t len; 
        long long int off;
        struct work_struct the_work;
} packed_work;


#define MINORS 128

static object_state objects[MINORS];
//static struct workqueue_struct* lp_workqueue[MINORS];

#define OBJECT_MAX_SIZE (4096) // just one page

static void workqueue_writefn(struct work_struct* work)
{
      
      int ret;
      
  
      //prendere lock e differenziare se bloccante o no
      object_state *the_object;
      printk(KERN_INFO "Executing Workqueue Function\n");
      packed_work * device = (packed_work*)container_of(work,packed_work,the_work);
      int len = device -> len;
      char* buff = device -> buffer;
      long long int offset = device -> off;
      printk(KERN_INFO "Container_of eseguita \n");
      
      int minor = get_minor(device -> filp);
      printk(KERN_INFO "Get minor eseguita\n");
      if(minor < 0){
        printk(KERN_INFO "Error minor work queue\n");
      }
      the_object = objects + minor;
      printk(KERN_INFO "Get the_object eseguita\n");
       if (the_object->blocking) 
       {
         mutex_lock_interruptible(&(the_object->lp_operation_synchronizer));
         printk(KERN_INFO "Preso lock\n");
       } else{
         mutex_trylock(&(the_object->lp_operation_synchronizer));
         printk(KERN_INFO "Preso lock\n");
       }
  
  //*(device -> off) = 0;
  //printk(KERN_DEBUG "aggiorna offset a zero: %lld\n", device -> off);
  offset += the_object -> low_prior_valid_bytes;
  printk(KERN_DEBUG "aggiorna offset eseguita: %lld\n", device -> off);

  if (offset >= OBJECT_MAX_SIZE)
  { // offset too large
    mutex_unlock(&(the_object->lp_operation_synchronizer));
  }
  if (((!the_object -> is_in_high_prior) && offset > the_object->low_prior_valid_bytes))
  { // offset beyond the current stream size
    mutex_unlock(&(the_object->lp_operation_synchronizer));
  }

  if ((OBJECT_MAX_SIZE - offset) < len) len = OBJECT_MAX_SIZE - (offset);

  //%d: contenuto di len, %d: contenuto di offset, , device -> len, device -> off
  //printk("%ld: lunghezza del buffer, \n", device ->len);
 // printk("%s: contenuto del buffer, \n", device ->buff);
  ret = copy_from_user(&(the_object->low_prior_stream_content[offset]), buff, len);
  printk(KERN_INFO "Copy to user eseguita con contenuto scritto: %s\n", the_object->low_prior_stream_content);

  offset += (offset - ret);
  the_object->low_prior_valid_bytes = offset;
  
  mutex_unlock(&(the_object->lp_operation_synchronizer));
  printk(KERN_INFO "Finished Workqueue Function\n");                                          
  
      
}

/* the actual driver */

static int dev_open(struct inode *inode, struct file *file)
{

  int minor;
  minor = get_minor(file);

  if (minor >= MINORS)
  {
    return -ENODEV;
  }

  printk("%s: device file successfully opened for object with minor %d\n", MODNAME, minor);
  // device opened by a default nop
  return 0;
}

static int dev_release(struct inode *inode, struct file *file)
{

  int minor;
  minor = get_minor(file);

  printk("%s: device file closed\n", MODNAME);
  // device closed by default nop
  return 0;
}
//sincroni per hp e asincrono per lp
static ssize_t dev_write(struct file *filp, const char *buff, size_t len, loff_t *off)
{

  int minor = get_minor(filp);
  int ret;
  int ret_mutex;
  int prior;
  packed_work* packed_work_sched;

  //wait_queue_head_t data;
  object_state *the_object;
  
  the_object = objects + minor;
  prior = the_object ->is_in_high_prior;
  packed_work_sched = kzalloc(sizeof(packed_work), GFP_ATOMIC);

  if(packed_work_sched == NULL){
    return -ENOSPC;
  }
  printk("%s: work buffer allocation success - address is %p\n",MODNAME,packed_work_sched);
  //packed_work_sched.the_work = kmalloc(sizeof(struct work_struct), GFP_KERNEL);
  // printk("%s: somebody called a write on dev with [major,minor] number [%d,%d]\n",MODNAME,get_major(filp),get_minor(filp));

  if (the_object->blocking) //TODO: fare i 4 casi blocking vs non-blocking e prior vs non-prior
  { 
    if(prior){
      //caso con waitqueue
      printk(KERN_INFO "Case Blocking with priority\n");

      ret_mutex = mutex_lock_interruptible(&(the_object->hp_operation_synchronizer));
      if (ret_mutex != 0){
        //init_waitqueue_entry(&wait, current);
        int ret_wq = wait_event_timeout(the_object -> hp_queue, mutex_lock_interruptible(&(the_object->hp_operation_synchronizer)) == 0, (HZ)*the_object -> timeout);
        
        if(!ret_wq){
          printk("Timeout expired\n");
          return -ETIMEDOUT;
        }
      }

      }else{
        //caso deferred work
        packed_work_sched -> buffer = (char *)__get_free_page(GFP_KERNEL);
        //strscpy(packed_work_sched -> buffer, buff, len);
        int ret = copy_from_user(packed_work_sched -> buffer, buff,len);
        if (ret == (int) len){
          return -ENOBUFS;
        }
        //packed_work_sched -> buffer = kstrndup(buff,len, GFP_KERNEL);
        

        packed_work_sched -> filp = filp;
        packed_work_sched -> len = len;
        packed_work_sched -> off = *off;
        printk(KERN_INFO "Case Blocking with non priority\n");
        //printk("%s: contenuto del buffer\n", packed_work_sched -> buff);
        
        INIT_WORK(&(packed_work_sched -> the_work),workqueue_writefn);
        //__INIT_WORK(&(packed_work_sched -> the_work),(void*) workqueue_writefn,(&(packed_work_sched -> the_work)));
      

       int ret_queue = queue_work(the_object -> lp_workqueue,&(packed_work_sched -> the_work));
        if(!ret_queue){
          return -EALREADY;
        }
        return 30; //scegliere codice di errore per questo caso
      }
        

    
    printk("%s: somebody called a blocked write on dev with [major,minor] number [%d,%d]\n", MODNAME, get_major(filp), get_minor(filp));
  }
  else
  {
    if(prior){
      //caso con waitqueue
      printk(KERN_INFO "Case Non Blocking with priority\n");
      ret_mutex = mutex_trylock(&(the_object->hp_operation_synchronizer));//controllare EBUSY
      if (ret_mutex != 0){
        int ret_wq = wait_event_timeout(the_object -> hp_queue, mutex_trylock(&(the_object->hp_operation_synchronizer)) == 0, (HZ)*the_object -> timeout);
        //cambiare perchè non FIFO
        if(!ret_wq){
          printk("Timeout expired\n");
          return -ETIMEDOUT;
        }
      }

    }else{
       //caso deferred work
        printk(KERN_INFO "Case Non Blocking with non priority\n");
        packed_work_sched -> buffer = (char *)__get_free_page(GFP_KERNEL);
        //strscpy(packed_work_sched -> buffer, buff, len);
        int ret = copy_from_user(packed_work_sched -> buffer, buff,len);
        if (ret == len){
          return -ENOBUFS;
        }
        /*if (ret_st != (int) len){
          return -EINVAL;
        }*/

        packed_work_sched -> filp = filp;
        packed_work_sched -> len = len;
        packed_work_sched -> off = *off;


        INIT_WORK(&(packed_work_sched -> the_work),workqueue_writefn);
        //__INIT_WORK(&(packed_work_sched -> the_work),(void*) workqueue_writefn, (unsigned long) (&(packed_work_sched -> the_work)));
        //schedule_work(&packed_work_sched.the_work);
      

        int ret_queue = queue_work(the_object -> lp_workqueue,&(packed_work_sched -> the_work));
        if(!ret_queue){
          return -EALREADY;
        }
        return 30; //scegliere codice di errore per questo caso
       

    }
    printk("%s: somebody called a non-blocked write on dev with [major,minor] number [%d,%d]\n", MODNAME, get_major(filp), get_minor(filp));
  }

  if(prior){
    *off += the_object -> high_prior_valid_bytes;
  }else{
    *off += the_object -> low_prior_valid_bytes;
  }

  if (*off >= OBJECT_MAX_SIZE)
  { // offset too large
    
    if(prior){
      mutex_unlock(&(the_object->hp_operation_synchronizer));
      wake_up_interruptible(&the_object ->hp_queue);
    }else{
      mutex_unlock(&(the_object->lp_operation_synchronizer));
      wake_up_interruptible(&the_object ->lp_queue);
    }
    return -ENOSPC; // no space left on device
  }
  if (((!the_object -> is_in_high_prior) && *off > the_object->low_prior_valid_bytes) || (the_object -> is_in_high_prior && *off > the_object->high_prior_valid_bytes))
  { // offset beyond the current stream size
    if(prior){
      mutex_unlock(&(the_object->hp_operation_synchronizer));
      wake_up_interruptible(&the_object ->hp_queue);
    }else{
      mutex_unlock(&(the_object->lp_operation_synchronizer));
      wake_up_interruptible(&the_object ->lp_queue);
    }
    return -ENOSR; // out of stream resources
  }

  if ((OBJECT_MAX_SIZE - *off) < len) len = OBJECT_MAX_SIZE - *off;

  if (the_object->is_in_high_prior)
  {
    ret = copy_from_user(&(the_object->high_prior_stream_content[*off]), buff, len);
  }
  else
  {
    ret = copy_from_user(&(the_object->low_prior_stream_content[*off]), buff, len);
  }

  *off += (len - ret);

  if(prior){
    the_object->high_prior_valid_bytes = *off;
  }else{
    the_object->low_prior_valid_bytes = *off;
  }
  

  //mutex_unlock(&(the_object->operation_synchronizer));
   if(prior){
     mutex_unlock(&(the_object->hp_operation_synchronizer));
     wake_up_interruptible(&the_object ->hp_queue);
    }else{
      mutex_unlock(&(the_object->lp_operation_synchronizer));
      wake_up_interruptible(&the_object ->lp_queue);
    }
  printk(KERN_INFO "finisched write\n");
  return len - ret;
}

//sempre sincrone
static ssize_t dev_read(struct file *filp, char *buff, size_t len, loff_t *off)
{

  int minor = get_minor(filp);
  int ret;
  int prior;
  object_state *the_object;

  the_object = objects + minor;
  prior = the_object -> is_in_high_prior;
  printk("%s: somebody called a read on dev with [major,minor] number [%d,%d]\n", MODNAME, get_major(filp), get_minor(filp));

  if (the_object->blocking)
  {
    if(prior){
      mutex_lock_interruptible(&(the_object->hp_operation_synchronizer));
    }else{
      mutex_lock_interruptible(&(the_object->lp_operation_synchronizer));
    }
    
  }
  else
  {
    if(prior){
      if(!mutex_trylock(&(the_object->hp_operation_synchronizer))){
      return -EBUSY;
      }
    }else{
      if(!mutex_trylock(&(the_object->lp_operation_synchronizer))){
      return -EBUSY;
    }
    }
    
  }
  if (((!the_object -> is_in_high_prior) && *off > the_object->low_prior_valid_bytes) || (the_object -> is_in_high_prior && *off > the_object->high_prior_valid_bytes))
  {
    if(prior){
      mutex_unlock(&(the_object->hp_operation_synchronizer));
    }else{
      mutex_unlock(&(the_object->lp_operation_synchronizer));
    }
    
    return 0;
  }

  if (((!the_object -> is_in_high_prior) && (the_object->low_prior_valid_bytes - *off) < len)){
    
    len = the_object->low_prior_valid_bytes - *off;

  }else if ((the_object -> is_in_high_prior) && (the_object->high_prior_valid_bytes - *off) < len){

    len = the_object->high_prior_valid_bytes - *off;

  }

  if (the_object->is_in_high_prior)
  {
    ret = copy_to_user(buff, &(the_object->high_prior_stream_content[*off]), len);
    the_object->high_prior_stream_content += len; //prova cancellazione contenuto
    the_object -> high_prior_valid_bytes -= len;
  }
  else
  {
    ret = copy_to_user(buff, &(the_object->low_prior_stream_content[*off]), len);
    //the_object->low_prior_stream_content[0] = the_object->low_prior_stream_content[*off + ret];
    the_object->low_prior_stream_content += len; //prova cancellazione contenuto
    the_object -> low_prior_valid_bytes -= len;
  }

  *off += (len - ret);
  if(prior){
    mutex_unlock(&(the_object->hp_operation_synchronizer));
  }else{
    mutex_unlock(&(the_object->lp_operation_synchronizer));
  }
  
  return len - ret;
}

static long dev_ioctl(struct file *filp, unsigned int command, unsigned long param)
{

  int minor = get_minor(filp);
  object_state *the_object;

  the_object = objects + minor;
  printk("%s: somebody called an ioctl on dev with [major,minor] number [%d,%d] and command %u \n", MODNAME, get_major(filp), get_minor(filp), command);

  switch (command)
  {
  case HP_B:
    the_object->is_in_high_prior = 1;
    the_object->blocking = 1;
    the_object->timeout = (int32_t) param;
    printk("Inserimento parametri effettuato\n");
    break;

  case HP_NB:
    the_object->is_in_high_prior = 1;
    the_object->blocking = 0;
    the_object->timeout = (int32_t) param;
    printk("Inserimento parametri effettuato\n");
    break;
  
   case LP_NB:
    the_object->is_in_high_prior = 0;
    the_object->blocking = 0;
    the_object->timeout = (int32_t) param;
    printk("Inserimento parametri effettuato\n");
    break;
  
   case LP_B:
    the_object->is_in_high_prior = 0;
    the_object->blocking = 1;
    the_object->timeout = (int32_t) param;
    printk("Inserimento parametri effettuato\n");
    break;
  
  }
 

  return 0;
}

static struct file_operations fops = {
    .owner = THIS_MODULE, // do not forget this
    .write = dev_write,
    .read = dev_read,
    .open = dev_open,
    .release = dev_release,
    .unlocked_ioctl = dev_ioctl
};

int init_module(void)
{

  int i;
  

  // initialize the drive internal state
  for (i = 0; i < MINORS; i++)
  {

    mutex_init(&(objects[i].lp_operation_synchronizer));
    mutex_init(&(objects[i].hp_operation_synchronizer));
    init_waitqueue_head(&(objects[i].hp_queue));
    init_waitqueue_head(&(objects[i].lp_queue));
    objects[i].lp_workqueue = create_singlethread_workqueue("wq");
    if ( !objects[i].lp_workqueue ) {
		printk( "create workqueue failed\n" );
		
	  }
    //INIT_WORK(&(objects[i].lp_workqueue),workqueue_writefn);
    objects[i].blocking = 0;
    objects[i].timeout = 0;
    objects[i].low_prior_valid_bytes = 0;
    objects[i].high_prior_valid_bytes = 0;
    objects[i].low_prior_stream_content = NULL;
    objects[i].low_prior_stream_content = (char *)__get_free_page(GFP_KERNEL);
    objects[i].high_prior_stream_content = NULL;
    objects[i].high_prior_stream_content = (char *)__get_free_page(GFP_KERNEL);
    if (objects[i].high_prior_stream_content == NULL || objects[i].low_prior_stream_content == NULL)
      goto revert_allocation;
  }

  Major = __register_chrdev(0, 0, 128, DEVICE_NAME, &fops);
  // actually allowed minors are directly controlled within this driver

  if (Major < 0)
  {
    printk("%s: registering device failed\n", MODNAME);
    return Major;
  }

  printk(KERN_INFO "%s: new device registered, it is assigned major number %d\n", MODNAME, Major);

  return 0;

revert_allocation:
  for (; i >= 0; i--)
  {
    destroy_workqueue(objects[i].lp_workqueue);
    free_page((unsigned long)objects[i].low_prior_stream_content);
    free_page((unsigned long)objects[i].high_prior_stream_content);
  }
  return -ENOMEM;
}

void cleanup_module(void)
{

  int i;
  for (i = 0; i < MINORS; i++)
  {
    destroy_workqueue(objects[i].lp_workqueue);
    free_page((unsigned long)objects[i].low_prior_stream_content);
    free_page((unsigned long)objects[i].high_prior_stream_content);
  }

  unregister_chrdev(Major, DEVICE_NAME);

  printk(KERN_INFO "%s: new device unregistered, it was assigned major number %d\n", MODNAME, Major);

  return;
}
