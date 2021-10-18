#include <pipe.h>
#include <context.h>
#include <memory.h>
#include <lib.h>
#include <entry.h>
#include <file.h>

// Per process info for the pipe.
struct pipe_info_per_process
{

	// TODO:: Add members as per your need...
	int valid; // such a shame to not have a bool
	u32 pid;
	int read_status;
	int write_status;
};

// Global information for the pipe.
struct pipe_info_global
{

	char *pipe_buff; // Pipe buffer: DO NOT MODIFY THIS.

	// TODO:: Add members as per your need...
	int front;
	int rear;
};

// Pipe information structure.
// NOTE: DO NOT MODIFY THIS STRUCTURE.
struct pipe_info
{
	struct pipe_info_per_process pipe_per_proc[MAX_PIPE_PROC];
	struct pipe_info_global pipe_global;
};

// Function to allocate space for the pipe and initialize its members.
struct pipe_info *alloc_pipe_info()
{

	// Allocate space for pipe structure and pipe buffer.
	struct pipe_info *pipe = (struct pipe_info *)os_page_alloc(OS_DS_REG);
	char *buffer = (char *)os_page_alloc(OS_DS_REG);

	// Assign pipe buffer.
	pipe->pipe_global.pipe_buff = buffer;

	/**
	 *  TODO:: Initializing pipe fields
	 *
	 *  Initialize per process fields for this pipe.
	 *  Initialize global fields for this pipe.
	 *
	 */
	pipe->pipe_global.front = -1;
	pipe->pipe_global.rear = -1;

	pipe->pipe_per_proc[0].valid = 1;
	pipe->pipe_per_proc[0].read_status = 1;
	pipe->pipe_per_proc[0].write_status = 1;

	// Return the pipe.
	return pipe;
}

// Function to free pipe buffer and pipe info object.
// NOTE: DO NOT MODIFY THIS FUNCTION.
void free_pipe(struct file *filep)
{

	os_page_free(OS_DS_REG, filep->pipe->pipe_global.pipe_buff);
	os_page_free(OS_DS_REG, filep->pipe);
}

// Fork handler for the pipe.
int do_pipe_fork(struct exec_context *child, struct file *filep)
{

	/**
	 *  TODO:: Implementation for fork handler
	 *
	 *  You may need to update some per process or global info for the pipe.
	 *  This handler will be called twice since pipe has 2 file objects.
	 *  Also consider the limit on no of processes a pipe can have.
	 *  Return 0 on success.
	 *  Incase of any error return -EOTHERS.
	 *
	 */

	// find a free index
	int child_index = -1, parent_index = -1, first_free_index = -1, i;
	u32 cpid = child->pid;
	u32 ppid = child->ppid;
	for (i = 0; i < MAX_PIPE_PROC; i++)
	{
		if ((filep->pipe->pipe_per_proc[i].valid == 0) && (child_index == -1))
		{
			first_free_index = i;
		}
		if ((filep->pipe->pipe_per_proc[i].valid) && (filep->pipe->pipe_per_proc[i].pid == ppid))
		{
			parent_index = i;
		}
		// already initialized
		if ((filep->pipe->pipe_per_proc[i].valid) && (filep->pipe->pipe_per_proc[i].pid == cpid))
		{
			child_index = i;
		}
	}

	// limit on no of processes reached
	if (((first_free_index == -1) && (child_index == -1)) || (parent_index == -1))
		return -EOTHERS;

	// Initialization needed
	if (child_index == -1)
	{
		child_index = first_free_index;
	}

	if (filep->mode & O_READ)
	{
		filep->pipe->pipe_per_proc[child_index].pid = child->pid;
		filep->pipe->pipe_per_proc[child_index].read_status = filep->pipe->pipe_per_proc[parent_index].read_status;
		filep->pipe->pipe_per_proc[child_index].valid = 1;

		// printk("child pid: %d\n", child->pid);
		// printk("parent pid:%d\n", child->ppid);
		// printk("child index:%d\n", child_index);
		// printk("parent index:%d\n", parent_index);
		// printk("parent read status: %d\n", filep->pipe->pipe_per_proc[parent_index].read_status);
	}
	else if (filep->mode & O_WRITE)
	{
		filep->pipe->pipe_per_proc[child_index].pid = child->pid;
		filep->pipe->pipe_per_proc[child_index].write_status = filep->pipe->pipe_per_proc[parent_index].write_status;
		filep->pipe->pipe_per_proc[child_index].valid = 1;
		// printk("child pid: %d\n", child->pid);
		// printk("parent pid:%d\n", child->ppid);
		// printk("child index:%d\n", child_index);
		// printk("parent index:%d\n", parent_index);
		// printk("parent write status: %d\n", filep->pipe->pipe_per_proc[parent_index].write_status);
	}
	else
	{
		return -EOTHERS;
	}

	// Return successfully.
	return 0;
}

