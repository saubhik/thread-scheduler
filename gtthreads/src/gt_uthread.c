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

#include "gt_include.h"
/**********************************************************************/
/** DECLARATIONS **/
/**********************************************************************/


/**********************************************************************/
/* kthread runqueue and env */

/* XXX: should be the apic-id */
#define KTHREAD_CUR_ID	0

/**********************************************************************/
/* uthread scheduling */
static void uthread_context_func(int);
static int uthread_init(uthread_struct_t *u_new);

/**********************************************************************/
/* uthread creation */
#define UTHREAD_DEFAULT_SSIZE (16 * 1024)

extern int uthread_create(uthread_t *u_tid, int (*u_func)(void *), void *u_arg, uthread_group_t u_gid, int u_weight, int u_cap);

/**********************************************************************/
/** DEFNITIONS **/
/**********************************************************************/

/**********************************************************************/
/* uthread scheduling */

/* Assumes that the caller has disabled vtalrm and sigusr1 signals */
/* uthread_init will be using */
static int uthread_init(uthread_struct_t *u_new)
{
	stack_t oldstack;
	sigset_t set, oldset;
	struct sigaction act, oldact;

	gt_spin_lock(&(ksched_shared_info.uthread_init_lock));

	/* Register a signal(SIGUSR2) for alternate stack */
	act.sa_handler = uthread_context_func;
	act.sa_flags = (SA_ONSTACK | SA_RESTART);
	if(sigaction(SIGUSR2,&act,&oldact))
	{
		fprintf(stderr, "uthread sigusr2 install failed !!");
		return -1;
	}

	/* Install alternate signal stack (for SIGUSR2) */
	if(sigaltstack(&(u_new->uthread_stack), &oldstack))
	{
		fprintf(stderr, "uthread sigaltstack install failed.");
		return -1;
	}

	/* Unblock the signal(SIGUSR2) */
	sigemptyset(&set);
	sigaddset(&set, SIGUSR2);
	sigprocmask(SIG_UNBLOCK, &set, &oldset);


	/* SIGUSR2 handler expects kthread_runq->cur_uthread
	 * to point to the newly created thread. We will temporarily
	 * change cur_uthread, before entering the synchronous call
	 * to SIGUSR2. */

	/* kthread_runq is made to point to this new thread
	 * in the caller. Raise the signal(SIGUSR2) synchronously */
#if 0
	raise(SIGUSR2);
#endif
	syscall(__NR_tkill, kthread_cpu_map[kthread_apic_id()]->tid, SIGUSR2);

	/* Block the signal(SIGUSR2) */
	sigemptyset(&set);
	sigaddset(&set, SIGUSR2);
	sigprocmask(SIG_BLOCK, &set, &oldset);
	if(sigaction(SIGUSR2,&oldact,NULL))
	{
		fprintf(stderr, "uthread sigusr2 revert failed !!");
		return -1;
	}

	/* Disable the stack for signal(SIGUSR2) handling */
	u_new->uthread_stack.ss_flags = SS_DISABLE;

	/* Restore the old stack/signal handling */
	if(sigaltstack(&oldstack, NULL))
	{
		fprintf(stderr, "uthread sigaltstack revert failed.");
		return -1;
	}

	gt_spin_unlock(&(ksched_shared_info.uthread_init_lock));
	return 0;
}

