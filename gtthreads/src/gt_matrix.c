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

unsigned short use_gt_yield;

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
	/* Generate matrix of size `size` populated with val. */
	int i,j;
	mat->rows = size;
	mat->cols = size;

	mat->m = (int **)MALLOC_SAFE(size * sizeof(*(mat->m)));
	for (i = 0; i < size; ++i)
		mat->m[i] = (int *)MALLOC_SAFE(size * sizeof(*(mat->m[0])));

	for(i = 0; i < size; i++) {
		for (j = 0; j < size; j++) {
			mat->m[i][j] = val;
		}
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
//	fprintf(stderr, "\nThread(id:%d, group:%d) started\n", ptr->tid, ptr->gid);
//#endif

	for (i = start_row; i < end_row; i++)
	{
		for (j = start_col; j < end_col; j++)
			for (k = 0; k < ptr->_A->cols; k++)  /* _A->cols == _B->rows */
				ptr->_C->m[i][j] += ptr->_A->m[i][k] * ptr->_B->m[k][j];

		if (use_gt_yield && i == ptr->_A->rows / 2)
		{
			printf("Calling gt_yield(). Preempting.\n");
			gt_yield();
		}
	}

//#ifdef GT_THREADS
//	fprintf(stderr, "\nThread(id:%d, group:%d, cpu:%d) finished (TIME : %lu s and %lu us)",
//			ptr->tid, ptr->gid, cpuid, (tv2.tv_sec - tv1.tv_sec), (tv2.tv_usec - tv1.tv_usec));
//#else
//	gettimeofday(&tv2,NULL);
//	fprintf(stderr, "\nThread(id:%d, group:%d) finished (TIME : %lu s and %ld us)\n",
//			ptr->tid, ptr->gid, (tv2.tv_sec - tv1.tv_sec), (tv2.tv_usec - tv1.tv_usec));
//#endif

#undef ptr
	return 0;
}

/* NUM_THREADS triplets of matrices, one triplet for one uthread */
matrix_t matrices[128 * 3];

static void init_matrices()
{
	int sizes[] = {32, 64, 128, 256};
	for (int i = 0; i < 128; ++i)
	{
		int size = sizes[i/32];
		generate_matrix(&matrices[3 * i], size, 1);
		generate_matrix(&matrices[3 * i + 1], size, 1);
		generate_matrix(&matrices[3 * i + 2], size, 0);
	}

	return;
}

static void deallocate_matrices()
{
	for (int i = 0; i < 128 * 3; ++i)
	{
		for (int j = 0; j < matrices[i].rows; ++j)
			free(matrices[i].m[j]);
		free(matrices[i].m);
	}
}

void parse_args(int argc, char* argv[])
{
	int inx, load_balance = 0, uthread_scheduler = 0;

	use_gt_yield = 0;

	for(inx=0; inx<argc; inx++)
	{
		if(argv[inx][0]=='-')
		{
			if(!strcmp(&argv[inx][1], "lb"))
			{
				load_balance = 1;
				printf("Enabling load balancing\n");
				printf("NOTE: Meant for use with credit scheduler\n");
			}
			else if(!strcmp(&argv[inx][1], "s"))
			{
				inx++;
				if(!strcmp(&argv[inx][0], "0"))
				{
					uthread_scheduler = 0;
					printf("Using priority scheduler\n");
				}
				else if(!strcmp(&argv[inx][0], "1"))
				{
					uthread_scheduler = 1;
					printf("Using credit scheduler\n");
				}
			}
			else if (!strcmp(&argv[inx][1], "gt_yield"))
			{
				use_gt_yield = 1;
				printf("Voluntary Preemption using GT Yield enabled\n");
			}
		}
	}

	gtthread_app_init(uthread_scheduler, load_balance);
}



uthread_arg_t uargs[NUM_THREADS];
uthread_t utids[NUM_THREADS];

int main(int argc, char** argv)
{
	uthread_arg_t *uarg;
	int combination, credits[] = {25, 50, 75, 100};
	int inx, i, j, k;

	init_matrices();

	parse_args(argc, argv);

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
				uarg->_A = &matrices[3 * inx];
				uarg->_B = &matrices[3 * inx + 1];
				uarg->_C = &matrices[3 * inx + 2];

				uarg->tid = inx;

				uarg->gid = (inx % NUM_GROUPS);

//		uarg->start_row = (inx * PER_THREAD_ROWS);
//#ifdef GT_GROUP_SPLIT
//		/* Wanted to split the columns by groups !!! */
//		uarg->start_col = (uarg->gid * PER_GROUP_COLS);
//#endif

				uthread_create(&utids[inx], uthread_mulmat, uarg, uarg->gid, credits[j], 0);
			}
		}
	}

	gtthread_app_exit();

	deallocate_matrices();

	// print_matrix(&C);
	// fprintf(stderr, "********************************");
	return(0);
}
