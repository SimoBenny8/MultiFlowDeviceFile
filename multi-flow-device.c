
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
  struct workqueue_struct *lp_workqueue;
  int low_prior_valid_bytes;
  int high_prior_valid_bytes;
  char *low_prior_stream_content;
  char *high_prior_stream_content; // the I/O node is a buffer in memory
} object_state;

typedef struct _packed_work
{
  struct file *filp;
  char *buffer;
  size_t len;
  long long int off;
  struct work_struct the_work;
} packed_work;

typedef struct _session_struct
{
  int is_in_high_prior;
  int blocking;
  int timeout;
} session_struct;

#define MINORS 128

object_state objects[MINORS];
int status[MINORS];
int num_byte_hp[MINORS];
int num_byte_lp[MINORS];
int num_th_in_queue_hp[MINORS];
int num_th_in_queue_lp[MINORS];

module_param_array(status, int, NULL, S_IRUSR | S_IWUSR);
module_param_array(num_byte_hp, int, NULL, S_IRUSR | S_IWUSR);
module_param_array(num_byte_lp, int, NULL, S_IRUSR | S_IWUSR);
module_param_array(num_th_in_queue_hp, int, NULL, S_IRUSR | S_IWUSR);
module_param_array(num_th_in_queue_lp, int, NULL, S_IRUSR | S_IWUSR);

int call_wait_queue(int minor,  wait_queue_head_t *wait_queue, struct mutex* op_sync, int timeout){
      int ret_wq;

      atomic_fetch_add(&num_th_in_queue_hp[minor], 1);
      // ret_wq returns 0 if the condition evaluated to false after the timeout elapsed
      ret_wq = wait_event_timeout(*wait_queue, mutex_trylock(op_sync), (HZ/1000)*timeout);
      atomic_fetch_sub(&num_th_in_queue_hp[minor], 1);
      return ret_wq;

}

void write_op(char** stream,int valid_bytes ,int len, char** buff){

  *stream = krealloc(*stream,(valid_bytes)+len,GFP_KERNEL);
  memset(*stream + valid_bytes,0,len);
  strncat(*stream, *buff, len);

}

static void workqueue_writefn(struct work_struct *work)
{

  packed_work *device;
  session_struct *session;
  int minor;
  int len;
  char *buff;
  long long int offset;
  

  object_state *the_object;
  printk(KERN_INFO "Executing Workqueue Function\n");
  device = (packed_work *)container_of(work, packed_work, the_work);
  session = (device->filp)->private_data;

  len = device->len;
  buff = device->buffer;
  offset = device->off;

  minor = get_minor(device->filp);
  if (minor < 0)
  {
    printk(KERN_INFO "Error minor work queue\n");
  }
  the_object = objects + minor;
  
  mutex_lock(&(the_object->lp_operation_synchronizer));
  printk(KERN_INFO "Got lock for deferred work\n");
  
  offset = 0;
  offset += the_object->low_prior_valid_bytes;
  printk(KERN_DEBUG "Offset before writing: %lld\n", offset);

  write_op(&(the_object->low_prior_stream_content),the_object->low_prior_valid_bytes ,len, &buff);
  

  offset += len;
  the_object->low_prior_valid_bytes = offset;
  num_byte_lp[minor] += len;
  printk(KERN_INFO "String content: %s, with offset: %lld\n", the_object->low_prior_stream_content, offset);
  printk(KERN_INFO "Valid bytes low prior: %d\n", the_object->low_prior_valid_bytes);
  kfree(device->buffer);
  kfree(device);
  mutex_unlock(&(the_object->lp_operation_synchronizer));
}

/* the actual driver */