extern void uthread_schedule(uthread_struct_t * (*kthread_best_sched_uthread)(kthread_runqueue_t *))
{
	kthread_context_t *k_ctx;
	kthread_runqueue_t *kthread_runq;
	uthread_struct_t *u_obj;

	/* Signals used for cpu_thread scheduling */
	// kthread_block_signal(SIGVTALRM);
	// kthread_block_signal(SIGUSR1);

#if 0
	fprintf(stderr, "uthread_schedule invoked !!\n");
#endif

	k_ctx = kthread_cpu_map[kthread_apic_id()];
	kthread_runq = &(k_ctx->krunqueue);

	if((u_obj = kthread_runq->cur_uthread))
	{
		struct timeval tv, curr_consumed_tv;
		gettimeofday(&tv, NULL);

		timersub(&tv, &(u_obj->last_scheduled_at), &curr_consumed_tv);
		printf("Thread(id:%d, group:%d) consumed %lu s and %ld us of CPU time\n", u_obj->uthread_tid, u_obj->uthread_gid, curr_consumed_tv.tv_sec, curr_consumed_tv.tv_usec);
		timeradd(&(u_obj->agg_cpu_time), &curr_consumed_tv, &(u_obj->agg_cpu_time));

		/* Assuming credit-to-CPU-time conversion of 1 credit = 2ms */
		u_int64_t debit = (curr_consumed_tv.tv_sec * (u_int64_t)500) + (curr_consumed_tv.tv_usec / 2000);
		u_obj->uthread_credit -= debit;
		printf("CREDIT CHANGE: %lu debited from Thread(id:%d, group:%d)\n", debit, u_obj->uthread_tid, u_obj->uthread_gid);

		/*Go through the runq and schedule the next thread to run */
		kthread_runq->cur_uthread = NULL;

		if(u_obj->uthread_state & (UTHREAD_DONE | UTHREAD_CANCELLED))
		{
			/* Thread will not be scheduled again, update thread_cpu_time */
			ksched_shared_info.thread_cpu_time[u_obj->uthread_tid / 8][u_obj->uthread_tid % 8] = u_obj->agg_cpu_time;

			/* Update thread creation to completion time since uthread has completed task */
			struct timeval c2c_time;
			timersub(&tv, &(u_obj->created_at), &c2c_time);
			ksched_shared_info.thread_c2c_time[u_obj->uthread_tid / 8][u_obj->uthread_tid % 8] = c2c_time;

			/* XXX: Inserting uthread into zombie queue is causing improper
			 * cleanup/exit of uthread (core dump) */
			uthread_head_t * kthread_zhead = &(kthread_runq->zombie_uthreads);
			gt_spin_lock(&(kthread_runq->kthread_runqlock));
			kthread_runq->kthread_runqlock.holder = 0x01;
			TAILQ_INSERT_TAIL(kthread_zhead, u_obj, uthread_runq);
			gt_spin_unlock(&(kthread_runq->kthread_runqlock));

			{
				ksched_shared_info_t *ksched_info = &ksched_shared_info;	
				gt_spin_lock(&ksched_info->ksched_lock);
				ksched_info->kthread_cur_uthreads--;
				gt_spin_unlock(&ksched_info->ksched_lock);
			}
		}
		else
		{
			/* XXX: Apply uthread_group_penalty before insertion */
			u_obj->uthread_state = UTHREAD_RUNNABLE;
			if (ksched_shared_info.uthread_scheduler == 0) {
				/* For O(1) priority scheduler */
				add_to_runqueue(kthread_runq->expires_runq, &(kthread_runq->kthread_runqlock), u_obj);
			}
			else
			{
				if (u_obj->uthread_credit < 0)
				{
					/* Add to expires run queue as priority is OVER */
					add_to_runqueue(kthread_runq->expires_runq, &(kthread_runq->kthread_runqlock), u_obj);
					printf("Thread(id:%d, group:%d) inserted into expires runqueue, because it's credit %d < 0\n", u_obj->uthread_tid, u_obj->uthread_gid, u_obj->uthread_credit);
				}
				else
				{
					/* Add to active run queue as priority is UNDER */
					add_to_runqueue(kthread_runq->active_runq, &(kthread_runq->kthread_runqlock), u_obj);
					printf("Thread(id:%d, group:%d) inserted into active runqueue, because it's credit %d >= 0\n", u_obj->uthread_tid, u_obj->uthread_gid, u_obj->uthread_credit);
				}
			}
			/* XXX: Save the context (signal mask not saved) */
			if(sigsetjmp(u_obj->uthread_env, 0))
				return;
		}
	}

	/* kthread_best_sched_uthread acquires kthread_runqlock. Dont lock it up when calling the function. */
	if(!(u_obj = kthread_best_sched_uthread(kthread_runq)))
	{
		/* Done executing all uthreads. Return to main */
		/* XXX: We can actually get rid of KTHREAD_DONE flag */
		if(ksched_shared_info.kthread_tot_uthreads && !ksched_shared_info.kthread_cur_uthreads)
		{
			fprintf(stderr, "Quitting kthread (%d)\n", k_ctx->cpuid);
			k_ctx->kthread_flags |= KTHREAD_DONE;
		}

		siglongjmp(k_ctx->kthread_env, 1);
		return;
	}

	kthread_runq->cur_uthread = u_obj;

	gettimeofday(&(u_obj->last_scheduled_at), NULL);
	printf("Thread(id:%d, group:%d) is scheduled.\n", u_obj->uthread_tid, u_obj->uthread_gid);

	if((u_obj->uthread_state == UTHREAD_INIT) && (uthread_init(u_obj)))
	{
		fprintf(stderr, "uthread_init failed on kthread(%d)\n", k_ctx->cpuid);
		exit(0);
	}

	u_obj->uthread_state = UTHREAD_RUNNING;

	/* Re-install the scheduling signal handlers */
	kthread_install_sighandler(SIGVTALRM, k_ctx->kthread_sched_timer);
	kthread_install_sighandler(SIGUSR1, k_ctx->kthread_sched_relay);
	/* Jump to the selected uthread context */
	siglongjmp(u_obj->uthread_env, 1);

	return;
}


