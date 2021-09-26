#include <ppipe.h>
#include <context.h>
#include <memory.h>
#include <lib.h>
#include <entry.h>
#include <file.h>

// Per process information for the ppipe.
struct ppipe_info_per_process
{

    // TODO:: Add members as per your need...
    int valid; // such a shame to not have a bool
    u32 pid;
    int front;
    int read_status;
    int write_status;
};

// Global information for the ppipe.
struct ppipe_info_global
{

    char *ppipe_buff; // Persistent pipe buffer: DO NOT MODIFY THIS.

    // TODO:: Add members as per your need...
    int front;
    int rear;
    int is_flushed;
};

// Persistent pipe structure.
// NOTE: DO NOT MODIFY THIS STRUCTURE.
struct ppipe_info
{

    struct ppipe_info_per_process ppipe_per_proc[MAX_PPIPE_PROC];
    struct ppipe_info_global ppipe_global;
};

// Function to allocate space for the ppipe and initialize its members.
struct ppipe_info *alloc_ppipe_info()
{

    // Allocate space for ppipe structure and ppipe buffer.
    struct ppipe_info *ppipe = (struct ppipe_info *)os_page_alloc(OS_DS_REG);
    char *buffer = (char *)os_page_alloc(OS_DS_REG);

    // Assign ppipe buffer.
    ppipe->ppipe_global.ppipe_buff = buffer;

    /**
     *  TODO:: Initializing pipe fields
     *
     *  Initialize per process fields for this ppipe.
     *  Initialize global fields for this ppipe.
     *
     */
    ppipe->ppipe_global.front = -1;
    ppipe->ppipe_global.rear = -1;

    ppipe->ppipe_per_proc[0].valid = 1;
    ppipe->ppipe_per_proc[0].read_status = 1;
    ppipe->ppipe_per_proc[0].write_status = 1;
    ppipe->ppipe_per_proc[0].front = -1;

    // Return the ppipe.
    return ppipe;
}

// Function to free ppipe buffer and ppipe info object.
// NOTE: DO NOT MODIFY THIS FUNCTION.
void free_ppipe(struct file *filep)
{

    os_page_free(OS_DS_REG, filep->ppipe->ppipe_global.ppipe_buff);
    os_page_free(OS_DS_REG, filep->ppipe);
}

// Fork handler for ppipe.
int do_ppipe_fork(struct exec_context *child, struct file *filep)
{

    /**
     *  TODO:: Implementation for fork handler
     *
     *  You may need to update some per process or global info for the ppipe.
     *  This handler will be called twice since ppipe has 2 file objects.
     *  Also consider the limit on no of processes a ppipe can have.
     *  Return 0 on success.
     *  Incase of any error return -EOTHERS.
     *
     */

    int child_index = -1, parent_index = -1, i;
    int prev_read_status, prev_write_status;
    u32 cpid = child->pid;
    u32 ppid = child->ppid;
    for (i = 0; i < MAX_PPIPE_PROC; i++)
    {
        if ((filep->ppipe->ppipe_per_proc[i].valid == 0) && (child_index == -1))
        {
            child_index = i;
        }
        if ((filep->ppipe->ppipe_per_proc[i].valid) && (filep->ppipe->ppipe_per_proc[i].pid == ppid))
        {
            parent_index = i;
        }
    }

    // limit on no of processes reached
    if (child_index == -1 || parent_index == -1)
        return -EOTHERS;

    if (filep->mode & O_READ)
    {
        prev_write_status = filep->ppipe->ppipe_per_proc[child_index].write_status;

        filep->ppipe->ppipe_per_proc[child_index] = filep->ppipe->ppipe_per_proc[parent_index];
        filep->ppipe->ppipe_per_proc[child_index].write_status = prev_write_status;
        filep->ppipe->ppipe_per_proc[child_index].pid = child->pid;
    }
    else if (filep->mode & O_WRITE)
    {
        prev_read_status = filep->ppipe->ppipe_per_proc[child_index].read_status;

        filep->ppipe->ppipe_per_proc[child_index] = filep->ppipe->ppipe_per_proc[parent_index];
        filep->ppipe->ppipe_per_proc[child_index].read_status = prev_read_status;
        filep->ppipe->ppipe_per_proc[child_index].pid = child->pid;
    }
    else
    {
        return -EOTHERS;
    }

    // Return successfully.
    return 0;
}

