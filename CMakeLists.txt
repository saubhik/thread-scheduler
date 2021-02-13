cmake_minimum_required(VERSION 3.17)
project(cs6210_spring21_project1)

set(CMAKE_CXX_STANDARD 14)

include_directories(gtthreads/src)

add_executable(cs6210_spring21_project1
        gtthreads/src/gt_bitops.h
        gtthreads/src/gt_include.h
        gtthreads/src/gt_kthread.c
        gtthreads/src/gt_kthread.h
        gtthreads/src/gt_matrix.c
        gtthreads/src/gt_pq.c
        gtthreads/src/gt_pq.h
        gtthreads/src/gt_signal.c
        gtthreads/src/gt_signal.h
        gtthreads/src/gt_spinlock.c
        gtthreads/src/gt_spinlock.h
        gtthreads/src/gt_tailq.h
        gtthreads/src/gt_uthread.c
        gtthreads/src/gt_uthread.h
        gtthreads/Makefile)