static int dev_open(struct inode *inode, struct file *file)
{

  int minor;
  minor = get_minor(file);

  file->private_data = (session_struct *)kzalloc(sizeof(session_struct), GFP_KERNEL);
  ((session_struct *)(file->private_data))->timeout = 10; //milliseconds
  ((session_struct *)(file->private_data))->blocking = 0;
  ((session_struct *)(file->private_data))->is_in_high_prior = 0;

  if (minor >= MINORS)
  {
    return -ENODEV;
  }
  if (status[minor] == 0)
  {
    printk(KERN_INFO "Device disabled \n");
    return -1;
  }

  printk(KERN_INFO "%s: device file successfully opened for object with minor %d\n", MODNAME, minor);
  return 0;
}

static int dev_release(struct inode *inode, struct file *file)
{

  int minor;
  minor = get_minor(file);
  
  kfree(file->private_data);

  printk(KERN_INFO "%s: device file closed\n", MODNAME);
  return 0;
}

int call_copy_from_user(const char** buffer,size_t* len, char** temp_buffer){
  
  int ret;
  
  *temp_buffer = (char* )kzalloc(*len, GFP_ATOMIC);
  if (*temp_buffer == NULL) return 0;
  ret = copy_from_user(*temp_buffer,*buffer,*len);
  printk("First copy to temp buffer done with total bytes: %ld\n", *len - ret);
  return ret;
}

int call_deferred_work(int minor,struct workqueue_struct **wq,struct file **filp,loff_t **off,size_t* len,const char** buffer,packed_work **work){
      
      int ret;
      int ret_queue;
      
      ret = call_copy_from_user(buffer,len,&((*work)->buffer));
      if (ret > 0){
        (*work)->buffer = krealloc((*work)->buffer, *len - ret, GFP_KERNEL);
      }

      (*work) -> filp = *filp;
      (*work) -> len = *len - ret;
      (*work) -> off = **off;

      INIT_WORK(&((*work)->the_work), workqueue_writefn);

      atomic_fetch_add(&num_th_in_queue_lp[minor], 1);
      ret_queue = queue_work(*wq, &((*work)->the_work));
      atomic_fetch_sub(&num_th_in_queue_lp[minor], 1);
      if (!ret_queue)
      {
        return 0;
      }
      return ret;

}


static ssize_t dev_write(struct file *filp, const char *buff, size_t len, loff_t *off)
{

  int minor = get_minor(filp);
  int ret;
  int ret_mutex;
  int prior;
  int ret_wq;
  packed_work *packed_work_sched;
  session_struct *session;
  char* temp_buffer;
  object_state *the_object;

  the_object = objects + minor;
  
  session = filp->private_data;
  prior = session->is_in_high_prior;

  if(prior){
    //copy to temp buffer
    ret = call_copy_from_user(&buff,&len, &temp_buffer);
    if(ret == (int) len) return 0;
    
    if (session->blocking){
      
      printk(KERN_INFO "Case Blocking with high priority\n");

      ret_wq = call_wait_queue(minor, &(the_object->hp_queue), &(the_object->hp_operation_synchronizer), session->timeout);
      if(ret_wq == -1){
        return 0;
      }
    }else{
      printk(KERN_INFO "Case Non Blocking with high priority\n");
      ret_mutex = mutex_trylock(&(the_object->hp_operation_synchronizer));
     
      if (!ret_mutex)
      {
        printk(KERN_INFO "Busy lock\n");
        return 0;
      }
    }
  }else{

    printk(KERN_INFO "Case low priority\n");
    packed_work_sched = kzalloc(sizeof(packed_work), GFP_ATOMIC);
    if (packed_work_sched == NULL) return -ENOSPC;
    ret = call_deferred_work(minor,&(the_object->lp_workqueue),&filp,&off,&len,&buff,&packed_work_sched);
    
    printk(KERN_INFO "%s: somebody called a write on dev with [major,minor] number [%d,%d]\n", MODNAME, get_major(filp), get_minor(filp));
    return len - ret;

  }

  
  *off = 0;
  *off += the_object->high_prior_valid_bytes;
  
  if ((*off > the_object->high_prior_valid_bytes))
  { // offset beyond the current stream size
    mutex_unlock(&(the_object->hp_operation_synchronizer));
    wake_up(&the_object->hp_queue);
    kfree(temp_buffer);
    return 0; 
  }


  write_op(&(the_object->high_prior_stream_content), the_object->high_prior_valid_bytes, len, &temp_buffer);

  *off += (len - ret);

  
  the_object->high_prior_valid_bytes = *off;
  num_byte_hp[minor] += (len - ret);
  printk(KERN_INFO "Stream high priority contains: %s with valid bytes %d\n",the_object->high_prior_stream_content, the_object->high_prior_valid_bytes);
  
  
  mutex_unlock(&(the_object->hp_operation_synchronizer));
  wake_up(&the_object->hp_queue);
  
  kfree(temp_buffer);

  return len - ret;
}

