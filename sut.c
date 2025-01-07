//Issues: Early termination on some runs
//Would love some feedback on what lines of the code might be wrong!
//Thank you!!

#include <ucontext.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include "queue.h"
#include <unistd.h>
#include <stdbool.h>
#include <time.h>
#include <fcntl.h>
#include <string.h>
#define SPLIT_TOKEN "\1"
#define STACK_SIZE 1024*1024


// Global variables for executors and queues
pthread_t C_EXEC, I_EXEC;
struct queue ready_queue, wait_queue, to_io, from_io;
// Mutexes
pthread_mutex_t cexec_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t io_mutex = PTHREAD_MUTEX_INITIALIZER;
// Flags
bool ready_queue_empty = false; 
bool wait_queue_empty = false;  
bool first_done = false; //TODO: review 
bool running = true;

// C-EXEC main user thread context
ucontext_t main_cexec;

typedef void (*sut_task_f)();

void *C_EXEC_SCHEDULER(){
    struct queue_entry *next;

    while(!ready_queue_empty || !wait_queue_empty){
        //Peek at next task
        pthread_mutex_lock(&cexec_mutex);
        next = queue_peek_front(&ready_queue);
        pthread_mutex_unlock(&cexec_mutex);

        //If next !NUll then
        if(next){
            // Pop and execute
            pthread_mutex_lock(&cexec_mutex);
            queue_pop_head(&ready_queue);
            pthread_mutex_unlock(&cexec_mutex);

            //Swap from main to task
            ucontext_t task = *(ucontext_t *) next->data;
            swapcontext(&main_cexec, &task);

            first_done = true;

        } else if (first_done){
            ready_queue_empty = true;
        }
    }
    nanosleep((const struct timespec[]){{0, 100000L}}, NULL);
    return NULL;
}



void *I_EXEC_SCHEDULER() {
    while (!ready_queue_empty || !wait_queue_empty ||running) {  
        struct queue_entry *io_request;
        
        // Lock the mutex to safely access the queue
        pthread_mutex_lock(&io_mutex);
        io_request = queue_peek_front(&to_io);
        pthread_mutex_unlock(&io_mutex);
        // If there is an I/O request
        if (io_request) {

            queue_pop_head(&to_io);
            // Parse the message to determine the type of I/O operation
            int fd, size;
            char *filename;
            const int MAX_OPS = 10; // A reasonable size for the operation array
            char *operation[MAX_OPS];
            char *saveptr; // For strtok_r
            int i = 0;
            
            char *str_copy = strdup(io_request->data);
            if (!str_copy) {
                perror("Failed to duplicate string");
                exit(EXIT_FAILURE);
            }

            char *token = strtok_r(str_copy, SPLIT_TOKEN, &saveptr);
            while (token && i < MAX_OPS - 1) {
                operation[i] = strdup(token);
                token = strtok_r(NULL, SPLIT_TOKEN, &saveptr);
                i = i + 1;
            }
            //Null terminate
            operation[i] = NULL;
            
            if (strcmp(operation[0], "open") == 0) {
                // Extract filename from args
                filename = operation[1];
                fd = open(filename, O_RDWR | O_CREAT, 0666);

                // Allocate memory for the file descriptor and store it
                char *open_res = malloc(BUFSIZ);
                sprintf(open_res, "opened%s%d%s", SPLIT_TOKEN, fd, SPLIT_TOKEN);

                // Prepare response message with the file descriptor and sed
                pthread_mutex_lock(&io_mutex);
                struct queue_entry *open_response = queue_new_node(open_res);
                queue_insert_tail(&from_io, open_response);
                pthread_mutex_unlock(&io_mutex);

                
            } else if (strcmp(operation[0], "read") == 0) {
                // Extract file descriptor and size from operation
                fd = atoi(operation[1]);
                size = atoi(operation[2]);
                char *buffer = malloc(size);
                read(fd, buffer, size);
                char *read_res = malloc(size+10); // Allocate a buffer of a standard size
                
                if (read_res != NULL) {
                    // Format the response string
                    sprintf(read_res, "reading%s%s", SPLIT_TOKEN, buffer);
                    // Prepare response message with the read data and sed
                    pthread_mutex_lock(&io_mutex);
                    struct queue_entry *read_response = queue_new_node(read_res);
                    queue_insert_tail(&from_io, read_response);
                    pthread_mutex_unlock(&io_mutex);
                } else {
                    perror("Failed to allocate memory for read response\n");
                }
                free(buffer);
            } else if (strcmp(operation[0], "write") == 0) {
                // Extract file descriptor, buffer and size from operation
                char *data_to_write = strdup(operation[2]);
                fd = atoi(operation[1]);
                size = atoi(operation[3]);
                write(fd, data_to_write, size);
                
            } else if (strcmp(operation[0], "close") == 0) {
                // Extract file descriptor from operation
                int res = close(atoi(operation[1]));
                if (res == 0){
                    pthread_mutex_lock(&io_mutex);
                    char *closed = malloc(1);
                    closed = "closed";
                    struct queue_entry *close_response = queue_new_node(closed);
                    queue_insert_tail(&from_io, close_response);
                    pthread_mutex_unlock(&io_mutex);
                } else {
                    perror("Failed to close file\n");
                }

            }

            // ADD to ready_queue
            pthread_mutex_lock(&cexec_mutex);
            struct queue_entry *ready = queue_peek_front(&wait_queue);
            if(ready) {
            queue_insert_tail(&ready_queue, ready);
            queue_pop_head(&wait_queue);
            ready_queue_empty = false;
            }
            pthread_mutex_unlock(&cexec_mutex);
            // Free the request data if necessary
            free(io_request->data);
            free(io_request);
            
        } else {
            // If there's nothing to do, sleep for a short period to avoid busy waiting
            wait_queue_empty = true; // testing this idea
            nanosleep((const struct timespec[]){{0, 100000L}}, NULL);
        }
    }
    return NULL;
}