// Function to close the ppipe ends and free the ppipe when necessary.
long ppipe_close(struct file *filep)
{

    /**
     *  TODO:: Implementation of Pipe Close
     *
     *  Close the read or write end of the ppipe depending upon the file
     *      object's mode.
     *  You may need to update some per process or global info for the ppipe.
     *  Use free_pipe() function to free ppipe buffer and ppipe object,
     *      whenever applicable.
     *  After successful close, it return 0.
     *  Incase of any error return -EOTHERS.
     *
     */

    int process_index, num_process = 0, i;
    u32 cpid = get_current_ctx()->pid;
    for (i = 0; i < MAX_PPIPE_PROC; i++)
    {
        if (!(filep->ppipe->ppipe_per_proc[i].valid))
            continue;

        if (filep->ppipe->ppipe_per_proc[i].pid == cpid)
            process_index = i;

        num_process++;
    }

    // ? Should we check for process index

    // update variable according to the end being closed
    if (filep->mode & O_READ)
    {
        filep->ppipe->ppipe_per_proc[process_index].read_status = 0;
    }
    else if (filep->mode & O_WRITE)
    {
        filep->ppipe->ppipe_per_proc[process_index].write_status = 0;
    }
    else
    {
        return -EOTHERS;
    }

    if ((filep->ppipe->ppipe_per_proc[process_index].read_status == 0) && (filep->ppipe->ppipe_per_proc[process_index].write_status == 0))
    {
        // free for other processes
        num_process--;
        filep->ppipe->ppipe_per_proc[process_index].valid = 0;
    }

    // ppipe is useless
    if (num_process == 0)
        free_ppipe(filep);

    int ret_value;

    // Close the file.
    ret_value = file_close(filep); // DO NOT MODIFY THIS LINE.

    // And return.
    return ret_value;
}

// Function to perform flush operation on ppipe.
int do_flush_ppipe(struct file *filep)
{

    /**
     *  TODO:: Implementation of Flush system call
     *
     *  Reclaim the region of the persistent pipe which has been read by
     *      all the processes.
     *  Return no of reclaimed bytes.
     *  In case of any error return -EOTHERS.
     *
     */

    //
    int reclaimed_bytes = 0, avail_bytes, i;
    int process_front;
    int global_front = filep->ppipe->ppipe_global.front;
    int global_rear = filep->ppipe->ppipe_global.rear;
    int curr_ppipe_size = (global_rear - global_front + 1 + MAX_PPIPE_SIZE) % MAX_PPIPE_SIZE;

    // empty ppipe: nothing to flush
    if ((global_front == -1) && (global_rear == -1))
        return reclaimed_bytes;
    else
    {
        // will try to minimize this
        reclaimed_bytes = curr_ppipe_size;
    }

    for (i = 0; i < MAX_PPIPE_PROC; i++)
    {
        if (!(filep->ppipe->ppipe_per_proc[i].valid))
            continue;

        process_front = filep->ppipe->ppipe_per_proc[i].front;
        if (process_front == -1)
        {
            // essentially we can free whole pipe
            continue;
        }

        avail_bytes = (process_front - global_front + MAX_PPIPE_SIZE) % MAX_PPIPE_SIZE;

        // minimum bytes we can free safely
        if (avail_bytes < reclaimed_bytes)
        {
            reclaimed_bytes = avail_bytes;
        }
    }

    if (reclaimed_bytes == curr_ppipe_size)
    {
        // empty ppipe
        global_front = -1;
        global_rear = filep->ppipe->ppipe_global.rear = -1;
    }
    else
    {
        global_front = (global_front + reclaimed_bytes) % MAX_PPIPE_SIZE;
    }

    // update information
    filep->ppipe->ppipe_global.front = global_front;

    // Return reclaimed bytes.
    return reclaimed_bytes;
}

// Read handler for the ppipe.
int ppipe_read(struct file *filep, char *buff, u32 count)
{

    /**
     *  TODO:: Implementation of PPipe Read
     *
     *  Read the data from ppipe buffer and write to the provided buffer.
     *  If count is greater than the present data size in the ppipe then just read
     *      that much data.
     *  Validate file object's access right.
     *  On successful read, return no of bytes read.
     *  Incase of Error return valid error code.
     *      -EACCES: In case access is not valid.
     *      -EINVAL: If read end is already closed.
     *      -EOTHERS: For any other errors.
     *
     */

    if (filep == NULL)
    {
        printk("filep is null\n");
        return -EINVAL;
    }

    int process_index, i;
    u32 cpid = get_current_ctx()->pid;
    for (i = 0; i < MAX_PPIPE_SIZE; i++)
    {
        if (!(filep->ppipe->ppipe_per_proc[i].valid))
            continue;

        if (filep->ppipe->ppipe_per_proc[i].pid != cpid)
            continue;

        process_index = i;
        break;
    }

    if (process_index == -1)
    {
        printk("somehow calling process not registered\n");
        return -EOTHERS;
    }

    if (filep->ppipe->ppipe_per_proc[process_index].read_status == 0)
    {
        printk("read end already closed \n");
        return -EINVAL;
    }

    // write to buffer
    // checking validity of buffer is not required

    if (!(filep->mode & O_READ))
    {
        printk("no read permission for pipe file object \n");
        return -EACCES;
    }

    int bytes_read = 0;

    // reading is local to process
    int process_front = filep->ppipe->ppipe_per_proc[process_index].front;
    int global_rear = filep->ppipe->ppipe_global.rear;

    // empty queue
    if (process_front == -1)
    {
        return 0;
    }

    while (bytes_read < count)
    {
        buff[bytes_read] = filep->ppipe->ppipe_global.ppipe_buff[process_front];
        bytes_read += 1;

        // nothing to read
        if (process_front == global_rear)
        {
            process_front = -1;
            break;
        }

        process_front = (process_front + 1) % MAX_PPIPE_SIZE;
    }

    // update information
    filep->ppipe->ppipe_per_proc[process_index].front = process_front;

    // Return no of bytes read.
    return bytes_read;
}