void read_op(int minor, long long int* off, int* valid_bytes, char* temp_buff, wait_queue_head_t *wait_queue, struct mutex* op_sync, size_t* len, char** stream){

  if (*off > *valid_bytes)
  {
      mutex_unlock(op_sync);
      wake_up(wait_queue);
      kfree(temp_buff);
  }

    
  if ((*valid_bytes - *off) < *len) *len = *valid_bytes - *off;

  if(*len > *valid_bytes) *len = *valid_bytes;

 
  strncpy(temp_buff,stream[*off],*len);
  memmove(*stream, (*stream) + (*len), (*valid_bytes) - (*len));
  memset(*stream + (*valid_bytes - *len), 0, *len);
    
  num_byte_hp[minor] -= (*len);

  *valid_bytes -= (*len);
  printk(KERN_INFO "Stream after read operation: %s, with number of bytes read: %d\n", *stream, *valid_bytes);
    
  mutex_unlock(op_sync);
  wake_up(wait_queue);
  
}

static ssize_t dev_read(struct file *filp, char *buff, size_t len, loff_t *off)
{

  int minor = get_minor(filp);
  int ret;
  int prior;
  int ret_mutex;
  int ret_wq;
  char* temp_buff;
  object_state *the_object;
  session_struct *session;

  the_object = objects + minor;
  session = filp->private_data;

  prior = session->is_in_high_prior;
  temp_buff = (char*)kzalloc(len, GFP_ATOMIC);

  if (session->blocking)
  {
    if (prior)
    {
      printk(KERN_INFO "Read Case Blocking with high priority\n");
      ret_wq = call_wait_queue(minor, &(the_object->hp_queue), &(the_object->hp_operation_synchronizer), session->timeout);
      
      if(!ret_wq){
        printk(KERN_INFO "Timeout expired\n");
        return 0;
      }
    }
    else
    {
      printk(KERN_INFO "Read case Blocking with low priority\n");
      ret_wq = call_wait_queue(minor, &(the_object->lp_queue), &(the_object->lp_operation_synchronizer), session->timeout);
      
      if(!ret_wq){
        printk(KERN_INFO "Timeout expired\n");
        return 0;
      }
    }
  }
  else
  {
    if (prior)
    {
      printk(KERN_INFO "Read case Non Blocking with high priority\n");
      ret_mutex = mutex_trylock(&(the_object->hp_operation_synchronizer));
      if (!ret_mutex)
      {
        return 0;
      }
    }
    else
    {
      printk(KERN_INFO "Read case Non Blocking with low priority\n");
      ret_mutex = mutex_trylock(&(the_object->lp_operation_synchronizer));

      if (!ret_mutex)
      {
        return 0;
      }
    }
  }
  *off = 0;

  if (prior)
  {
    read_op(minor, off, &(the_object->high_prior_valid_bytes), temp_buff, &the_object->hp_queue, &(the_object->hp_operation_synchronizer),&len, &the_object->high_prior_stream_content);
  }else{
    read_op(minor, off, &(the_object->low_prior_valid_bytes), temp_buff, &the_object->lp_queue, &(the_object->lp_operation_synchronizer),&len, &the_object->low_prior_stream_content);
  }

  ret = copy_to_user(buff, temp_buff, len);

  kfree(temp_buff);
  return len - ret;
}

