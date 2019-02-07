#include <stdio.h>
#include <unistd.h>
#include <linux/unistd.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sched.h>
#include <signal.h>
#include <setjmp.h>
#include <errno.h>
#include <assert.h>
#include <stdlib.h> /** to use atoi() **/
#include <math.h> /** to use sart() **/

#include "gt_include.h"


#if 0
#define ROWS 32
#define COLS ROWS
#define SIZE COLS

#define NUM_CPUS 2
#define NUM_GROUPS NUM_CPUS
#define PER_GROUP_COLS (SIZE/NUM_GROUPS)

#define NUM_THREADS 32
#define PER_THREAD_ROWS (SIZE/NUM_THREADS)
#endif

#define NUM_CPUS 2
#define NUM_GROUPS NUM_CPUS
#define NUM_THREADS 128

unsigned long raw_time[16][8]; /** raw time period of eachh uthread **/
struct timeval start_time[16][8]; /** creation time of each uthread **/

/* A[SIZE][SIZE] X B[SIZE][SIZE] = C[SIZE][SIZE]
 * Let T(g, t) be thread 't' in group 'g'. 
 * T(g, t) is responsible for multiplication : 
 * A(rows)[(t-1)*SIZE -> (t*SIZE - 1)] X B(cols)[(g-1)*SIZE -> (g*SIZE - 1)] */

typedef struct matrix
{
	int **m;

	int rows;
	int cols;
	unsigned int reserved[2];
} matrix_t;


typedef struct __uthread_arg
{
	matrix_t *_A, *_B, *_C;
	unsigned int reserved0;

	unsigned int tid;
	unsigned int gid;
	int start_row; /* start_row -> (start_row + PER_THREAD_ROWS) */
	int start_col; /* start_col -> (start_col + PER_GROUP_COLS) */
	
}uthread_arg_t;
	
struct timeval tv1;

static void generate_matrix(matrix_t *mat, int size, int val)
{
	mat->m = (int **) MALLOC_SAFE(size * sizeof(*(mat->m)));
	for (int idx = 0; idx < size; ++idx) mat->m[idx] = (int *) MALLOC_SAFE(size * sizeof(*(mat->m[0])));
	int i,j;
	mat->rows = size;
	mat->cols = size;
	for(i = 0; i < mat->rows; i++)
		for( j = 0; j < mat->cols; j++)
		{
			mat->m[i][j] = val;
		}
	return;
}

static void print_matrix(matrix_t *mat)
{
	int i, j;

	for(i=0;i<mat->rows;i++)
	{
		for(j=0;j<mat->cols;j++)
			printf(" %d ",mat->m[i][j]);
		printf("\n");
	}

	return;
}

static int uthread_mulmat(void *p)
{
	int i, j, k;
	int start_row, end_row;
	int start_col, end_col;
	unsigned int cpuid;
	struct timeval tv2;

#define ptr ((uthread_arg_t *)p)

	i=0; j= 0; k=0;

	/** now the work allocated to each thread needs to be calculated **/
	int size = ptr->_A->rows;

	start_row = 0;
	end_row = size;

#ifdef GT_GROUP_SPLIT
	start_col = ptr->start_col;
	end_col = (ptr->start_col + ptr->_A->cols/NUM_GROUPS);
#else
	start_col = 0;
	end_col = size;
#endif

#ifdef GT_THREADS
	cpuid = kthread_cpu_map[kthread_apic_id()]->cpuid;
	fprintf(stderr, "\nThread(id:%d, group:%d, cpu:%d) started",ptr->tid, ptr->gid, cpuid);
#else
	fprintf(stderr, "\nThread(id:%d, group:%d) started",ptr->tid, ptr->gid);
#endif

	for(i = start_row; i < end_row; i++)
		for(j = start_col; j < end_col; j++)
			for(k = 0; k < ptr->_A->rows; k++)
				ptr->_C->m[i][j] += ptr->_A->m[i][k] * ptr->_B->m[k][j];

#ifdef GT_THREADS
	fprintf(stderr, "\nThread(id:%d, group:%d, cpu:%d) finished (TIME : %lu s and %lu us)",
			ptr->tid, ptr->gid, cpuid, (tv2.tv_sec - tv1.tv_sec), (tv2.tv_usec - tv1.tv_usec));
#else
	gettimeofday(&tv2,NULL);
	struct timeval tv3 = start_time[ptr->tid/8][ptr->tid%8];
	raw_time[ptr->tid/8][ptr->tid%8] = (tv2.tv_sec*1000000 + tv2.tv_usec) - (tv3.tv_sec*1000000 + tv3.tv_usec);
	fprintf(stderr, "\nThread(id:%d, group:%d) finished (TIME : %lu s and %lu us)",
			ptr->tid, ptr->gid, (tv2.tv_sec - tv1.tv_sec), (tv2.tv_usec - tv1.tv_usec));
#endif

#undef ptr

	return 0;
}