/* For uthreads, we obtain a seperate stack by registering an alternate
 * stack for SIGUSR2 signal. Once the context is saved, we turn this 
 * into a regular stack for uthread (by using SS_DISABLE). */
static void uthread_context_func(int signo)
{
	uthread_struct_t *cur_uthread;
	kthread_runqueue_t *kthread_runq;

	kthread_runq = &(kthread_cpu_map[kthread_apic_id()]->krunqueue);

	printf("..... uthread_context_func .....\n");
	/* kthread->cur_uthread points to newly created uthread */
	if(!sigsetjmp(kthread_runq->cur_uthread->uthread_env,0))
	{
		/* In UTHREAD_INIT : saves the context and returns.
		 * Otherwise, continues execution. */
		/* DONT USE any locks here !! */
		assert(kthread_runq->cur_uthread->uthread_state == UTHREAD_INIT);
		kthread_runq->cur_uthread->uthread_state = UTHREAD_RUNNABLE;
		return;
	}

	/* UTHREAD_RUNNING : siglongjmp was executed. */
	cur_uthread = kthread_runq->cur_uthread;
	assert(cur_uthread->uthread_state == UTHREAD_RUNNING);
	/* Execute the uthread task */
	cur_uthread->uthread_func(cur_uthread->uthread_arg);
	cur_uthread->uthread_state = UTHREAD_DONE;

	gt_yield();
	return;
}

extern void gt_yield() {
	if (ksched_shared_info.uthread_scheduler == 0)
		uthread_schedule(&sched_find_best_uthread);
	else
		/* For credit scheduler */
		uthread_schedule(&sched_find_next_uthread);
}

/**********************************************************************/
/* uthread creation */

extern kthread_runqueue_t *ksched_find_target(uthread_struct_t *);

