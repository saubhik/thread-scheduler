# Project 1: Thread Scheduling with GTThreads

**Due Date - Friday, Feb 12, 2021, 11:59 PM**

## 0. Reminder
- Start to work this project as early as possible.
- Write code by yourself and be honest.
- Ask questions through Piazza or come to office hours.
- Test program on [VM cluster](./vm_userguide.md) before making compression and [submission](./project_1_submission.md) it.

## 1. Introduction
The goal of the project is to understand and implement a credit-based scheduler in a user-level threads library. 
Please refer to existed codecase and implement the credit scheduler design.

To get full score, students will need to finish the program and write a complete report.
The implementation details are listed at [section 2](#2-implementation); 
the requirment of report is listed at [section 3](#3-report).

### GTThreads Library

GTThreads is a user-level thread library. Some of the features of the library are as follows:

* Multi-Processor support

   The user-level threads (uthreads) are run on all the processors available on the system.

* Local runqueue

   Each CPU has its own runqueue. The uthreads are assigned to one of these run queues at the time of creation. Part of the work in the project might involve using some metrics before assigning these uthreads the processor and/or run-time runqueue balancing.

* O(1) priority scheduler and co-scheduler 

   The library includes these two scheduling algorithms. 
The code can be reused for reference. For example, the priority hash tables in the library can be reused for the purpose of the credit scheduler. 
In particular, look at the functions ``sched_find_best_uthread_group`` and ``sched_find_best_uthread``.

### Credit Scheduler

The credit scheduler is a proportional fair share CPU scheduler built from the ground up to be work conserving on SMP hosts.
You would also need to perform the migration of uthreads between the kthreads when there is a **idle** kthread 
(this maintains the principles of work-conserving and load balancing). Please take a look at the following resources:
* [XenWiki for Credit Scheduler](http://wiki.xenproject.org/wiki/CreditScheduler)
* [The introduction slides of Xen Credit CPU Scheduler](http://www-archive.xenproject.org/files/summit_3/sched.pdf)
* [Comparison of the Three CPU Schedulers in Xen](http://www.xen.org/files/xensummit_4/3schedulers-xen-summit_Cherkosova.pdf)

## 2. Implementation 
Students are required to finish following implementations with GTThreads library:
* The credit scheduler.
* Set up input arguments. 

   Enable two input arguements: (i) choosing the running scheduler, by flag ``-s``, with option ``0`` for O(1) priority scheduler and ``1`` for credit scheduler. (ii) choosing to run load balancing or not, by flag ``-lb``.

   E.g. ``$ ./bin/matrix -s 1 -lb`` this command means using credit scheduler with load balancing mechanism.

* Modify matrix multiplication.

   In original codebase, uthreads across CPUs run single matrix multiplication with separating the matrix by rows and columns. Students are asked to change it into that **each uthread works its own matrix multiplication**.

* CPU yield function - gt\_yield.

   When an uthread executes this function, it should yield the CPU to the scheduler, which then schedules the next thread (per its scheduling scheme). 
On voluntary preemption, the thread should be charged credits only for the actual CPU cycles used.
For this, students need to implement a library function for voluntary preemption **gt_yield()**.
Also, *students should print logs for the credit change*.

* Load balancing

   Implement uthread migration if a kthread is idle. To show the correctness, *students should print queue status after load balancing*.

* Complete Makefile and README

   Update ``Makefile`` and ``README.txt`` if you have special compile steps or instructions to run your program.

## 3. Report 

There is no specific style for the report, but make sure to cover at least following content:

* Present your understanding of GTThreads package, including the O(1) priority scheduler (max. 2 pages).
* Present briefly how the credit scheduler works (max. 2 pages), try to explain its basic algorithm and rules. 
* Sketch your design of credit scheduler. Show how do you implement it with the provided package.

   Clearly show when does your design add or deduct credit, why and when does it call gt\_yield, and how to do load balancing.

* Experiments

   Using this setup to evaluate credit scheduler's performance:
   - Run 128 uthreads.
   - Each uthread will work on a matrix of its own.
   - Credits are ranging in {25, 50, 75, 100}.
   - Matrix sizes are ranging in {32, 64, 128, 256}.
   - So there are 16 possible combinations of credit and matrix size. 
   
      Since there will be 128 uthreads, there will be 8 uthreads for each combination. (It isn't always interesting to parallelize matrix multiplication. Here multiple sets of threads need to be running over your scheduler with different workloads and priorities).

   - Collect the time taken (to the accuracy of micro-seconds) by each uthread, from the time it was created, to the time it completed its task. Also measure the CPU time that each uthread spent running (i.e., excluding the time that it spent waiting to be scheduled) every time it was scheduled. 

   - The final output should be as follows: For each set of 8 uthreads (having a unique combination of credits and matrix size), print the mean and the standard deviation of both, the individual thread run times, and the total execution times.
It will be useful if some output is printed while the process is running. For example, you may want to print messages when an uthread is put back in the queue, when it is picked from the queue, etc. You can print the output to a per-kthread file instead, if you want.

   While showing the numbers (or bar charts even better!), please try to explain your findings: 
   - Does credit scheduler work as expect? 
   - Does the load balancing mechanism improve the performance?
   - Does credit scheduler perform better than O(1) priority scheduler?

* Mention implementation issues if any

   You might want to point out some inefficiencies in your code. 
If there is any minor issues, like performance, don't spend too much time trying to fix it, 
but make sure you list ways in which you might improve it.


## 4. Delivering Suggestion

In order to successfully complete the project, it is recommended that during **the first week** you are able
(i) to understand the GTThreads library, (ii) the mechanisms and algorithm of the credit scheduler, 
and (iii) to have a solid design of the implementation plan you will pursue. 
It will be helpful to create diagrams illustrating the control flow of the GTThread library.

The major goal is implementing your credit scheduler design, 
you can follow the item ordering in [section 2](#2-implementation) for implementation, the one listed earlier is more important (more credit for sure). 

Don't wait until last minute to write report. 
As mentioned at [section 3](#3-report), some requirements related to your understanding and design ideas. 
Starting the write-up earlier not only helps you have better overview of the project, but gives you a more complete report to submit. 




