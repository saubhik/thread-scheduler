1. Type 'make' to compile the GTThreads library 
2. Type 'make matrix' to compile the matrix program
3. ./bin/matrix to run the matrix program
    - Use '-s 1' to use the credit schduler.
    - Use '-s 0' to use the O(1) priority scheduler.
    - Use '-lb' to use the load balancing feature.
    - Use '-gt_yield' to use voluntary preemption during matrix multiplication.

NOTE: '-lb' works only with credit scheduler.