extern int uthread_create(uthread_t *u_tid, int (*u_func)(void *), void *u_arg, uthread_group_t u_gid, int u_weight, int u_cap)
{
	kthread_runqueue_t *kthread_runq;
	uthread_struct_t *u_new;

	/* Signals used for cpu_thread scheduling */
	// kthread_block_signal(SIGVTALRM);
	// kthread_block_signal(SIGUSR1);

	/* create a new uthread structure and fill it */
	if(!(u_new = (uthread_struct_t *)MALLOCZ_SAFE(sizeof(uthread_struct_t))))
	{
		fprintf(stderr, "uthread mem alloc failure !!");
		exit(0);
	}

	u_new->uthread_state = UTHREAD_INIT;
	u_new->uthread_priority = DEFAULT_UTHREAD_PRIORITY;
	u_new->uthread_gid = u_gid;
	u_new->uthread_func = u_func;
	u_new->uthread_arg = u_arg;

	/* For credit scheduler */
	u_new->uthread_weight = u_weight;
	u_new->uthread_credit = u_weight;
	u_new->uthread_cap = u_cap;

	/* For run time calculation */
	u_new->last_scheduled_at.tv_sec = 0;
	u_new->last_scheduled_at.tv_usec = 0;
	u_new->agg_cpu_time.tv_sec = 0;
	u_new->agg_cpu_time.tv_usec = 0;

	/* Set created_at */
	gettimeofday(&(u_new->created_at), NULL);

	/* Allocate new stack for uthread */
	u_new->uthread_stack.ss_flags = 0; /* Stack enabled for signal handling */
	if(!(u_new->uthread_stack.ss_sp = (void *)MALLOC_SAFE(UTHREAD_DEFAULT_SSIZE)))
	{
		fprintf(stderr, "uthread stack mem alloc failure !!");
		return -1;
	}
	u_new->uthread_stack.ss_size = UTHREAD_DEFAULT_SSIZE;


	{
		ksched_shared_info_t *ksched_info = &ksched_shared_info;

		gt_spin_lock(&ksched_info->ksched_lock);
		u_new->uthread_tid = ksched_info->kthread_tot_uthreads++;
		ksched_info->kthread_cur_uthreads++;
		gt_spin_unlock(&ksched_info->ksched_lock);
	}

	/* XXX: ksched_find_target should be a function pointer */
	kthread_runq = ksched_find_target(u_new);

	*u_tid = u_new->uthread_tid;
	/* Queue the uthread for target-cpu. Let target-cpu take care of initialization. */
	add_to_runqueue(kthread_runq->active_runq, &(kthread_runq->kthread_runqlock), u_new);


	/* WARNING : DONOT USE u_new WITHOUT A LOCK, ONCE IT IS ENQUEUED. */

	/* Resume with the old thread (with all signals enabled) */
	// kthread_unblock_signal(SIGVTALRM);
	// kthread_unblock_signal(SIGUSR1);

	return 0;
}

#if 0
/**********************************************************************/
kthread_runqueue_t kthread_runqueue;
kthread_runqueue_t *kthread_runq = &kthread_runqueue;
sigjmp_buf kthread_env;

/* Main Test */
typedef struct uthread_arg
{
	int num1;
	int num2;
	int num3;
	int num4;	
} uthread_arg_t;

#define NUM_THREADS 10
static int func(void *arg);

int main()
{
	uthread_struct_t *uthread;
	uthread_t u_tid;
	uthread_arg_t *uarg;

	int inx;

	/* XXX: Put this lock in kthread_shared_info_t */
	gt_spinlock_init(&uthread_group_penalty_lock);

	/* spin locks are initialized internally */
	kthread_init_runqueue(kthread_runq);

	for(inx=0; inx<NUM_THREADS; inx++)
	{
		uarg = (uthread_arg_t *)MALLOC_SAFE(sizeof(uthread_arg_t));
		uarg->num1 = inx;
		uarg->num2 = 0x33;
		uarg->num3 = 0x55;
		uarg->num4 = 0x77;
		uthread_create(&u_tid, func, uarg, (inx % MAX_UTHREAD_GROUPS));
	}

	kthread_init_vtalrm_timeslice();
	kthread_install_sighandler(SIGVTALRM, kthread_sched_vtalrm_handler);
	if(sigsetjmp(kthread_env, 0) > 0)
	{
		/* XXX: (TODO) : uthread cleanup */
		exit(0);
	}

	uthread_schedule(&ksched_priority);
	return(0);
}

static int func(void *arg)
{
	unsigned int count;
#define u_info ((uthread_arg_t *)arg)
	printf("Thread %d created\n", u_info->num1);
	count = 0;
	while(count <= 0xffffff)
	{
		if(!(count % 5000000))
			printf("uthread(%d) => count : %d\n", u_info->num1, count);
		count++;
	}
#undef u_info
	return 0;
}
#endif