// Function to close the pipe ends and free the pipe when necessary.
long pipe_close(struct file *filep)
{

	/**
	 *  TODO:: Implementation of Pipe Close
	 *
	 *  Close the read or write end of the pipe depending upon the file
	 *      object's mode.
	 *  You may need to update some per process or global info for the pipe.
	 *  Use free_pipe() function to free pipe buffer and pipe object,
	 *      whenever applicable.
	 *  After successful close, it return 0.
	 *  Incase of any error return -EOTHERS.
	 *
	 */

	// ! Still can't figure out per process variables
	int process_index, num_process = 0, i;
	u32 cpid = get_current_ctx()->pid;

	for (i = 0; i < MAX_PIPE_PROC; i++)
	{
		if (!(filep->pipe->pipe_per_proc[i].valid))
			continue;

		if (filep->pipe->pipe_per_proc[i].pid == cpid)
			process_index = i;

		num_process++;
	}

	// Update variables according to the end being closed
	if (filep->mode & O_READ)
	{
		filep->pipe->pipe_per_proc[process_index].read_status = 0;
	}
	else if (filep->mode & O_WRITE)
	{
		filep->pipe->pipe_per_proc[process_index].write_status = 0;
	}
	else
	{
		return -EOTHERS;
	}

	if ((filep->pipe->pipe_per_proc[process_index].read_status == 0) && (filep->pipe->pipe_per_proc[process_index].write_status == 0))
	{
		// free for other processes
		num_process--;
		filep->pipe->pipe_per_proc[process_index].valid = 0;
	}
	// Pipe is useless
	if (num_process == 0)
		free_pipe(filep);

	int ret_value;
	// Close the file and return.
	ret_value = file_close(filep); // DO NOT MODIFY THIS LINE.

	// And return.
	return ret_value;
}

// Check whether passed buffer is valid memory location for read or write.
int is_valid_mem_range(unsigned long buff, u32 count, int access_bit)
{

	/**
	 *  TODO:: Implementation for buffer memory range checking
	 *
	 *  Check whether passed memory range is suitable for read or write.
	 *  If access_bit == 1, then it is asking to check read permission.
	 *  If access_bit == 2, then it is asking to check write permission.
	 *  If range is valid then return 1.
	 *  Incase range is not valid or have some permission issue return -EBADMEM.
	 *
	 */

	int ret_value = -EBADMEM;
	int flag = 0, i;
	struct mm_segment mm_segment;
	struct vm_area *vm_area;
	struct exec_context *exec_context = get_current_ctx(); // Should be non-null

	// mm_segment
	for (i = 0; i < MAX_MM_SEGS; i++)
	{
		mm_segment = exec_context->mms[i];
		if ((mm_segment.start <= buff) && (mm_segment.end >= buff + count - 1))
		{
			flag = mm_segment.access_flags;
			break;
		}
	}

	// VM Area
	if (flag == 0)
	{
		vm_area = exec_context->vm_area;
		while (vm_area != NULL)
		{
			if ((vm_area->vm_start <= buff) && (vm_area->vm_end >= buff + count - 1))
			{
				flag = vm_area->access_flags;
				break;
			}
			vm_area = vm_area->vm_next;
		}
	}

	// Opened in proper mode
	if (flag & access_bit)
		ret_value = 1;

	// Return the finding.
	return ret_value;
}

