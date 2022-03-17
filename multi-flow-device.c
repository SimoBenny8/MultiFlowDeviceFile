
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
#include <linux/types.h>
#include <asm/atomic.h>
#include <stdatomic.h>
#include <linux/string.h>

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
} object_state;

typedef struct _packed_work{
        struct file *filp;
        char *buffer;
        size_t len; 
        long long int off;
        struct work_struct the_work;
} packed_work;

typedef struct _session_struct{
    int is_in_high_prior;
    int blocking;
    int32_t timeout;
}session_struct;


#define MINORS 128

object_state objects[MINORS];
int status[MINORS];
int num_byte_hp[MINORS];
int num_byte_lp[MINORS];
int num_th_in_queue_hp[MINORS];
int num_th_in_queue_lp[MINORS];

module_param_array(status,int,NULL,S_IRUSR|S_IWUSR);
module_param_array(num_byte_hp,int,NULL,S_IRUSR|S_IWUSR);
module_param_array(num_byte_lp,int,NULL,S_IRUSR|S_IWUSR);
module_param_array(num_th_in_queue_hp,int,NULL,S_IRUSR|S_IWUSR);
module_param_array(num_th_in_queue_lp,int,NULL,S_IRUSR|S_IWUSR);

#define OBJECT_MAX_SIZE (4096) // just one page

static void workqueue_writefn(struct work_struct* work)
{
      
      packed_work * device;
      session_struct *session;
      int minor;
      int len;
      char* buff;
      long long int offset;
      int ret;
      //prendere lock e differenziare se bloccante o no
      object_state *the_object;
      printk(KERN_INFO "Executing Workqueue Function\n");
      device = (packed_work*)container_of(work,packed_work,the_work);
      session = (device -> filp) -> private_data;

      len = device -> len;
      buff = device -> buffer;
      offset = device -> off;
      printk(KERN_INFO "Container_of eseguita \n");
      
      minor = get_minor(device -> filp);
      printk(KERN_INFO "Get minor eseguita\n");
      if(minor < 0){
        printk(KERN_INFO "Error minor work queue\n");
      }
      the_object = objects + minor;
      printk(KERN_INFO "Get the_object eseguita\n");
       if (session -> blocking) 
       {
         //mutex_lock_interruptible(&(the_object->lp_operation_synchronizer));
        atomic_fetch_add(num_th_in_queue_lp[minor],1);
        ret = wait_event_timeout(the_object -> lp_queue, mutex_trylock(&(the_object->lp_operation_synchronizer)), (HZ)*(session -> timeout));
        atomic_fetch_sub(num_th_in_queue_hp[minor],1);
        //num_th_in_queue_hp[minor] += 1;
        if(!ret){
          printk("Timeout expired\n");
          return NULL;
        }

        printk(KERN_INFO "Preso lock\n");
       } else{
         
         ret = mutex_trylock(&(the_object->lp_operation_synchronizer));
         if (ret == EBUSY){
           printk("Lock busy\n");
         }
         printk(KERN_INFO "Preso lock\n");
       }
  
 
  offset += the_object -> low_prior_valid_bytes;
  printk(KERN_DEBUG "aggiorna offset eseguita: %lld\n", device -> off);

  if (offset >= OBJECT_MAX_SIZE)
  { // offset too large
    free_page((unsigned long)(device ->buffer));
    kfree(device);
    mutex_unlock(&(the_object->lp_operation_synchronizer));
    wake_up(&(the_object -> lp_queue));
  }
  if ((offset > the_object->low_prior_valid_bytes))
  { // offset beyond the current stream size
    free_page((unsigned long)(device ->buffer));
    kfree(device);
    mutex_unlock(&(the_object->lp_operation_synchronizer));
    wake_up(&(the_object -> lp_queue));
  }

  if ((OBJECT_MAX_SIZE - offset) < len) len = OBJECT_MAX_SIZE - (offset);

 
  strncat(the_object -> low_prior_stream_content,buff, len);
  printk(KERN_INFO "Contenuto scritto: %s, con offset: %lld\n", the_object->low_prior_stream_content, offset);

  offset += len;
  the_object->low_prior_valid_bytes = offset;
  num_byte_lp[minor] += len;
  free_page((unsigned long)(device ->buffer));
  kfree(device);
  mutex_unlock(&(the_object->lp_operation_synchronizer));
  wake_up(&(the_object -> lp_queue));
  printk(KERN_INFO "Finished Workqueue Function\n");  
                                          
  
      
}