static long dev_ioctl(struct file *filp, unsigned int command, unsigned long param)
{
  int minor = get_minor(filp);
  object_state *the_object;
  session_struct *session = filp->private_data;

  the_object = objects + minor;
  printk(KERN_INFO "%s: somebody called an ioctl on dev with [major,minor] number [%d,%d] and command %u \n", MODNAME, get_major(filp), get_minor(filp), command);

  switch (command)
  {
  case HP_B:
    session->is_in_high_prior = 1;
    session->blocking = 1;
    session->timeout = (int)param;
    printk(KERN_INFO "Added High Priority Blocking Case parameters\n");
    break;

  case HP_NB:
    session->is_in_high_prior = 1;
    session->blocking = 0;
    session->timeout = (int)param;
    printk(KERN_INFO "Added High Priority Non Blocking Case parameters\n");
    break;

  case LP_NB:
    session->is_in_high_prior = 0;
    session->blocking = 0;//need for read operations
    session->timeout = (int)param;
    printk(KERN_INFO "Added Low Priority Non Blocking Case parameters\n");
    break;

  case LP_B:
    session->is_in_high_prior = 0;
    session->blocking = 1;
    session->timeout = (int)param;
    printk(KERN_INFO "Added Low Priority Blocking Case parameters\n");
    break;

  case EN_DIS:
    if(param == 0){
      printk(KERN_INFO "Device with minor %d  is set to disable\n", minor);
      status[minor] = (int)param;
    }
    else if (param == 1)
    {
      printk(KERN_INFO "Device with minor %d  is set to enable\n", minor);
      status[minor] = (int)param;
    }else{
      printk(KERN_INFO "Parameter not valid\n");
    }
    break;
  }

  return 0;
}

static struct file_operations fops = {
    .owner = THIS_MODULE,
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
    if (!objects[i].lp_workqueue)
    {
      printk(KERN_INFO "Create workqueue failed\n");
    }
    objects[i].low_prior_valid_bytes = 0;
    objects[i].high_prior_valid_bytes = 0;
    objects[i].low_prior_stream_content = NULL;
    objects[i].low_prior_stream_content = (char *)kzalloc(0, GFP_KERNEL);
    objects[i].high_prior_stream_content = NULL;
    objects[i].high_prior_stream_content = (char *)kzalloc(0, GFP_KERNEL);
    if (objects[i].high_prior_stream_content == NULL || objects[i].low_prior_stream_content == NULL)
      goto revert_allocation;
  }

  Major = __register_chrdev(0, 0, 128, DEVICE_NAME, &fops);
  // actually allowed minors are directly controlled within this driver

  if (Major < 0)
  {
    printk(KERN_INFO "%s: registering device failed\n", MODNAME);
    return Major;
  }

  printk(KERN_INFO "%s: new device registered, it is assigned major number %d\n", MODNAME, Major);

  return 0;

revert_allocation:
  for (; i >= 0; i--)
  {
    destroy_workqueue(objects[i].lp_workqueue);
    kfree(objects[i].low_prior_stream_content);
    kfree(objects[i].high_prior_stream_content);
  }
  return -ENOMEM;
}

void cleanup_module(void)
{

  int i;
  for (i = 0; i < MINORS; i++)
  {
    destroy_workqueue(objects[i].lp_workqueue);
    kfree(objects[i].low_prior_stream_content);
    kfree(objects[i].high_prior_stream_content);
  }

  unregister_chrdev(Major, DEVICE_NAME);

  printk(KERN_INFO "%s: new device unregistered, it was assigned major number %d\n", MODNAME, Major);

  return;
}