// Function to read given no of bytes from the pipe.
int pipe_read(struct file *filep, char *buff, u32 count)
{

	/**
	 *  TODO:: Implementation of Pipe Read
	 *
	 *  Read the data from pipe buffer and write to the provided buffer.
	 *  If count is greater than the present data size in the pipe then just read
	 *       that much data.
	 *  Validate file object's access right.
	 *  On successful read, return no of bytes read.
	 *  In case of Error return valid error code.
	 *       -EACCES: In case access is not valid.
	 *       -EINVAL: If read end is already closed.
	 *       -EOTHERS: For any other errors.
	 *
	 */
	// ! Non-Blocking Function

	if (filep == NULL)
	{
		printk("filep is null\n");
		return -EINVAL;
	}

	int process_index, i;
	u32 cpid = get_current_ctx()->pid;
	for (i = 0; i < MAX_PIPE_PROC; i++)
	{
		if (!(filep->pipe->pipe_per_proc[i].valid))
			continue;

		if (filep->pipe->pipe_per_proc[i].pid != cpid)
			continue;

		process_index = i;
		break;
	}

	if (process_index == -1)
	{
		printk("somehow process not registered!!\n");
		return -EOTHERS;
	}

	// read end is already closed
	if (filep->pipe->pipe_per_proc[process_index].read_status == 0)
	{
		printk("read end already closed\n");
		return -EINVAL;
	}

	// write to buffer
	int check_buffer = is_valid_mem_range((unsigned long)buff, count, 2);
	if (check_buffer != 1)
	{
		printk("Bad memory location of read buffer\n");
		return check_buffer;
	}

	// Validate file object's access right
	if (!(filep->mode & O_READ))
	{
		printk("no read permission for pipe file object\n");
		return -EACCES;
	}

	int bytes_read = 0;

	int front = filep->pipe->pipe_global.front;
	int rear = filep->pipe->pipe_global.rear;
	// empty queue
	if ((front == -1) && (rear == -1))
	{
		return 0;
	}

	// printk("=======read starts=======\n");
	// printk("count: %d\n",count);
	// printk("front: %d\n",front);
	// printk("rear: %d\n",rear);
	while (bytes_read < count)
	{
		buff[bytes_read] = filep->pipe->pipe_global.pipe_buff[front];
		bytes_read += 1;
		// nothing to read
		if (front == rear)
		{
			front = -1;
			rear = filep->pipe->pipe_global.rear = -1;
			break;
		}

		front = (front + 1) % MAX_PIPE_SIZE;
	}

	// update information
	filep->pipe->pipe_global.front = front;

	// Return no of bytes read.
	return bytes_read;
}