// Send a write request to I_EXEC
void sut_write(int fd, char *buf, int size) {
    //Save context and send request to io
    ucontext_t current_context;
    getcontext(&current_context);
    struct queue_entry *task = queue_new_node(&current_context);
    
    pthread_mutex_lock(&cexec_mutex);
    queue_insert_tail(&wait_queue, task);
    wait_queue_empty = false;
    pthread_mutex_unlock(&cexec_mutex);

    //Building & sending request
    char *msg = malloc(BUFSIZ);
    snprintf(msg, BUFSIZ, "write%s%d%s%s%s%d%s", SPLIT_TOKEN, fd, SPLIT_TOKEN, buf, SPLIT_TOKEN, size, SPLIT_TOKEN);
    pthread_mutex_lock(&io_mutex);
    struct queue_entry *write_req = queue_new_node(msg);
    queue_insert_tail(&to_io, write_req);
    wait_queue_empty = false;
    pthread_mutex_unlock(&io_mutex);

    swapcontext(&current_context, &main_cexec);  

}

char* sut_read(int fd, char *buf, int size) {

    // save context
    ucontext_t current;
    getcontext(&current);
    struct queue_entry *task = queue_new_node(&current);
    
    pthread_mutex_lock(&cexec_mutex);
    queue_insert_tail(&wait_queue, task);
    wait_queue_empty = false;
    pthread_mutex_unlock(&cexec_mutex);

    // Send a read request
    char *msg = malloc(size+10); 
    snprintf(msg, BUFSIZ, "read%s%d%s%d", SPLIT_TOKEN, fd, SPLIT_TOKEN, size);
    
    pthread_mutex_lock(&io_mutex);
    struct queue_entry *read_req = queue_new_node(msg);
    queue_insert_tail(&to_io, read_req);
    wait_queue_empty = false;
    pthread_mutex_unlock(&io_mutex);

    swapcontext(&current, &main_cexec); 
    //Make sur we are waiting for response
    bool is_read = false;
    char *token;
    while(!is_read){
        pthread_mutex_lock(&io_mutex);
        struct queue_entry *io_response = queue_peek_front(&from_io);
        char *saveptr2; // For strtok_r
            
        char *str_copy2 = strdup(io_response->data);
        token = strtok_r(str_copy2, SPLIT_TOKEN, &saveptr2);

        if (strcmp(token, "reading") == 0){
            pthread_mutex_lock(&cexec_mutex);
            token = strtok_r(NULL, SPLIT_TOKEN, &saveptr2);
            strncpy(buf, token, size);
              
            queue_pop_head(&from_io);
            free(io_response->data);
            free(io_response);
            pthread_mutex_unlock(&cexec_mutex);
            is_read = true;           
    }
    pthread_mutex_unlock(&io_mutex);

    return token;
    nanosleep((const struct timespec[]){{0, 100000L}}, NULL);
    }
    return NULL;
}

// Create & Initialize Queue
// Create C_EXEC and I_EXEC threads 
void sut_init(){
    ready_queue = queue_create();
    wait_queue = queue_create();
    to_io = queue_create();
    from_io = queue_create();
    queue_init(&ready_queue);
    queue_init(&wait_queue);
    queue_init(&to_io);
    queue_init(&from_io);  
    pthread_create(&C_EXEC, NULL, C_EXEC_SCHEDULER, NULL);
    pthread_create(&I_EXEC, NULL, I_EXEC_SCHEDULER, NULL);
}

// Function that creates a new task and adds it to the ready queue
bool sut_create(sut_task_f fn) {
    // Allocate a new ucontext_t structure
    ucontext_t *task_context = malloc(sizeof(ucontext_t));
    if (task_context == NULL) {
        return false; // Return false if memory allocation fails
    }

    // Initialize the context to the current context
    if (getcontext(task_context) == -1) {
        free(task_context);
        return false; // Return false if getting the context fails
    }

    // Allocate a stack for the new task
    task_context->uc_stack.ss_sp = malloc(STACK_SIZE);
    if (task_context->uc_stack.ss_sp == NULL) {
        free(task_context);
        return false; // Return false if stack allocation fails
    }
    task_context->uc_stack.ss_size = STACK_SIZE;
    task_context->uc_link = &main_cexec; // Set up link to return to main context

    // Set the context to start the task function when switched to
    makecontext(task_context, (void (*)(void))fn, 0);

    // Wrap the context in a queue entry
    struct queue_entry *new_task = queue_new_node(task_context);
    if (new_task == NULL) {
        free(task_context->uc_stack.ss_sp);
        free(task_context);
        return false; // Return false if queue entry allocation fails
    }

    // Add the new task to the ready queue
    pthread_mutex_lock(&cexec_mutex);
    queue_insert_tail(&ready_queue, new_task);
    ready_queue_empty = false;
    pthread_mutex_unlock(&cexec_mutex);

    // Return true to indicate that the task was created successfully
    return true;
}

