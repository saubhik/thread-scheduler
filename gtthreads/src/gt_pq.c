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
/* runqueue operations */
static void __add_to_runqueue(runqueue_t *runq, uthread_struct_t *u_elm);
static void __rem_from_runqueue(runqueue_t *runq, uthread_struct_t *u_elm);


/**********************************************************************/
/* runqueue operations */
static inline void __add_to_runqueue(runqueue_t *runq, uthread_struct_t *u_elem)
{
	unsigned int uprio, ugroup;
	uthread_head_t *uhead;

	/* Find a position in the runq based on priority and group.
	 * Update the masks. */
	uprio = u_elem->uthread_priority;
	ugroup = u_elem->uthread_gid;

	/* Insert at the tail */
	uhead = &runq->prio_array[uprio].group[ugroup];
	TAILQ_INSERT_TAIL(uhead, u_elem, uthread_runq);

	/* Update information */
	if(!IS_BIT_SET(runq->prio_array[uprio].group_mask, ugroup))
		SET_BIT(runq->prio_array[uprio].group_mask, ugroup);

	runq->uthread_tot++;

	runq->uthread_prio_tot[uprio]++;
	if(!IS_BIT_SET(runq->uthread_mask, uprio))
		SET_BIT(runq->uthread_mask, uprio);

	runq->uthread_group_tot[ugroup]++;
	if(!IS_BIT_SET(runq->uthread_group_mask[ugroup], uprio))
		SET_BIT(runq->uthread_group_mask[ugroup], uprio);

	return;
}

static inline void __rem_from_runqueue(runqueue_t *runq, uthread_struct_t *u_elem)
{
	unsigned int uprio, ugroup;
	uthread_head_t *uhead;

	/* Find a position in the runq based on priority and group.
	 * Update the masks. */
	uprio = u_elem->uthread_priority;
	ugroup = u_elem->uthread_gid;

	/* Insert at the tail */
	uhead = &runq->prio_array[uprio].group[ugroup];
	TAILQ_REMOVE(uhead, u_elem, uthread_runq);

	/* Update information */
	if(TAILQ_EMPTY(uhead))
		RESET_BIT(runq->prio_array[uprio].group_mask, ugroup);

	runq->uthread_tot--;

	if(!(--(runq->uthread_prio_tot[uprio])))
		RESET_BIT(runq->uthread_mask, uprio);

	if(!(--(runq->uthread_group_tot[ugroup])))
	{
		assert(TAILQ_EMPTY(uhead));
		RESET_BIT(runq->uthread_group_mask[ugroup], uprio);
	}

	return;
}


/**********************************************************************/
/* Exported runqueue operations */
extern void init_runqueue(runqueue_t *runq)
{
	uthread_head_t *uhead;
	int i, j;
	/* Everything else is global, so already initialized to 0(correct init value) */
	for(i=0; i<MAX_UTHREAD_PRIORITY; i++)
	{
		for(j=0; j<MAX_UTHREAD_GROUPS; j++)
		{
			uhead = &((runq)->prio_array[i].group[j]);
			TAILQ_INIT(uhead);
		}
	}
	return;
}

extern void add_to_runqueue(runqueue_t *runq, gt_spinlock_t *runq_lock, uthread_struct_t *u_elem)
{
	gt_spin_lock(runq_lock);
	runq_lock->holder = 0x02;
	__add_to_runqueue(runq, u_elem);
	gt_spin_unlock(runq_lock);
	return;
}

extern void rem_from_runqueue(runqueue_t *runq, gt_spinlock_t *runq_lock, uthread_struct_t *u_elem)
{
	gt_spin_lock(runq_lock);
	runq_lock->holder = 0x03;
	__rem_from_runqueue(runq, u_elem);
	gt_spin_unlock(runq_lock);
	return;
}

extern void switch_runqueue(runqueue_t *from_runq, gt_spinlock_t *from_runqlock, 
		runqueue_t *to_runq, gt_spinlock_t *to_runqlock, uthread_struct_t *u_elem)
{
	rem_from_runqueue(from_runq, from_runqlock, u_elem);
	add_to_runqueue(to_runq, to_runqlock, u_elem);
	return;
}


/**********************************************************************/

extern void kthread_init_runqueue(kthread_runqueue_t *kthread_runq)
{
	kthread_runq->active_runq = &(kthread_runq->runqueues[0]);
	kthread_runq->expires_runq = &(kthread_runq->runqueues[1]);

	gt_spinlock_init(&(kthread_runq->kthread_runqlock));
	init_runqueue(kthread_runq->active_runq);
	init_runqueue(kthread_runq->expires_runq);

	TAILQ_INIT(&(kthread_runq->zombie_uthreads));
	return;
}