// Function to write given no of bytes to the pipe.
int pipe_write(struct file *filep, char *buff, u32 count)
{

	/**
	 *  TODO:: Implementation of Pipe Write
	 *
	 *  Write the data from the provided buffer to the pipe buffer.
	 *  If count is greater than available space in the pipe then just write data
	 *       that fits in that space.
	 *  Validate file object's access right.
	 *  On successful write, return no of written bytes.
	 *  Incase of Error return valid error code.
	 *       -EACCES: In case access is not valid.
	 *       -EINVAL: If write end is already closed.
	 *       -EOTHERS: For any other errors.
	 *
	 */

	// should not happen ideally
	if (filep == NULL)
	{
		printk("filep is null\n");
		return -EINVAL;
	}

	int process_index = -1, i;
	u32 cpid = get_current_ctx()->pid;
	for (i = 0; i < MAX_PIPE_PROC; i++)
	{
		if (!(filep->pipe->pipe_per_proc[i].valid))
			continue;

		if (filep->pipe->pipe_per_proc[i].pid != cpid)
			continue;

		process_index = i;
		break;
	}

	if (process_index == -1)
	{
		printk("somehow process not registered!!\n");
		return -EOTHERS;
	}

	// write end is already closed
	if (filep->pipe->pipe_per_proc[process_index].write_status == 0)
	{
		printk("write end already closed\n");
		// printk("current pid: %d\n", get_current_ctx()->pid);
		// printk("parent pid: %d\n", get_current_ctx()->ppid);
		// printk("write status : %d\n", filep->pipe->pipe_per_proc[process_index].write_status);
		return -EINVAL;
	}

	// read from buffer
	int check_buffer = is_valid_mem_range((unsigned long)buff, count, 1);
	if (check_buffer != 1)
	{
		printk("Bad memory location of write buffer \n");
		return check_buffer;
	}

	if (!(filep->mode & O_WRITE))
	{
		printk("no write access for pipe file object\n");
		return -EACCES;
	}

	int bytes_written = 0;

	int front = filep->pipe->pipe_global.front;
	int rear = filep->pipe->pipe_global.rear;

	// printk("=======write starts=======\n");
	// printk("count: %d\n",count);
	// printk("front: %d\n",front);
	// printk("rear: %d\n",rear);

	// queue full
	if (front == (rear + 1) % MAX_PIPE_SIZE)
	{
		return 0;
	}

	// never overwrite
	while (bytes_written < count)
	{
		if ((front == -1) && (rear == -1))
		{
			// empty queue initialize
			front = filep->pipe->pipe_global.front = 0;
			rear = 0;
		}
		else
		{
			// go in circle
			rear = (rear + 1) % MAX_PIPE_SIZE;
		}

		filep->pipe->pipe_global.pipe_buff[rear] = buff[bytes_written];
		bytes_written += 1;

		// queue full
		if (front == (rear + 1) % MAX_PIPE_SIZE)
			break;
	}

	// update information
	filep->pipe->pipe_global.rear = rear;

	// Return no of bytes written.
	return bytes_written;
}

// Function to create pipe.
int create_pipe(struct exec_context *current, int *fd)
{

	/**
	 *  TODO:: Implementation of Pipe Create
	 *
	 *  Find two free file descriptors.
	 *  Create two file objects for both ends by invoking the alloc_file() function.
	 *  Create pipe_info object by invoking the alloc_pipe_info() function and
	 *       fill per process and global info fields.
	 *  Fill the fields for those file objects like type, fops, etc.
	 *  Fill the valid file descriptor in *fd param.
	 *  On success, return 0.
	 *  Incase of Error return valid Error code.
	 *       -ENOMEM: If memory is not enough.
	 *       -EOTHERS: Some other errors.
	 *
	 */

	int i, read_fd = -1, write_fd = -1;
	struct file *read_file, *write_file;
	struct pipe_info *pi_object;

	// Find two free file descriptors
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

	// Create two file objects for both ends by invoking the alloc_file() function.
	read_file = alloc_file();
	write_file = alloc_file();

	// allocate and initialize pipe_info object
	pi_object = alloc_pipe_info();
	pi_object->pipe_per_proc[0].pid = current->pid; // to uniquely identify a process

	// Fill the fields for those file objects like type, fops, etc.
	read_file->type = PIPE;
	read_file->mode = O_READ;
	read_file->fops->close = pipe_close; // & is not required for function pointers
	read_file->fops->read = pipe_read;   // no write required
	read_file->pipe = pi_object;

	write_file->type = PIPE;
	write_file->mode = O_WRITE;
	write_file->fops->close = pipe_close;
	write_file->fops->write = pipe_write; // no read required
	write_file->pipe = pi_object;

	// Fill the valid file descriptor in *fd param.
	current->files[read_fd] = read_file;
	current->files[write_fd] = write_file;
	fd[0] = read_fd;
	fd[1] = write_fd;
	// Simple return.
	return 0;
}
