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
#include <string.h>
#include <math.h>

#include "gt_include.h"


//#define ROWS 512
//#define COLS ROWS
//#define SIZE COLS
//
#define NUM_CPUS 2
#define NUM_GROUPS NUM_CPUS
//#define PER_GROUP_COLS (SIZE/NUM_GROUPS)
//
//#define NUM_THREADS 32
#define NUM_THREADS 128
//#define PER_THREAD_ROWS (SIZE/NUM_THREADS)


/* A[SIZE][SIZE] X B[SIZE][SIZE] = C[SIZE][SIZE]
 * Let T(g, t) be thread 't' in group 'g'. 
 * T(g, t) is responsible for multiplication : 
 * A(rows)[(t-1)*SIZE -> (t*SIZE - 1)] X B(cols)[(g-1)*SIZE -> (g*SIZE - 1)] */

struct timeval thread_created_at[16][8];
struct timeval thread_completed_at[16][8];

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

	int combination; /* Matrix size x credit combination */
	int rank_in_set; /* Thread number in set of 8 threads */

}uthread_arg_t;

struct timeval tv1;

static void generate_matrix(matrix_t *mat, int size, int val)
{
	/* Generate matrix of size `size` populated with val. */
	int i,j;
	mat->rows = size;
	mat->cols = size;

	u_long len = sizeof(int *) * mat->rows + sizeof(int) * mat->rows * mat->cols;
	mat->m = (int **)MALLOC_SAFE(len);

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

extern int uthread_create(uthread_t *, void *, void *, uthread_group_t, int, int);

static void * uthread_mulmat(void *p)
{
	int i, j, k;
	int start_row, end_row;
	int start_col, end_col;
	unsigned int cpuid;
	struct timeval tv2;

#define ptr ((uthread_arg_t *)p)

	i=0; j= 0; k=0;

//	start_row = ptr->start_row;
	start_row = 0;
//	end_row = (ptr->start_row + PER_THREAD_ROWS);
	end_row = ptr->_A->rows;

//#ifdef GT_GROUP_SPLIT
//	start_col = ptr->start_col;
//	end_col = (ptr->start_col + PER_THREAD_ROWS);
//#else
	start_col = 0;
//	end_col = SIZE;
	end_col = ptr->_B->cols;
//#endif

//#ifdef GT_THREADS
//	cpuid = kthread_cpu_map[kthread_apic_id()]->cpuid;
//	fprintf(stderr, "\nThread(id:%d, group:%d, cpu:%d) started",ptr->tid, ptr->gid, cpuid);
//#else
	fprintf(stderr, "\nThread(id:%d, group:%d) started",ptr->tid, ptr->gid);
//#endif

	for(i = start_row; i < end_row; i++)
		for(j = start_col; j < end_col; j++)
			for(k = 0; k < ptr->_A->cols; k++)  /* _A->cols == _B->rows */
				ptr->_C->m[i][j] += ptr->_A->m[i][k] * ptr->_B->m[k][j];

//#ifdef GT_THREADS
//	fprintf(stderr, "\nThread(id:%d, group:%d, cpu:%d) finished (TIME : %lu s and %lu us)",
//			ptr->tid, ptr->gid, cpuid, (tv2.tv_sec - tv1.tv_sec), (tv2.tv_usec - tv1.tv_usec));
//#else
	gettimeofday(&tv2,NULL);
	thread_completed_at[ptr->combination][ptr->rank_in_set] = tv2;
	fprintf(stderr, "\nThread(id:%d, group:%d) finished (TIME : %lu s and %d us)",
			ptr->tid, ptr->gid, (tv2.tv_sec - tv1.tv_sec), (tv2.tv_usec - tv1.tv_usec));
//#endif

#undef ptr
	return 0;
}

/* NUM_THREADS triplets of matrices, one triplet for one uthread */
matrix_t A[NUM_THREADS], B[NUM_THREADS], C[NUM_THREADS];

static void init_matrices()
{
	int sizes[] = {32, 64, 128, 256}, size;
	for (int i = 0; i < NUM_THREADS; ++i)
	{
		size = sizes[i/32];
		generate_matrix(&A[i], size, 1);
		generate_matrix(&B[i], size, 1);
		generate_matrix(&C[i], size, 0);
	}

	return;
}

static void deallocate_matrices()
{
	for (int i = 0; i < NUM_THREADS; ++i)
	{
		for (int j = 0; j < A[i].rows; ++j)
			free(A[i].m[j]);
		free(A[i].m);
	}
}

static void print_creation_to_completion_time_stats()
{
	u_long mean, tmp[16][8];
	double stddev;
	struct timeval tv;
	int i, j;

	/* For each combination */
	for (i = 0; i < 16; ++i)
	{
		mean = 0;
		stddev = 0;

		/* For each thread in set */
		for (j = 0; j < 8; ++j)
		{
			timersub(&thread_completed_at[i][j], &thread_created_at[i][j], &tv);
			tmp[i][j] = tv.tv_sec * 1000000 + tv.tv_usec;
			mean += tmp[i][j];
		}
		mean /= 8;
		printf("Mean of thread %d's creation to completion time in combination %d is %lu\n", j, i, mean);

		/* For each thread in set */
		for (j = 0; j < 8; ++j)
			stddev += (double)(tmp[i][j] - mean) * (double)(tmp[i][j] - mean);
		stddev = sqrt(stddev / 8);
		printf("Standard Deviation of thread %d's creation to completion time in combination %d is %f\n", j, i, stddev);
	}
}

static void print_thread_run_time_stats(u_long** run_time)
{
	u_long mean;
	double stddev;
	int i, j;

	/* For each combination */
	for (i = 0; i < 16; ++i)
	{
		mean = 0;
		stddev = 0;

		/* For each thread in set */
		for (j = 0; j < 8; ++j)
			mean += run_time[i][j];
		mean /= 8;
		printf("Mean of thread %d's run time in combination %d is %lu\n", j, i, mean);

		/* For each thread in set */
		for (j = 0; j < 8; ++j)
			stddev += (double)(run_time[i][j] - mean) * (double)(run_time[i][j] - mean);
		stddev = sqrt(stddev / 8);
		printf("Standard Deviation of thread %d's run time in combination %d is %f\n", j, i, stddev);
	}
}

int parse_args(int argc, char* argv[])
{
	int inx, uthread_scheduler;

	for(inx=0; inx<argc; inx++)
	{
		if(argv[inx][0]=='-')
		{
			if(!strcmp(&argv[inx][1], "lb"))
			{
				//TODO: add option of load balancing mechanism
				printf("enable load balancing\n");
			}
			else if(!strcmp(&argv[inx][1], "s"))
			{
				inx++;
				if(!strcmp(&argv[inx][0], "0"))
				{
					uthread_scheduler= 0;
					printf("use priority scheduler\n");
				}
				else if(!strcmp(&argv[inx][0], "1"))
				{
					uthread_scheduler = 1;
					printf("use credit scheduler\n");
				}
			}
		}
	}

	return uthread_scheduler;
}



uthread_arg_t uargs[NUM_THREADS];
uthread_t utids[NUM_THREADS];

int main(int argc, char** argv)
{
	uthread_arg_t *uarg;
	int uthread_scheduler, combination, credits[] = {25, 50, 75, 100};
	int inx, i, j, k;

	uthread_scheduler = parse_args(argc, argv);

	gtthread_app_init(uthread_scheduler);

	init_matrices();

	gettimeofday(&tv1,NULL);

	/* For each matrix size */
	for (i = 0; i < 4; ++i)
	{
		/* For each credit */
		for (j = 0; j < 4; ++j)
		{
			combination = 4 * i + j;
			/* For each of 8 threads to the combination */
			for (k = 0; k < 8; ++k)
			{
				inx = combination * 8 + k;
				uarg = &uargs[inx];
				uarg->_A = &A[inx];
				uarg->_B = &B[inx];
				uarg->_C = &C[inx];

				uarg->tid = inx;

				uarg->gid = (inx % NUM_GROUPS);

				uarg->combination = combination;
				uarg->rank_in_set = k;

				gettimeofday(&thread_created_at[combination][k], NULL);

//		uarg->start_row = (inx * PER_THREAD_ROWS);
//#ifdef GT_GROUP_SPLIT
//		/* Wanted to split the columns by groups !!! */
//		uarg->start_col = (uarg->gid * PER_GROUP_COLS);
//#endif

				uthread_create(&utids[inx], uthread_mulmat, uarg, uarg->gid, credits[j], 0);
			}
		}
	}

	u_long** thread_run_time = gtthread_app_exit();

	print_creation_to_completion_time_stats();
	print_thread_run_time_stats(thread_run_time);

	deallocate_matrices();
	for (i = 0; i < 16; ++i)
		free(thread_run_time[i]);
	free(thread_run_time);

	// print_matrix(&C);
	// fprintf(stderr, "********************************");
	return(0);
}