static void print_runq(runqueue_t *runq, char *runq_str)
{
	int i, j;
	uthread_head_t *u_head;
	uthread_struct_t *u_obj;

	printf("******************************************************\n");
	printf("Run queue(%s) state : \n", runq_str);
	printf("Threads in current run queue:\n");
	for (i = 0; i < MAX_UTHREAD_PRIORITY; ++i)
		if (runq->uthread_prio_tot > 0)
			for (j = 0; j < MAX_UTHREAD_GROUPS; ++j)
			{
				u_head = &((runq)->prio_array[i].group[j]);
				TAILQ_FOREACH(u_obj, u_head, uthread_runq)
					printf("Thread(id:%d, group:%d) with priority:%d belongs to this run queue\n", u_obj->uthread_tid, u_obj->uthread_gid, i);
			}
	printf("******************************************************\n");
}

static void print_runq_stats(runqueue_t *runq, char *runq_str)
{
	int inx;
	printf("******************************************************\n");
	printf("Run queue(%s) state : \n", runq_str);
	printf("******************************************************\n");
	printf("uthreads details - (tot:%d , mask:%x)\n", runq->uthread_tot, runq->uthread_mask);
	printf("******************************************************\n");
	printf("uthread priority details : \n");
	for(inx=0; inx<MAX_UTHREAD_PRIORITY; inx++)
		printf("uthread priority (%d) - (tot:%d)\n", inx, runq->uthread_prio_tot[inx]);
	printf("******************************************************\n");
	printf("uthread group details : \n");
	for(inx=0; inx<MAX_UTHREAD_GROUPS; inx++)
		printf("uthread group (%d) - (tot:%d , mask:%x)\n", inx, runq->uthread_group_tot[inx], runq->uthread_group_mask[inx]);
	printf("******************************************************\n");
	return;
}

extern uthread_struct_t *sched_find_best_uthread(kthread_runqueue_t *kthread_runq)
{
	/* [1] Tries to find the highest priority RUNNABLE uthread in active-runq.
	 * [2] Found - Jump to [FOUND]
	 * [3] Switches runqueues (active/expires)
	 * [4] Repeat [1] through [2]
	 * [NOT FOUND] Return NULL(no more jobs)
	 * [FOUND] Remove uthread from pq and return it. */

	runqueue_t *runq;
	prio_struct_t *prioq;
	uthread_head_t *u_head;
	uthread_struct_t *u_obj;
	unsigned int uprio, ugroup;

	gt_spin_lock(&(kthread_runq->kthread_runqlock));

	runq = kthread_runq->active_runq;

	kthread_runq->kthread_runqlock.holder = 0x04;
	if(!(runq->uthread_mask))
	{ /* No jobs in active. switch runqueue */
		assert(!runq->uthread_tot);
		kthread_runq->active_runq = kthread_runq->expires_runq;
		kthread_runq->expires_runq = runq;

		runq = kthread_runq->expires_runq;
		if(!runq->uthread_mask)
		{
			assert(!runq->uthread_tot);
			gt_spin_unlock(&(kthread_runq->kthread_runqlock));
			return NULL;
		}
	}

	/* Find the highest priority bucket */
	uprio = LOWEST_BIT_SET(runq->uthread_mask);
	prioq = &(runq->prio_array[uprio]);

	assert(prioq->group_mask);
	ugroup = LOWEST_BIT_SET(prioq->group_mask);

	u_head = &(prioq->group[ugroup]);
	u_obj = TAILQ_FIRST(u_head);
	__rem_from_runqueue(runq, u_obj);

	gt_spin_unlock(&(kthread_runq->kthread_runqlock));
#if 0
	printf("cpu(%d) : sched best uthread(id:%d, group:%d)\n", u_obj->cpu_id, u_obj->uthread_tid, u_obj->uthread_gid);
#endif
	return(u_obj);
}