// Write handler for ppipe.
int ppipe_write(struct file *filep, char *buff, u32 count)
{

    /**
     *  TODO:: Implementation of PPipe Write
     *
     *  Write the data from the provided buffer to the ppipe buffer.
     *  If count is greater than available space in the ppipe then just write
     *      data that fits in that space.
     *  Validate file object's access right.
     *  On successful write, return no of written bytes.
     *  Incase of Error return valid error code.
     *      -EACCES: In case access is not valid.
     *      -EINVAL: If write end is already closed.
     *      -EOTHERS: For any other errors.
     *
     */
    if (filep == NULL)
    {
        printk("filep is null\n");
        return -EINVAL;
    }

    int process_index, i;
    u32 cpid = get_current_ctx()->pid;
    for (i = 0; i < MAX_PPIPE_SIZE; i++)
    {
        if (!(filep->ppipe->ppipe_per_proc[i].valid))
            continue;

        if (filep->ppipe->ppipe_per_proc[i].pid != cpid)
            continue;

        process_index = i;
        break;
    }

    if (process_index == -1)
    {
        printk("somehow calling process not registered\n");
        return -EOTHERS;
    }

    if (filep->ppipe->ppipe_per_proc[process_index].write_status == 0)
    {
        printk("write end already closed \n");
        return -EINVAL;
    }

    // write to buffer
    // checking validity of buffer is not required

    if (!(filep->mode & O_WRITE))
    {
        printk("no write permission for pipe file object \n");
        return -EACCES;
    }
    int bytes_written = 0;

    // writing is a global process
    int global_front = filep->ppipe->ppipe_global.front;
    int global_rear = filep->ppipe->ppipe_global.rear;

    // queue full
    if (global_front == (global_rear + 1) % MAX_PPIPE_SIZE)
    {
        return 0;
    }

    // never overwrite
    while (bytes_written < count)
    {

        // empty queue needs initialization
        if ((global_front == -1) && (global_rear == -1))
        {
            global_front = filep->ppipe->ppipe_global.front = 0;

            // process front initialization
            for (int i = 0; i < MAX_PPIPE_SIZE; i++)
            {
                filep->ppipe->ppipe_per_proc[i].front = 0;
            }
            global_rear = 0;
        }
        else
        {
            // go in circle
            global_rear = (global_rear + 1) % MAX_PPIPE_SIZE;
        }

        filep->ppipe->ppipe_global.ppipe_buff[global_rear] = buff[bytes_written];
        bytes_written += 1;

        // queue full
        if (global_front == (global_rear + 1) % MAX_PPIPE_SIZE)
        {
            break;
        }
    }

    // update information
    filep->ppipe->ppipe_global.rear = global_rear;

    // Return no of bytes written.
    return bytes_written;
}

// Function to create persistent pipe.
int create_persistent_pipe(struct exec_context *current, int *fd)
{

    /**
     *  TODO:: Implementation of PPipe Create
     *
     *  Find two free file descriptors.
     *  Create two file objects for both ends by invoking the alloc_file() function.
     *  Create ppipe_info object by invoking the alloc_ppipe_info() function and
     *      fill per process and global info fields.
     *  Fill the fields for those file objects like type, fops, etc.
     *  Fill the valid file descriptor in *fd param.
     *  On success, return 0.
     *  Incase of Error return valid Error code.
     *      -ENOMEM: If memory is not enough.
     *      -EOTHERS: Some other errors.
     *
     */

    int i, read_fd = -1, write_fd = -1;
    struct file *read_file, *write_file;
    struct ppipe_info *ppi_object;

    // Two free file descriptors
    for (i = 0; i < MAX_OPEN_FILES; i++)
    {
        if (current->files[i] == NULL)
        {
            if (read_fd == -1)
            {
                read_fd = i;
            }
            else
            {
                write_fd = i;
                break;
            }
        }
    }

    if (read_fd == -1 || write_fd == -1)
    {
        return -ENOMEM;
    }

    //  Create two file objects for both ends by invoking the alloc_file() function.
    read_file = alloc_file();
    write_file = alloc_file();

    // Create ppipe_info object by invoking the alloc_ppipe_info() function and
    // fill per process and global info fields.
    ppi_object = alloc_ppipe_info();
    ppi_object->ppipe_per_proc[0].pid = current->pid;

    // filling the fields of file object
    read_file->type = PPIPE;
    read_file->mode = O_READ;
    read_file->fops->close = ppipe_close;
    read_file->fops->read = ppipe_read;
    read_file->ppipe = ppi_object;

    write_file->type = PPIPE;
    write_file->mode = O_WRITE;
    write_file->fops->close = ppipe_close;
    write_file->fops->write = ppipe_write;
    write_file->ppipe = ppi_object;

    // Fill the valid file descriptor in *fd param
    current->files[read_fd] = read_file;
    current->files[write_fd] = write_file;
    fd[0] = read_fd;
    fd[1] = write_fd;
    // Simple return.
    return 0;
}
