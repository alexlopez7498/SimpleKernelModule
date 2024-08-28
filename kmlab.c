#define LINUX
#define PROC_NAME "linked_list"
#define PROCFS_DIR "kmlab"
#define PROCFS_FILE "status"
#include <linux/proc_fs.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/workqueue.h>
#include <asm/uaccess.h>
#include <asm/types.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/spinlock.h>
#include "kmlab_given.h"
// Include headers as needed ...
#include "ll.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Lopez-Garcia"); // Change with your lastname
MODULE_DESCRIPTION("CPTS360 Lab 4");

#define DEBUG 1
static DEFINE_SPINLOCK(spn_lock);
// Global variables as needed ...
struct processes { // make a struct so that we can insert it into the list with the pid and cputime 
    pid_t pid;
    long unsigned cputime;
    struct list_head list;
};
LIST_HEAD(my_list);
struct list_head my_list; // GLOBAL VARIABLES TO USE IN MY FUNCTION
struct proc_dir_entry *proc;
struct proc_dir_entry *proc_file;
static struct timer_list proc_timer;
int len,temp;
char *msg;
struct proc_ops proc_file_fops = { // STRUCT FOR READ AND WRIRTE FUNCTION FOR EASY ACCESS
    .proc_read = my_proc_read,
    .proc_write = my_proc_write,
};

static void addPID(int pid) // FUNCTION SIMILAR TO ADD NODE BUT ADDS A NODE WITH THE PID NUMBER
{
    struct processes *newEntry;
    unsigned long flags;

    // create new entry and add to end of list
    newEntry = (struct processes *)kzalloc(sizeof(struct processes), GFP_KERNEL);
    newEntry->pid = pid;
    spin_lock_irqsave(&spn_lock, flags); // SPINLOCK IT SO THAT IT DOESN'T GET MESSED
    list_add_tail(&newEntry->list, &my_list); // Add to end of the list
    spin_unlock_irqrestore(&spn_lock, flags); // SPINLOCK IT SO THAT IT DOESN'T GET MESSED

}

ssize_t my_proc_write(struct file *file, const char __user *buffer,size_t len, loff_t *offset)
{
    int pid;
    char kmBUFFER[8] = "";

    if (copy_from_user(kmBUFFER, buffer, len))// COPY THE PID FROM THE USER TO THE kernal BUFFER
        return -EFAULT;

    if ((kstrtoint(kmBUFFER, 10, &pid))) // THEN WE CHANGE THE PID STRING TO AN INT 
        return -ESRCH;
        
    addPID(pid); // THEN WE ADD THE NODE INTO THE LIST WITH THE PID WE JUST GOT

    return len;
}

static size_t getListLength(void) // FUNCTION TO GET THE LENGTH OF THE LIST
{
    struct processes *process;
    size_t len;
    unsigned long flags; // HAVE THIS VARIABLE FOR WHEN I NEED TO SPINLOCK

    spin_lock_irqsave(&spn_lock, flags); //SPINLOCK SINCE WE ARE DEALING WITH LIST
    list_for_each_entry(process, &my_list, list) 
    {
        len++; //GO THROUGH WHOLE LIST AND ADD TO LEN
    }
    spin_unlock_irqrestore(&spn_lock, flags);//SPINLOCK SINCE WE ARE DEALING WITH LIST

    return len; // THEN WE RETURN LEN
}

ssize_t my_proc_read(struct file *file, char __user *buffer, size_t len, loff_t *offset)
{
    struct processes *process;
    char *kmBuffer;
    unsigned long flags;
    size_t size = 0;

    if (*offset) // base case
    {
        return 0;
    }

    kmBuffer = kmalloc(len, GFP_KERNEL);
    if (!kmBuffer) // other base case
    {
        return -ENOMEM;
    }

    spin_lock_irqsave(&spn_lock, flags); // SPINLOCK IT SO THAT IT DOESN'T GET MESSED

 
    list_for_each_entry(process, &my_list, list) // GO THROUGH THE WHOLE LIST AND PRINT THE PID AND CPUTIME
    {
        size += snprintf(kmBuffer + size, len - size, "Pid %d: %lu\n", process->pid, process->cputime);
    }

    spin_unlock_irqrestore(&spn_lock, flags); // SPINLOCK IT SO THAT IT DOESN'T GET MESSED

    if (copy_to_user(buffer, kmBuffer, size)) // COPY OVER FROM kmBUFFER TO BUFFER
    {
        kfree(kmBuffer); // THEN FREE THE KMBUFFER
        return -EFAULT;
    }
    
    return size;
}



static void work_function(struct work_struct *work) // WORK FUNCTION ALLOWS TO GET THE NEW CPU TIME FOR THE PID
{
    unsigned long flags;
    struct processes *process, *tempPROC;
    int check = 0;
    spin_lock_irqsave(&spn_lock, flags); // SPINLOCK IT SO THAT IT DOESN'T GET MESSED
    list_for_each_entry_safe(process, tempPROC, &my_list, list) 
    {
        check = get_cpu_use(process->pid, &process->cputime); // check is equal to the function 
        if(check == -1) // if check is equal to -1 then we know that it is done and we can delete it 
        {
            list_del(&process->list);
            kfree(process);
        }
    }
    spin_unlock_irqrestore(&spn_lock, flags); // SPINLOCK IT SO THAT IT DOESN'T GET MESSED
}

DECLARE_WORK(workqueue, work_function); // have the workqueu go into the work function

void timer_callback(struct timer_list *t) // call back timer to schedule work when the list is not empty
{

    if (getListLength() > 0) 
    {
        schedule_work(&workqueue);
    }

    mod_timer(&proc_timer, jiffies + msecs_to_jiffies(5000)); // mod the timer for 5 seconds
}

int __init kmlab_init(void)
{
   #ifdef DEBUG
   pr_info("KMLAB MODULE LOADING\n");
   #endif
   // Insert your code here ...
   proc = proc_mkdir(PROCFS_DIR,NULL); // we make the directory for the proc
   if(proc == NULL) // base case for the proc directory
   {
      pr_alert("Error: unable to initialize /proc/%s\n", PROCFS_DIR);
        return -ENOMEM;
   }
   proc_file = proc_create(PROCFS_FILE, 0666, proc, &proc_file_fops); // then we want to create the file 
    if (proc_file == NULL) { // base case
        pr_alert("Error: unable to create /proc/%s/%s\n", PROCFS_DIR, PROCFS_FILE);
        proc_remove(proc);
        return -ENOMEM;
    }

   INIT_LIST_HEAD(&my_list); // initialize the list
   
   timer_setup(&proc_timer,timer_callback,0); // we set up the timer for with the call back
   mod_timer(&proc_timer, jiffies + msecs_to_jiffies(5000));
   pr_info("KMLAB MODULE LOADED\n");
   return 0;   
}

// kmlab_exit - Called when module is unloaded
void __exit kmlab_exit(void)
{
    struct processes *process, *tempProc;

    #ifdef DEBUG
    pr_info("KMLAB MODULE UNLOADING\n");
    #endif

    list_for_each_entry_safe(process, tempProc, &my_list, list) // go through the whole list and delete the whole list
    {
        list_del(&process->list);
        kfree(process);
    }

    del_timer(&proc_timer); // we delete the timer when we are done

    proc_remove(proc); // we delete/remove the proc directory and the file
    proc_remove(proc_file);

    flush_scheduled_work(); // this library function deletes the work queue

    pr_info("KMLAB MODULE UNLOADED\n");
}

module_init(kmlab_init);
module_exit(kmlab_exit);