extern uthread_struct_t *sched_find_next_uthread(kthread_runqueue_t *kthread_runq)
{
	runqueue_t *runq;
	prio_struct_t  *prioq;
	uthread_head_t *u_head;
	uthread_struct_t *u_obj = NULL;
	unsigned int i, j, uprio, ugroup;

	gt_spin_lock(&(kthread_runq->kthread_runqlock));

	runq = kthread_runq->active_runq;

	if (!(runq->uthread_mask))
	{
		/* No threads in ACTIVE runq. Switch runq and bump credits. */
		assert(!runq->uthread_tot);
		kthread_runq->active_runq = kthread_runq->expires_runq;
		kthread_runq->expires_runq = runq;

		runq = kthread_runq->active_runq;
		if (!runq->uthread_mask)
		{
			/* Both EXPIRES and ACTIVE runq are empty, load balance from another kthread */
			assert(!runq->uthread_tot);
			gt_spin_unlock(&(kthread_runq->kthread_runqlock));

			if (ksched_shared_info.load_balance == 1) {
				/* Load balancing: Implement uthread migration if a kthread is idle. */
				int inx;
				kthread_context_t  *tgt_k_ctx = NULL, *tmp_k_ctx, *k_ctx;

				k_ctx = kthread_cpu_map[kthread_apic_id()];

				for (inx = 0; inx < GT_MAX_KTHREADS; inx++) {
					if ((tmp_k_ctx = kthread_cpu_map[inx]) && (tmp_k_ctx != k_ctx))
						if (tmp_k_ctx->kthread_flags & KTHREAD_DONE)
							continue;
					tgt_k_ctx = tmp_k_ctx;
					break;
				}

				if (tgt_k_ctx) {
					kthread_runqueue_t *tgt_kthread_runq = &(tgt_k_ctx->krunqueue);
					runqueue_t *tgt_runq = tgt_kthread_runq->active_runq;

					gt_spin_lock(&(tgt_kthread_runq->kthread_runqlock));

					if (tgt_runq->uthread_mask) {
						uprio = LOWEST_BIT_SET(tgt_runq->uthread_mask);
						prioq = &(tgt_runq->prio_array[uprio]);

						assert(prioq->group_mask);
						ugroup = LOWEST_BIT_SET(prioq->group_mask);

						u_head = &(prioq->group[ugroup]);
						u_obj = TAILQ_LAST(u_head, uthread_head);

						printf("Migrating Thread(id:%d, group:%d) from another kthread\n", u_obj->uthread_tid, u_obj->uthread_gid);

						printf("Printing target kthread's runq before balancing\n");
						print_runq(tgt_runq, "ACTIVE");

						__rem_from_runqueue(tgt_runq, u_obj);

						printf("Printing target kthread's runq after balancing\n");
						print_runq(tgt_runq, "ACTIVE");
					}

					gt_spin_unlock(&(tgt_kthread_runq->kthread_runqlock));
					return(u_obj);
				}
			}

			return NULL;
		}

		printf("No threads in ACTIVE kernel runq. Bumping credits of threads in EXPIRES runq\n");

		for (i = 0; i < MAX_UTHREAD_PRIORITY; ++i)
		{
			if (runq->uthread_prio_tot > 0)
				for (j = 0; j < MAX_UTHREAD_GROUPS; ++j)
				{
					u_head = &((runq)->prio_array[i].group[j]);
					TAILQ_FOREACH(u_obj, u_head, uthread_runq)
					{
						printf("CREDIT CHANGE: Thread(id:%d, group:%d, weight:%d)'s previous credit:%d\n", u_obj->uthread_tid, u_obj->uthread_gid, u_obj->uthread_weight, u_obj->uthread_credit);

						while (u_obj->uthread_credit < 0)
							u_obj->uthread_credit += u_obj->uthread_weight;

						printf("CREDIT CHANGE: Thread(id:%d, group:%d, weight:%d)'s current credit:%d\n", u_obj->uthread_tid, u_obj->uthread_gid, u_obj->uthread_weight, u_obj->uthread_credit);
					}
				}
		}

	}

	/* Find the highest priority bucket */
	uprio = LOWEST_BIT_SET(runq->uthread_mask);
	prioq = &(runq->prio_array[uprio]);

	assert(prioq->group_mask);
	ugroup = LOWEST_BIT_SET(prioq->group_mask);

	u_head = &(prioq->group[ugroup]);
	u_obj = TAILQ_FIRST(u_head);
	__rem_from_runqueue(runq, u_obj);

	gt_spin_unlock(&(kthread_runq->kthread_runqlock));
	return(u_obj);
}



/* XXX: More work to be done !!! */
extern gt_spinlock_t uthread_group_penalty_lock;
extern unsigned int uthread_group_penalty;

