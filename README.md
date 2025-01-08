# Simple Thread Scheduler

A lightweight user-level thread scheduler implemented in C, designed to manage compute and I/O tasks with first-come, first-served (FCFS) scheduling.

## Features

- **User-Level Threads**: Implements thread creation, context switching, and termination.
- **Dual Executors**: 
  - Compute Executor (C-EXEC): Handles compute tasks.
  - I/O Executor (I-EXEC): Manages blocking I/O tasks to prevent delays in compute operations.
- **Task Management**:
  - FCFS scheduling using ready and wait queues.
  - Tasks yield or terminate via `sut_yield()` and `sut_exit()`.
  - Dynamic task creation with `sut_create()`.
- **File I/O**: Support for opening, reading, writing, and closing files using dedicated functions (`sut_open()`, `sut_read()`, etc.).
- **Graceful Shutdown**: Terminates executors and cleans up resources with `sut_shutdown()`.

## API

- `sut_init()`: Initializes the scheduler.
- `sut_create(task_function)`: Creates a new task.
- `sut_yield()`: Pauses the current task and schedules the next one.
- `sut_exit()`: Terminates the current task.
- File operations: `sut_open()`, `sut_read()`, `sut_write()`, `sut_close()`.
- `sut_shutdown()`: Shuts down the scheduler and its executors.

## Technologies

- **Language**: C
- **Platform**: Linux/Unix
- **Key Libraries**: `makecontext()`, `swapcontext()`, queue management.

## How to Run

1. Clone this repository.
2. Compile the library and main program:
   ```bash
   gcc -o thread_scheduler thread_scheduler.c