//This function allows a running task to yield execution before completing its function
void sut_yield() {
    // Dynamically allocate memory for the current context
    ucontext_t *current_context = malloc(sizeof(ucontext_t));
    if (current_context == NULL) {
        perror("Failed to allocate memory for context \n");
        return; // or handle the error as appropriate
    }
    getcontext(current_context);

    struct queue_entry *task = queue_new_node(current_context);

    // Safely add the task to the ready queue
    pthread_mutex_lock(&cexec_mutex);
    ready_queue_empty = false;
    queue_insert_tail(&ready_queue, task);
    pthread_mutex_unlock(&cexec_mutex);

    // Switch context to main scheduler
    swapcontext(current_context, &main_cexec);
}


int sut_open(char *file_name) {

    ucontext_t *current_context = malloc(sizeof(ucontext_t));
    getcontext(current_context);
    struct queue_entry *task = queue_new_node(current_context);
   
    pthread_mutex_lock(&cexec_mutex);
    queue_insert_tail(&wait_queue, task);
    wait_queue_empty = false;
    pthread_mutex_unlock(&cexec_mutex);
    char tmp[BUFSIZ];
    //send message to I-exec by adding request to to-io
    sprintf(tmp, "open%s%s%s", SPLIT_TOKEN, file_name, SPLIT_TOKEN);
    char *msg = strdup(tmp);
    struct queue_entry *open_req = queue_new_node(msg);

    pthread_mutex_lock(&io_mutex);
    queue_insert_tail(&to_io, open_req);
    wait_queue_empty = false;
    pthread_mutex_unlock(&io_mutex);
  
    swapcontext(current_context, &main_cexec);    

    // Wait for the response from I_EXEC
    bool opened = false;
    int fd_return = -1;
    
    while(!opened){
        pthread_mutex_lock(&io_mutex);
        struct queue_entry *io_response = queue_peek_front(&from_io);

        char *response_data = (char *)io_response->data;
        char *fd_ret[2];
        fd_ret[0] = strtok(response_data, SPLIT_TOKEN);
        fd_ret[1] = strtok(NULL, SPLIT_TOKEN);

        if (strcmp(fd_ret[0], "opened") == 0) {
            opened = true;
            fd_return = atoi(fd_ret[1]);
            queue_pop_head(&from_io);
            free(io_response->data);
            free(io_response);
        }

        pthread_mutex_unlock(&io_mutex);
        nanosleep((const struct timespec[]){{0, 100000L}}, NULL);
    }
    return fd_return; // Return the file descriptor or -1
}

void sut_close(int fd) {

    ucontext_t *current_context = malloc(sizeof(ucontext_t));
    getcontext(current_context);
    struct queue_entry *task = queue_new_node(current_context);
 
    pthread_mutex_lock(&cexec_mutex);
    queue_insert_tail(&wait_queue, task);
    wait_queue_empty = false;
    pthread_mutex_unlock(&cexec_mutex);

    // Send a close request to I_EXEC
    char *msg = malloc(BUFSIZ);
    sprintf(msg, "close%s%d%s", SPLIT_TOKEN, fd, SPLIT_TOKEN);

    pthread_mutex_lock(&io_mutex);
    struct queue_entry *close_req = queue_new_node(msg);
    queue_insert_tail(&to_io, close_req);
    wait_queue_empty = false;
    pthread_mutex_unlock(&io_mutex);

    swapcontext(current_context, &main_cexec);    

    // Wait for the confirmation from I_EXEC
    // Should implement if we have multiple files althought should be good since queue is FIFO
    bool closed = false;
    while (!closed){
        pthread_mutex_lock(&io_mutex);
        struct queue_entry *io_response = queue_peek_front(&from_io);
        if (io_response && strcmp(io_response->data, "close")){
            closed = true;
            queue_pop_head(&from_io);
        } 
        pthread_mutex_unlock(&io_mutex);
        nanosleep((const struct timespec[]){{0, 100000L}}, NULL);
    }
}

void sut_exit() {
    // Free the current task's stack and context
    ucontext_t current;
    getcontext(&current);
    free(current.uc_stack.ss_sp);
    free(current.uc_link);
    setcontext(&main_cexec);
}

void sut_shutdown() {  
    // Wait for the executors to finish
    pthread_join(C_EXEC, NULL);
    running = false;
    pthread_join(I_EXEC, NULL);
}