extern uthread_struct_t *sched_find_best_uthread_group(kthread_runqueue_t *kthread_runq)
{
	/* [1] Tries to find a RUNNABLE uthread in active-runq from u_gid.
	 * [2] Found - Jump to [FOUND]
	 * [3] Tries to find a thread from a group with least threads in runq (XXX: NOT DONE)
	 * - [Tries to find the highest priority RUNNABLE thread (XXX: DONE)]
	 * [4] Found - Jump to [FOUND]
	 * [5] Switches runqueues (active/expires)
	 * [6] Repeat [1] through [4]
	 * [NOT FOUND] Return NULL(no more jobs)
	 * [FOUND] Remove uthread from pq and return it. */
	runqueue_t *runq;
	prio_struct_t *prioq;
	uthread_head_t *u_head;
	uthread_struct_t *u_obj;
	unsigned int uprio, ugroup, mask;
	uthread_group_t u_gid;

#ifndef COSCHED
	return sched_find_best_uthread(kthread_runq);
#endif

	/* XXX: Read u_gid from global uthread-select-criterion */
	u_gid = 0;
	runq = kthread_runq->active_runq;

	if(!runq->uthread_mask)
	{ /* No jobs in active. switch runqueue */
		assert(!runq->uthread_tot);
		kthread_runq->active_runq = kthread_runq->expires_runq;
		kthread_runq->expires_runq = runq;

		runq = kthread_runq->expires_runq;
		if(!runq->uthread_mask)
		{
			assert(!runq->uthread_tot);
			return NULL;
		}
	}


	if(!(mask = runq->uthread_group_mask[u_gid]))
	{ /* No uthreads in the desired group */
		assert(!runq->uthread_group_tot[u_gid]);
		return (sched_find_best_uthread(kthread_runq));
	}

	/* Find the highest priority bucket for u_gid */
	uprio = LOWEST_BIT_SET(mask);

	/* Take out a uthread from the bucket. Return it. */
	u_head = &(runq->prio_array[uprio].group[u_gid]);
	u_obj = TAILQ_FIRST(u_head);
	rem_from_runqueue(runq, &(kthread_runq->kthread_runqlock), u_obj);

	return(u_obj);
}

#if 0
/*****************************************************************************************/
/* Main Test Function */

runqueue_t active_runqueue, expires_runqueue;

#define MAX_UTHREADS 1000
uthread_struct_t u_objs[MAX_UTHREADS];

static void fill_runq(runqueue_t *runq)
{
	uthread_struct_t *u_obj;
	int inx;
	/* create and insert */
	for(inx=0; inx<MAX_UTHREADS; inx++)
	{
		u_obj = &u_objs[inx];
		u_obj->uthread_tid = inx;
		u_obj->uthread_gid = (inx % MAX_UTHREAD_GROUPS);
		u_obj->uthread_priority = (inx % MAX_UTHREAD_PRIORITY);
		__add_to_runqueue(runq, u_obj);
		printf("Uthread (id:%d , prio:%d) inserted\n", u_obj->uthread_tid, u_obj->uthread_priority);
	}

	return;
}

static void change_runq(runqueue_t *from_runq, runqueue_t *to_runq)
{
	uthread_struct_t *u_obj;
	int inx;
	/* Remove and delete */
	for(inx=0; inx<MAX_UTHREADS; inx++)
	{
		u_obj = &u_objs[inx];
		switch_runqueue(from_runq, to_runq, u_obj);
		printf("Uthread (id:%d , prio:%d) moved\n", u_obj->uthread_tid, u_obj->uthread_priority);
	}

	return;
}


static void empty_runq(runqueue_t *runq)
{
	uthread_struct_t *u_obj;
	int inx;
	/* Remove and delete */
	for(inx=0; inx<MAX_UTHREADS; inx++)
	{
		u_obj = &u_objs[inx];
		__rem_from_runqueue(runq, u_obj);
		printf("Uthread (id:%d , prio:%d) removed\n", u_obj->uthread_tid, u_obj->uthread_priority);
	}

	return;
}

int main()
{
	runqueue_t *active_runq, *expires_runq;
	uthread_struct_t *u_obj;
	int inx;

	active_runq = &active_runqueue;
	expires_runq = &expires_runqueue;

	init_runqueue(active_runq);
	init_runqueue(expires_runq);

	fill_runq(active_runq);
	print_runq_stats(active_runq, "ACTIVE");
	print_runq_stats(expires_runq, "EXPIRES");
	change_runq(active_runq, expires_runq);
	print_runq_stats(active_runq, "ACTIVE");
	print_runq_stats(expires_runq, "EXPIRES");
	empty_runq(expires_runq);
	print_runq_stats(active_runq, "ACTIVE");
	print_runq_stats(expires_runq, "EXPIRES");

	return 0;
}

#endif