/* the actual driver */

static int dev_open(struct inode *inode, struct file *file)
{

  int minor;
  minor = get_minor(file);

  file -> private_data = (session_struct*) kzalloc(sizeof(session_struct), GFP_KERNEL);
  ((session_struct*) (file->private_data))-> timeout = 0;
  ((session_struct*) (file->private_data)) -> blocking = 0;
  ((session_struct*) (file->private_data)) -> is_in_high_prior = 0;

  if (minor >= MINORS)
  {
    return -ENODEV;
  }
  if(status[minor] == 0){
    
    printk("Device disabled \n");
    return -1;
  }

  printk("%s: device file successfully opened for object with minor %d\n", MODNAME, minor);
  // device opened by a default nop
  return 0;
}

static int dev_release(struct inode *inode, struct file *file)
{

  int minor;
  minor = get_minor(file);
  kfree(file -> private_data);

  printk("%s: device file closed\n", MODNAME);
  // device closed by default nop
  return 0;
}
//sincroni per hp e asincrono per lp
static ssize_t dev_write(struct file *filp, const char *buff, size_t len, loff_t *off)
{

  int minor = get_minor(filp);
  int ret;
  int prior;
  int ret_wq;
  int ret_queue;
  packed_work* packed_work_sched;
  session_struct *session;

  //wait_queue_head_t data;
  object_state *the_object;
  
  the_object = objects + minor;
  
  packed_work_sched = kzalloc(sizeof(packed_work), GFP_ATOMIC);
  session = filp -> private_data;
  prior = session ->is_in_high_prior;

  if(packed_work_sched == NULL){
    return -ENOSPC;
  }
  printk("%s: work buffer allocation success - address is %p\n",MODNAME,packed_work_sched);
  
  printk("%s: somebody called a write on dev with [major,minor] number [%d,%d]\n",MODNAME,get_major(filp),get_minor(filp));

  if (session->blocking) //TODO: fare i 4 casi blocking vs non-blocking e prior vs non-prior
  { 
    if(prior){
      //caso con waitqueue
      printk(KERN_INFO "Case Blocking with priority\n");

        atomic_fetch_add(num_th_in_queue_hp[minor],1);      
        ret_wq = wait_event_timeout(the_object -> hp_queue, mutex_trylock(&(the_object->hp_operation_synchronizer)), (HZ)*session -> timeout);
        atomic_fetch_sub(num_th_in_queue_hp[minor],1);
        if(!ret_wq){
          printk("Timeout expired\n");
          return -ETIMEDOUT;
        }
     

      }else{
        //caso deferred work
        printk(KERN_INFO "Case Blocking with non priority\n");
        packed_work_sched -> buffer = (char *)__get_free_page(GFP_KERNEL);
        ret = copy_from_user(packed_work_sched -> buffer, buff,len);
        if (ret == (int) len){
          return -ENOBUFS;
        }
        
        packed_work_sched -> filp = filp;
        packed_work_sched -> len = len;
        packed_work_sched -> off = *off;
       
        
        
        INIT_WORK(&(packed_work_sched -> the_work),workqueue_writefn);
              
        atomic_fetch_add(&num_th_in_queue_lp[minor],1);
        ret_queue = queue_work(the_object -> lp_workqueue,&(packed_work_sched -> the_work));
        atomic_fetch_sub(&num_th_in_queue_lp[minor],1);
        //num_th_in_queue_lp[minor] += 1;
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
        atomic_fetch_add(num_th_in_queue_hp[minor],1);
        ret_wq = wait_event_timeout(the_object -> hp_queue, mutex_trylock(&(the_object->hp_operation_synchronizer)), (HZ)*session -> timeout);
        atomic_fetch_sub(num_th_in_queue_hp[minor],1);
        if(!ret_wq){
          printk("Timeout expired\n");
          return -ETIMEDOUT;
        }
      

    }else{
       //caso deferred work
        printk(KERN_INFO "Case Non Blocking with non priority\n");
        packed_work_sched -> buffer = (char *)__get_free_page(GFP_KERNEL);
        ret = copy_from_user(packed_work_sched -> buffer, buff,len);
        if (ret == len){
          return -ENOBUFS;
        }
        

        packed_work_sched -> filp = filp;
        packed_work_sched -> len = len;
        packed_work_sched -> off = *off;


        INIT_WORK(&(packed_work_sched -> the_work),workqueue_writefn);

        atomic_fetch_add(num_th_in_queue_lp[minor],1);
        ret_queue = queue_work(the_object -> lp_workqueue,&(packed_work_sched -> the_work));
        atomic_fetch_sub(&num_th_in_queue_lp[minor],1);
        //num_th_in_queue_lp[minor] += 1; //fare check
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
  if (((!session -> is_in_high_prior) && *off > the_object->low_prior_valid_bytes) || (session -> is_in_high_prior && *off > the_object->high_prior_valid_bytes))
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

  if (session->is_in_high_prior)
  {
    ret = copy_from_user(&(the_object->high_prior_stream_content[*off]), buff, len);
    //num_th_in_queue_hp[minor] -= 1;
    //num_byte_hp[minor] = len;
  }
  else
  {
    ret = copy_from_user(&(the_object->low_prior_stream_content[*off]), buff, len);
    //num_th_in_queue_lp[minor] -= 1;
    //num_byte_lp[minor] = len;
  }

  *off += (len - ret);

  if(prior){
    the_object->high_prior_valid_bytes = *off;
    num_byte_hp[minor] += (len - ret);
  }else{
    the_object->low_prior_valid_bytes = *off;
    num_byte_lp[minor] += (len - ret);
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
//TODO: controllare se read viene chiamata prima della write (per questo non legge)
static ssize_t dev_read(struct file *filp, char *buff, size_t len, loff_t *off)
{

  int minor = get_minor(filp);
  int ret;
  int prior;
  int ret_mutex;
  int ret_wq;
  object_state *the_object;
  session_struct *session;

  the_object = objects + minor;
  session = filp -> private_data;
  
  prior = session -> is_in_high_prior;
  printk("%s: somebody called a read on dev with [major,minor] number [%d,%d]\n", MODNAME, get_major(filp), get_minor(filp));

  if (session->blocking)
  {
    if(prior){

        atomic_fetch_add(num_th_in_queue_hp[minor],1);
        ret_wq = wait_event_timeout(the_object -> hp_queue, mutex_trylock(&(the_object->hp_operation_synchronizer)), (HZ)*session -> timeout);
        atomic_fetch_sub(num_th_in_queue_hp[minor],1);
        if(!ret_wq){
          printk("Timeout wait queue Read op expired\n");
          return -ETIMEDOUT;
        }
      
      
    }else{
        atomic_fetch_add(num_th_in_queue_lp[minor],1);
        ret_wq = wait_event_timeout(the_object -> lp_queue, mutex_trylock(&(the_object->lp_operation_synchronizer)), (HZ)*session -> timeout);
        atomic_fetch_sub(num_th_in_queue_hp[minor],1);
        if(!ret_wq){
          printk("Timeout wait queue Read op expired\n");
          return -ETIMEDOUT;
        }
     
    }
    
  }
  else
  {
    if(prior){
      printk(KERN_INFO " Read case Non Blocking with priority\n");
      ret_mutex = mutex_trylock(&(the_object->hp_operation_synchronizer));//controllare EBUSY
      if (ret_mutex == EBUSY){
        return -EBUSY;
      }
    }else{
      printk(KERN_INFO " Read case Non Blocking with non priority\n");
      ret_mutex = mutex_trylock(&(the_object->lp_operation_synchronizer));

      if (ret_mutex == EBUSY){
        return -EBUSY;
      }
    
    }
  }
  if (((!session -> is_in_high_prior) && *off > the_object->low_prior_valid_bytes) || (session -> is_in_high_prior && *off > the_object->high_prior_valid_bytes))
  {
    if(prior){
      mutex_unlock(&(the_object->hp_operation_synchronizer));
      wake_up_interruptible(&the_object ->hp_queue);
    }else{
      mutex_unlock(&(the_object->lp_operation_synchronizer));
      wake_up_interruptible(&the_object ->lp_queue);
    }
    
    return 0;
  }

  if (((!session -> is_in_high_prior) && (the_object->low_prior_valid_bytes - *off) < len)){
    
    len = the_object->low_prior_valid_bytes - *off;

  }else if ((session -> is_in_high_prior) && (the_object->high_prior_valid_bytes - *off) < len){

    len = the_object->high_prior_valid_bytes - *off;

  }

  if (session->is_in_high_prior)
  { 
    char* buffer_tmp;
    //logica: salva in un buff tampone la residua stringa, azzera buf e poi copia residua in buf
    ret = copy_to_user(buff, &(the_object->high_prior_stream_content[*off]), len);
    buffer_tmp = kzalloc((strlen(the_object->high_prior_stream_conten)) - (len-ret)), GFP_KERNEL);
    char *p = the_object->high_prior_stream_content + (len - ret);
    memcpy(buffer_tmp,p, strlen(p));
    memset(the_object->high_prior_stream_content,0,strlen(the_object->high_prior_stream_content);
    memcpy(the_object->high_prior_stream_content,buffer_tmp,strlen(buffer_tmp));
    kfree(buffer_tmp);
    num_byte_hp[minor] -= (len - ret);
    
    printk("Stream prima di memset: %s, con byte letti: %ld\n", the_object->high_prior_stream_content, len-ret);
    
    printk("Stream dopo memset: %s\n", the_object->high_prior_stream_content);
    the_object -> high_prior_valid_bytes -= (len - ret);
  }
  else{
    char* buffer_tmp;
    ret = copy_to_user(buff, &(the_object->low_prior_stream_content[*off]), len);
    buffer_tmp = kzalloc((strlen(the_object->low_prior_stream_conten)) - (len-ret)), GFP_KERNEL);
    char *p = the_object->low_prior_stream_content + (len - ret);
    memcpy(buffer_tmp,p, strlen(p));
    memset(the_object->low_prior_stream_content,0,strlen(*(the_object->low_prior_stream_content));
    memcpy(the_object->low_prior_stream_content,buffer_tmp,strlen(buffer_tmp));


    num_byte_lp[minor] -= (len - ret);
    printk("Stream prima di memset: %s, con byte letti: %ld\n", the_object->low_prior_stream_content, len-ret);
    memset(the_object->low_prior_stream_content,0,len-ret);
    printk("Stream dopo memset: %s\n", the_object->low_prior_stream_content);
    the_object->low_prior_stream_content += (len - ret); //prova cancellazione contenuto
    the_object -> low_prior_valid_bytes -= (len - ret);
  }

  *off += (len - ret);
  if(prior){
    mutex_unlock(&(the_object->hp_operation_synchronizer));
    wake_up_interruptible(&the_object ->hp_queue);
  }else{
    mutex_unlock(&(the_object->lp_operation_synchronizer));
    wake_up_interruptible(&the_object ->lp_queue);
  }
  
  return len - ret;
}

static long dev_ioctl(struct file *filp, unsigned int command, unsigned long param)
{
//aggiungere gestione sessione chiusa
  int minor = get_minor(filp);
  object_state *the_object;
  session_struct *session = filp -> private_data;

  the_object = objects + minor;
  printk("%s: somebody called an ioctl on dev with [major,minor] number [%d,%d] and command %u \n", MODNAME, get_major(filp), get_minor(filp), command);

  switch (command)
  {
  case HP_B:
    session ->is_in_high_prior = 1;
    session ->blocking = 1;
    session ->timeout = (int32_t) param;
    printk("Inserimento parametri HP_B effettuato\n");
    break;

  case HP_NB:
    session ->is_in_high_prior = 1;
    session ->blocking = 0;
    session ->timeout = (int32_t) param;
    printk("Inserimento parametri HP_NB effettuato\n");
    break;
  
   case LP_NB:
    session ->is_in_high_prior = 0;
    session ->blocking = 0;
    session ->timeout = (int32_t) param;
    printk("Inserimento parametri LP_NB effettuato\n");
    break;
  
   case LP_B:
    session ->is_in_high_prior = 0;
    session ->blocking = 1;
    session ->timeout = (int32_t) param;
    printk("Inserimento parametri LP_B effettuato\n");
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
    status[i] = 1;
    num_byte_hp[i] = 0;
    num_byte_lp[i] = 0;
    num_th_in_queue_hp[i] = 0;
    num_th_in_queue_lp[i] = 0;
    mutex_init(&(objects[i].lp_operation_synchronizer));
    mutex_init(&(objects[i].hp_operation_synchronizer));
    init_waitqueue_head(&(objects[i].hp_queue));
    init_waitqueue_head(&(objects[i].lp_queue));
    objects[i].lp_workqueue = create_singlethread_workqueue("wq");
    if ( !objects[i].lp_workqueue ) {
		printk( "create workqueue failed\n" );
		
	  }
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