matrix_t matrices[3*128];

static void init_matrices()
{
	int mat_size_arr[] = {32, 64, 128, 256};

	for (int i = 0; i < 128; ++i) {
		int cur_size = mat_size_arr[i/32];
		generate_matrix(&matrices[i*3], cur_size, 1);
		generate_matrix(&matrices[i*3 + 1], cur_size, 1);
		generate_matrix(&matrices[i*3 + 2], cur_size, 0);
	}

	return;
}

static void free_matrices() {
	for (int i = 0; i < 48; ++i) {
		for (int j = 0; j < matrices[i].rows; ++j) {
			free(matrices[i].m[j]);
		}
		free(matrices[i].m);
	}
}

static void print_raw_time() {
	unsigned long mean;
	double std_dev;
	for (int i =0; i < 16; ++i) {
		mean = 0;
		std_dev = 0;
		for (int j = 0; j < 8; ++j) {
			//if (i == 0) printf("%lu\n", raw_time[i][j]);
			mean += raw_time[i][j];
		}
		mean /= 8;
		printf("mean uthread raw time for uthread set %d is %lu\n", i, mean);
		for (int j = 0; j < 8; ++j) {
			std_dev += (raw_time[i][j] - mean) * (raw_time[i][j] - mean);
		}
		std_dev = sqrt(std_dev/8);
		printf("standard deviation of uthread raw time for uthread set %d is %f\n\n", i, std_dev);
	}
}

static void print_cpu_time(unsigned long** cpu_time) {
	unsigned long mean;
	double std_dev;
	for (int i = 0; i < 16; ++i) {
		mean = 0;
		std_dev = 0;
		for (int j = 0; j < 8; ++j) {
			//if (i == 5) printf("%lu\n", cpu_time[i][j]);
			mean += cpu_time[i][j];
		}
		mean /= 8;
		printf("mean uthread cpu time for uthread set %d is %lu\n", i, mean);
		for (int j = 0; j < 8; ++j) {
			std_dev += (cpu_time[i][j] - mean) * (cpu_time[i][j] - mean);
		}
		std_dev = sqrt(std_dev/8);
		printf("standard deviation of uthread cpu time for uthread set %d is %f\n\n", i, std_dev);
	}
}


uthread_arg_t uargs[NUM_THREADS];
uthread_t utids[NUM_THREADS];

int main(int argc, char** argv)
{
	int credit_arr[] = {25, 50, 75, 100};

	uthread_arg_t *uarg;
	int inx;
	int kthread_sched_arg = atoi(argv[1]);
	printf("kthread_sched_arg: %d\n", kthread_sched_arg);
	//int weight = atoi(argv[1]);
	//int cap = atoi(argv[2]);


	gtthread_app_init(kthread_sched_arg);

	init_matrices();

	gettimeofday(&tv1,NULL);

	/** size **/
	for (int i = 0; i < 4; ++i) { 
		/** credit **/
		for(int j = 0; j < 4; ++j)
		{
			int comb = (4*i + j);
			for (int k = 0; k < 8; ++k) {
				inx = comb*8 + k;
				uarg = &uargs[inx];
				uarg->_A = &matrices[inx*3];
				uarg->_B = &matrices[inx*3 + 1];
				uarg->_C = &matrices[inx*3 + 2];

				uarg->tid = inx;

				uarg->gid = (inx % NUM_GROUPS);

#ifdef GT_GROUP_SPLIT
				/* Wanted to split the columns by groups !!! */
				uarg->start_col = (uarg->gid * uarg->_A->cols/NUM_GROUPS);
#endif

				gettimeofday(&start_time[inx/8][inx%8], NULL);
				uthread_create(&utids[inx], uthread_mulmat, uarg, uarg->gid, credit_arr[j], 0);
			}
		}
	}

	unsigned long** cpu_time = gtthread_app_exit();

	print_raw_time();
	fprintf(stderr, "**************************************************************\n\n");
	print_cpu_time(cpu_time);
	//print_matrix(&matrices[98]);
	free_matrices();
	for (int i = 0; i < 16; ++i) free(cpu_time[i]);
	free(cpu_time);
	//fprintf(stderr, "********************************");
	return(0);
}
