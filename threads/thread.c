#include "threads/thread.h"
#include <debug.h>
#include <stddef.h>
#include <random.h>
#include <stdio.h>
#include <string.h>
#include "threads/flags.h"
#include "threads/interrupt.h"
#include "threads/intr-stubs.h"
#include "threads/palloc.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "intrinsic.h"
#ifdef USERPROG
#include "userprog/process.h"
#include "filesys/filesys.h"
#include "filesys/file.h"
#endif

/* Random value for struct thread's `magic' member.
   Used to detect stack overflow.  See the big comment at the top
   of thread.h for details. */
#define THREAD_MAGIC 0xcd6abf4b

/* Random value for basic thread
   Do not modify this value. */
#define THREAD_BASIC 0xd42df210

// P1-AS
// 고정 소수점 계산
#define FRAC (1 << 14)
#define TO_REAL(x) ((x) * FRAC)					// from int to 17.14 format
#define TO_INT(x) ( (x) > 0 ? \
					(((x) + FRAC/2) / FRAC) : \
					(((x) - FRAC/2) / FRAC) ) 	// from 17.14 format to int

static int load_avg; // in 17.14 format

/* List of processes in THREAD_READY state, that is, processes
   that are ready to run but not actually running. */
static struct list ready_list;
static struct list sleep_list; // P1-AC
static struct list all_list; // P1-AS

/* Idle thread. */
static struct thread *idle_thread;

/* Initial thread, the thread running init.c:main(). */
static struct thread *initial_thread;

/* Lock used by allocate_tid(). */
static struct lock tid_lock;

/* Thread destruction requests */
static struct list destruction_req;

/* Statistics. */
static long long idle_ticks;    /* # of timer ticks spent idle. */
static long long kernel_ticks;  /* # of timer ticks in kernel threads. */
static long long user_ticks;    /* # of timer ticks in user programs. */

/* Scheduling. */
#define TIME_SLICE 4            /* # of timer ticks to give each thread. */
static unsigned thread_ticks;   /* # of timer ticks since last yield. */

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
bool thread_mlfqs;

// P1-AS
static void update_load_avg(void);
static void update_recent_cpu(struct thread *t);
static void update_recent_cpu_all(void);
static void update_priority(struct thread *t);
static void update_priority_all(void);
static int clamp_priority(int priority);
static int clamp_nice(int nice);

// P2
static bool init_file_table(struct thread *t);
static void migrate_list(struct list *old_l, struct list *new_l);

static void kernel_thread (thread_func *, void *aux);

static void idle (void *aux UNUSED);
static struct thread *next_thread_to_run (void);
static void init_thread (struct thread *, const char *name, int priority);
static void do_schedule(int status);
static void schedule (void);
static tid_t allocate_tid (void);

/* Returns true if T appears to point to a valid thread. */
#define is_thread(t) ((t) != NULL && (t)->magic == THREAD_MAGIC)

/* Returns the running thread.
 * Read the CPU's stack pointer `rsp', and then round that
 * down to the start of a page.  Since `struct thread' is
 * always at the beginning of a page and the stack pointer is
 * somewhere in the middle, this locates the curent thread. */
#define running_thread() ((struct thread *) (pg_round_down (rrsp ())))


// Global descriptor table for the thread_start.
// Because the gdt will be setup after the thread_init, we should
// setup temporal gdt first.
static uint64_t gdt[3] = { 0, 0x00af9a000000ffff, 0x00cf92000000ffff };

// ============================= [MAIN FUNC] ===================================

/* Initializes the threading system by transforming the code
   that's currently running into a thread.  This can't work in
   general and it is possible in this case only because loader.S
   was careful to put the bottom of the stack at a page boundary.

   Also initializes the run queue and the tid lock.

   After calling this function, be sure to initialize the page
   allocator before trying to create any threads with
   thread_create().

   It is not safe to call thread_current() until this function
   finishes. */
void
thread_init (void) {
	ASSERT (intr_get_level () == INTR_OFF);

	/* Reload the temporal gdt for the kernel
	 * This gdt does not include the user context.
	 * The kernel will rebuild the gdt with user context, in gdt_init (). */
	struct desc_ptr gdt_ds = {
		.size = sizeof (gdt) - 1,
		.address = (uint64_t) gdt
	};
	lgdt (&gdt_ds);

	/* Init the globla thread context */
	lock_init (&tid_lock);
	list_init (&ready_list);
	list_init (&destruction_req);

	/* Set up a thread structure for the running thread. */
	initial_thread = running_thread ();
	init_thread (initial_thread, "main", PRI_DEFAULT);
	initial_thread->status = THREAD_RUNNING;
	initial_thread->tid = allocate_tid ();

	list_init(&sleep_list); // P1-AC
	list_init(&all_list); // P1-AS, P2
	list_push_back(&all_list, &initial_thread->elem_2);
	
	if (thread_mlfqs) { // P1-AS
		// main 쓰레드 설정
		initial_thread->nice = 0;
		initial_thread->recent_cpu = 0;
	}

	initial_thread->p_tid = TID_ERROR; // main 쓰레드는 부모가 없음 (P2)
}

/* Starts preemptive thread scheduling by enabling interrupts.
   Also creates the idle thread. */
void
thread_start (void) {
	/* Create the idle thread. */
	struct semaphore idle_started;
	sema_init (&idle_started, 0);
	thread_create ("idle", PRI_MIN, idle, &idle_started);

	/* Start preemptive thread scheduling. */
	intr_enable ();

	/* Wait for the idle thread to initialize idle_thread. */
	sema_down (&idle_started);

	list_remove(&idle_thread->elem_2); // P1-AS, P2
}

/* Called by the timer interrupt handler at each timer tick.
   Thus, this function runs in an external interrupt context. */
void
thread_tick (void) {
	struct thread *t = thread_current ();

	/* Update statistics. */
	if (t == idle_thread)
		idle_ticks++;
#ifdef USERPROG
	else if (t->pml4 != NULL)
		user_ticks++;
#endif
	else
		kernel_ticks++;
	
	if (thread_mlfqs && t != idle_thread) { // P1-AS
		t->recent_cpu += TO_REAL(1); // running thread의 recent_cpu를 증가
	}

	/* Enforce preemption. */
	if (++thread_ticks >= TIME_SLICE) {
		if (thread_mlfqs) // P1-AS
			update_priority_all();
		intr_yield_on_return ();
	}
}

// P1-AS
// 1초에 한 번씩 interrupt에 의해 호출되는 함수
void thread_sec(void) {
	if (!thread_mlfqs)
		return;
	
	enum intr_level old_level = intr_disable();

	update_load_avg();
	update_recent_cpu_all();

	intr_set_level(old_level);
}

/* Prints thread statistics. */
void
thread_print_stats (void) {
	printf ("Thread: %lld idle ticks, %lld kernel ticks, %lld user ticks\n",
			idle_ticks, kernel_ticks, user_ticks);
}

/* Creates a new kernel thread named NAME with the given initial
   PRIORITY, which executes FUNCTION passing AUX as the argument,
   and adds it to the ready queue.  Returns the thread identifier
   for the new thread, or TID_ERROR if creation fails.

   If thread_start() has been called, then the new thread may be
   scheduled before thread_create() returns.  It could even exit
   before thread_create() returns.  Contrariwise, the original
   thread may run for any amount of time before the new thread is
   scheduled.  Use a semaphore or some other form of
   synchronization if you need to ensure ordering.

   The code provided sets the new thread's `priority' member to
   PRIORITY, but no actual priority scheduling is implemented.
   Priority scheduling is the goal of Problem 1-3. */
tid_t
thread_create (const char *name, int priority,
		thread_func *function, void *aux) {
	struct thread *t;
	tid_t tid;

	ASSERT (function != NULL);

	/* Allocate thread. */
	t = palloc_get_page (PAL_ZERO);
	if (t == NULL)
		return TID_ERROR;

	/* Initialize thread. */
	init_thread (t, name, priority);
	tid = t->tid = allocate_tid ();

	// 상속 및 부모-자식 관계 설정
	struct thread *cur_t = thread_current();

	enum intr_level old_level = intr_disable();
	list_push_back(&all_list, &t->elem_2); // all_list에 삽입
	intr_set_level(old_level);

	if (thread_mlfqs) { // P1-AS
		// nice, recent_cpu, priority 상속
		t->nice = cur_t->nice;
		t->recent_cpu = cur_t->recent_cpu;
		t->priority = cur_t->priority;
	}

	// P2
	t->p_tid = cur_t->tid; // 부모 쓰레드 tid 저장
	if (!init_file_table(t)) { // fd table 초기화
		return TID_ERROR;
	}

	/* Call the kernel_thread if it scheduled.
	 * Note) rdi is 1st argument, and rsi is 2nd argument. */
	t->tf.rip = (uintptr_t) kernel_thread;
	t->tf.R.rdi = (uint64_t) function;
	t->tf.R.rsi = (uint64_t) aux;
	t->tf.ds = SEL_KDSEG;
	t->tf.es = SEL_KDSEG;
	t->tf.ss = SEL_KDSEG;
	t->tf.cs = SEL_KCSEG;
	t->tf.eflags = FLAG_IF;

	/* Add to run queue. */
	thread_unblock (t);
	thread_preempt();

	return tid;
}

// ============================= [BLCK FUNC] ===================================

/* Puts the current thread to sleep.  It will not be scheduled
   again until awoken by thread_unblock().

   This function must be called with interrupts turned off.  It
   is usually a better idea to use one of the synchronization
   primitives in synch.h. */
void
thread_block (void) {
	ASSERT (!intr_context ());
	ASSERT (intr_get_level () == INTR_OFF);
	thread_current ()->status = THREAD_BLOCKED;
	schedule ();
}

/* Transitions a blocked thread T to the ready-to-run state.
   This is an error if T is not blocked.  (Use thread_yield() to
   make the running thread ready.)

   This function does not preempt the running thread.  This can
   be important: if the caller had disabled interrupts itself,
   it may expect that it can atomically unblock a thread and
   update other data. */
void
thread_unblock (struct thread *t) {
	enum intr_level old_level;

	ASSERT (is_thread (t));

	old_level = intr_disable ();
	ASSERT (t->status == THREAD_BLOCKED);
	list_push_back (&ready_list, &t->elem);
	t->status = THREAD_READY;
	intr_set_level (old_level);
}

// P1-AC
// 쓰레드를 wake_tick까지 block
void thread_sleep_until(int64_t wake_tick) {
	struct thread *t = thread_current();
	t->wake_tick = wake_tick; // 깨어날 시각

	enum intr_level old_level = intr_disable ();

	// 현재 쓰레드를 wake_tick 오름차순으로 sleep_list에 삽입
	list_insert_ordered(&sleep_list, &t->elem, thread_wake_tick_less, NULL);
	thread_block();

	intr_set_level (old_level);
}

// P1-AC
// sleep_list 리스트에서 깨울 시간이 지난 쓰레드들을 unblock
// 다음으로 깨울 시각을 반환
int64_t thread_wake_sleepers(int64_t cur_tick) {
	if (list_empty(&sleep_list)) {
		return __INT64_MAX__;
	}

	enum intr_level old_level = intr_disable ();

	struct list_elem *e;
	struct thread *t;

	for (e = list_begin(&sleep_list);
		 e != list_end(&sleep_list); e = list_next(e)) {
		t = list_entry(e, struct thread, elem);
		// sleep_list는 wake_tick 오름차순으로 정렬되어있음
		if (cur_tick >= t->wake_tick) {
			// 깨어날 시각이 지났으면 깨우기
			e = list_remove(e);
			e = e->prev; // thread_unblock을 하면 e의 next/prev가 바뀌므로
			thread_unblock(t);
		} else {
			break; // 나머지는 시간이 남았으므로 스킵
		}
	}

	int64_t ret;
	// 다음으로 쓰레드를 깨울 시각을 timer_sleep()에게 전달
	if (list_empty(&sleep_list)) {
		ret = __INT64_MAX__;
	} else {
		ret = list_entry(list_begin(&sleep_list),
						 struct thread, elem)->wake_tick;
	}

	intr_set_level (old_level);

	return ret;
}

// ============================= [INFO FUNC] ===================================

/* Returns the name of the running thread. */
const char *
thread_name (void) {
	return thread_current ()->name;
}

/* Returns the running thread.
   This is running_thread() plus a couple of sanity checks.
   See the big comment at the top of thread.h for details. */
struct thread *
thread_current (void) {
	struct thread *t = running_thread ();

	/* Make sure T is really a thread.
	   If either of these assertions fire, then your thread may
	   have overflowed its stack.  Each thread has less than 4 kB
	   of stack, so a few big automatic arrays or moderate
	   recursion can cause stack overflow. */
	ASSERT (is_thread (t));
	ASSERT (t->status == THREAD_RUNNING);

	return t;
}

/* Returns the running thread's tid. */
// tid_t
// thread_tid (void) {
// 	return thread_current ()->tid;
// }

// /* Deschedules the current thread and destroys it.  Never
//    returns to the caller. */
// void
// thread_exit (void) {
// 	ASSERT (!intr_context ());

// #ifdef USERPROG
// 	process_exit ();
// #endif

// 	/* Just set our status to dying and schedule another process.
// 	   We will be destroyed during the call to schedule_tail(). */
// 	intr_disable ();
// 	do_schedule (THREAD_DYING);
// 	NOT_REACHED ();
// }
void
thread_exit (void) {
	ASSERT (!intr_context ());

	tid_t tid = thread_current()->tid;

#ifdef USERPROG

	process_exit (); // P2

#endif

	/* Just set our status to dying and schedule another process.
	   We will be destroyed during the call to schedule_tail(). */
	intr_disable ();
	
	list_remove(&thread_current()->elem_2);

	// P2
	// 모든 자식 쓰레드를 reap
	struct thread *t;
	struct list_elem *e;

	for (e = list_begin(&all_list); e != list_end(&all_list); e = list_next(e)) {
		t = list_entry(e, struct thread, elem_2);
		ASSERT(is_thread(t));
		if (t->p_tid == tid) {
			// 자식 쓰레드를 발견, reap
			sema_up(&t->reap_sema);
		}
	}

#ifdef USERPROG


#endif

	do_schedule (THREAD_DYING);
	NOT_REACHED ();
}

/* Yields the CPU.  The current thread is not put to sleep and
   may be scheduled again immediately at the scheduler's whim. */
void
thread_yield (void) {
	struct thread *curr = thread_current ();
	enum intr_level old_level;

	ASSERT (!intr_context ());

	old_level = intr_disable ();
	if (curr != idle_thread)
		list_push_back (&ready_list, &curr->elem);
	do_schedule (THREAD_READY);
	intr_set_level (old_level);
}

// P2
// ready_list의 최대 priority가 더 높으면 yield
void thread_preempt(void) {
	if (intr_context()) {
		// 외부 interrupt일 때는 yield 금지 (P3)
		return;
	}
	struct thread *curr = thread_current();
	bool need_yield = false;

	enum intr_level old_level = intr_disable();

	if (!list_empty(&ready_list)) {
		struct thread *next = list_entry(list_max(&ready_list,
												  thread_priority_less, NULL),
										 struct thread, elem);
		if (next->priority > curr->priority) {
			need_yield = true;
		}
	}

	intr_set_level(old_level);
	if (need_yield)
		thread_yield();
}

// ============================= [PRI FUNC] ====================================

// P1-PS
// priority를 변경 및 donate받은 후 필요 시 yield
void
thread_set_priority (int new_priority) {
	if (thread_mlfqs) { // P1-AS
		return;
	}

	struct thread *t = thread_current();
	int old_priority = t->priority;

	t->ori_priority = new_priority;
	thread_recalculate_donate(t); // 현재 lock_list로부터 priority 계산

	if (t->priority < old_priority) { // priority가 감소하면 양보
		thread_yield();
	}
}

/* Returns the current thread's priority. */
int
thread_get_priority (void) {
	return thread_current ()->priority;
}

// P1-PS
// donor가 donee에게 priority를 donate
void thread_donate_priority(struct thread *donor, struct thread *donee) {
	if (thread_mlfqs)
		return;
	
	donor->donee_t = donee;

	if (donor->priority > donee->priority) {
		// donate로 인해 priority가 상승함
		ASSERT(donee->status != THREAD_RUNNING);

		donee->priority = donor->priority; // priority 수정

		if (donee->donee_t) {
			// donee가 다른 lock에서 대기중인 경우 재귀 업데이트
			thread_donate_priority(donee, donee->donee_t);
		}
	}
}

// P1-PS
// t가 lock을 release했을 때 호출
// 쓰레드 t의 lock_list로부터 t의 새로운 priority를 계산
void thread_recalculate_donate(struct thread *t) {
	if (thread_mlfqs)
		return;
	
	int new_priority = t->ori_priority;

	struct list_elem *iter_e;
	struct lock *iter_l;
	struct thread *iter_t;

	// lock_list의 lock에서 최대 priority 추출
	for (iter_e = list_begin(&t->lock_list); iter_e != list_end(&t->lock_list);
							 iter_e = list_next(iter_e)) {
		iter_l = list_entry(iter_e, struct lock, elem);
		if (!list_empty(&iter_l->semaphore.waiters)) {
			iter_t = list_entry(list_max(&iter_l->semaphore.waiters,
										 thread_priority_less, NULL),
								struct thread, elem);
			if (iter_t->priority > new_priority) {
				new_priority = iter_t->priority;
			}
		}
	}
	t->priority = new_priority;
}

// ============================= [MLFQ FUNC] ===================================

// P1-AS
/* Sets the current thread's nice value to NICE. */
void
thread_set_nice (int nice) {
	thread_current()->nice = clamp_nice(nice);
}

/* Returns the current thread's nice value. */
int
thread_get_nice (void) {
	return thread_current()->nice;
}

/* Returns 100 times the system load average. */
int
thread_get_load_avg (void) {
	return TO_INT(100 * load_avg);
}

/* Returns 100 times the current thread's recent_cpu value. */
int
thread_get_recent_cpu (void) {
	return TO_INT(100 * thread_current()->recent_cpu);
}

// CMP FNC
// thread안의 elem에 대해 wake_tick을 비교 (P1-AC)
bool thread_wake_tick_less(const struct list_elem *a,
	const struct list_elem *b, void *aux UNUSED) {
	struct thread *ta = list_entry(a, struct thread, elem);
	struct thread *tb = list_entry(b, struct thread, elem);

	return ta->wake_tick < tb->wake_tick;
}

// thread안의 elem에 대해 priority를 비교 (P1-AS)
bool thread_priority_less(const struct list_elem *a,
	const struct list_elem *b, void *aux UNUSED) {
	struct thread *ta = list_entry(a, struct thread, elem);
	struct thread *tb = list_entry(b, struct thread, elem);

	return ta->priority < tb->priority;
}

// ============================= [PRCS FUNC] ===================================

// P2
// tid인 쓰레드를 반환
struct thread *thread_get_by_id(tid_t tid) {
	if (tid == TID_ERROR) {
		return NULL;
	}

	struct thread *t;
	struct list_elem *e;

	enum intr_level old_level = intr_disable();

	for (e = list_begin(&all_list); e != list_end(&all_list); e = list_next(e)) {
		t = list_entry(e, struct thread, elem_2);
		ASSERT(is_thread(t));
		if (t->tid == tid) {
			break;
		}
	}

	intr_set_level(old_level);

	if (e == list_end(&all_list)) {
		// child_tid 탐색 실패
		return NULL;
	}

	return t;
}

// P2
// fork시에 fd_page_list, file_list, fd_list를 복제
bool thread_dup_file_list(struct thread *old_t, struct thread *new_t) {
	// file_elem, fd_elem을 할당한 페이지들을 모두 복사한다.
	// 복사 후, 복사본 리스트의 next, prev 주소 업데이트가 필요하므로,
	// 원본 file_elem, fd_elem의 복사본 주소를 원본 구조체의 migrate에 저장,
	// 그 후 리스트를 순회하며 next, prev를 업데이트한다.

	// 1. 원본 fd_page_list를 하나씩 복사
	struct list *old_pl = &old_t->fd_page_list;
	struct list *new_pl = &new_t->fd_page_list;
	struct list *old_fl = &old_t->file_list;
	struct list *new_fl = &new_t->file_list;
	struct list *old_fdl = &old_t->fd_list;
	struct list *new_fdl = &new_t->fd_list;

	struct file_elem *first_pg = list_entry(list_begin(new_pl),
											struct file_elem, elem);
	palloc_free_page(first_pg); // init_file_table()에서 생성한 페이지를 반환
	
	list_init(new_pl);
	list_init(new_fl);
	list_init(new_fdl);

	struct list_elem *e, *e2;
	struct file_elem *old_pg, *new_pg;

	struct file_elem *fe;
	struct fd_elem *fde;

	uint64_t old_pg_no, new_pg_no;

	for (e = list_begin(old_pl); e != list_end(old_pl); e = list_next(e)) {
		old_pg = list_entry(e, struct file_elem, elem);
		new_pg = palloc_get_page(PAL_ZERO);

		if (new_pg == NULL) {
			// 복사 중 오류 발생 (새로운 페이지 할당 불가)
			for (e2 = list_begin(old_pl); e2 != e; e2 = list_next(e2)) {
				// 지금까지 할당한 페이지를 다시 반환
				old_pg = list_entry(e2, struct file_elem, elem);
				ASSERT(old_pg->migrate != NULL);
				palloc_free_page(old_pg->migrate);
			}
			return false;
		}

		memcpy(new_pg, old_pg, PGSIZE);

		// 복사본 file_elem, fd_elem의 주소를 migrate에 저장
		old_pg_no = pg_no(old_pg);
		new_pg_no = pg_no(new_pg);

		// fd_page_list migrate 계산
		old_pg->migrate =
			(struct file_elem*) ( (new_pg_no << PGBITS) | pg_ofs(old_pg) );

		// file_list migrate 계산
		for (e2 = list_begin(old_fl); e2 != list_end(old_fl); e2 = list_next(e2)) {
			fe = list_entry(e2, struct file_elem, elem);
			if (pg_no(fe) == old_pg_no) {
				fe->migrate =
					(struct file_elem*) ( (new_pg_no << PGBITS) | pg_ofs(fe) );
			}
		}

		// fd_list migrate 계산
		for (e2 = list_begin(old_fdl); e2 != list_end(old_fdl); e2 = list_next(e2)) {
			fde = list_entry(e2, struct fd_elem, elem);
			if (pg_no(fde) == old_pg_no) {
				fde->migrate =
					(struct file_elem*) ( (new_pg_no << PGBITS) | pg_ofs(fde) );
			}
		}
	}

	// 2. 리스트 내의 next, prev를 복사된 file_elem, fd_elem의 주소로 업데이트
	migrate_list(old_pl, new_pl); // fd_page_list

	migrate_list(old_fl, new_fl); // file_list
	// file_elem 안의 file을 복사
	for (e = list_begin(new_fl); e != list_end(new_fl); e = list_next(e)) {
		fe = list_entry(e, struct file_elem, elem);
		if (fe->file) {
			fe->file = file_duplicate(fe->file);
		}
	}

	migrate_list(old_fdl, new_fdl); // fd_list
	// fde->fe 또한 업데이트
	for (e = list_begin(new_fdl); e != list_end(new_fdl); e = list_next(e)) {
		fde = list_entry(e, struct fd_elem, elem);
		if (fde->fe) {
			fde->fe = fde->fe->migrate;
		}
	}

	return true;
}

// P2
// 프로세스 종료 전에 fd_page_list를 모두 free
void thread_clear_fd_page_list(struct thread *t) {
	struct list_elem *e;
	struct file_elem *fe;
	
	while (!list_empty(&t->file_list)) {
		// file_list의 파일을 모두 닫기
		e = list_pop_front(&t->file_list);
		fe = list_entry(e, struct file_elem, elem);
		file_close(fe->file);
	}

	while (!list_empty(&t->fd_page_list)) {
		// fd_page_list 모두 free
		e = list_pop_front(&t->fd_page_list);
		fe = list_entry(e, struct file_elem, elem);
		palloc_free_page(fe);
	}
}

// P2
// child_tid를 가진 쓰레드가 exit할 때까지 대기
int thread_wait(tid_t child_tid) {
	struct thread *t;
	struct list_elem *e;

	enum intr_level old_level = intr_disable();

	for (e = list_begin(&all_list); e != list_end(&all_list); e = list_next(e)) {
		t = list_entry(e, struct thread, elem_2);
		ASSERT(is_thread(t));
		if (t->tid == child_tid) {
			break;
		}
	}

	intr_set_level(old_level);

	if (e == list_end(&all_list)) {
		// child_tid 탐색 실패
		return -1;
	}

	if (t->p_tid != thread_current()->tid) {
		// child_tid 쓰레드가 자식 프로세스가 아님
		return -1;
	}

	sema_down(&t->wait_sema); // 자식이 끝날 때까지 대기
	int child_exit = t->exit_status;
	sema_up(&t->reap_sema); // 자식 프로세스의 메모리 청소를 허용

	return child_exit;
}

////////////////////////////////////////////////////////////////////////////////
//                                {STATICS}                                   //
////////////////////////////////////////////////////////////////////////////////

// ============================= [MLFQ FUNC] ===================================
static void update_load_avg(void) {
	ASSERT(thread_mlfqs);
	// load_avg = (59/60) * load_avg + (1/60) * ready_threads
	int ready_cnt = list_size(&ready_list);
	if (thread_current() != idle_thread) {
		ready_cnt++; // 현재 쓰레드도 센다
	}
	load_avg = ( 59 * load_avg + TO_REAL(ready_cnt) ) / 60;
}

static void update_recent_cpu(struct thread *t) {
	ASSERT(thread_mlfqs);
	// recent_cpu = (2 * load_avg)/(2 * load_avg + 1) * recent_cpu + nice
	// = 2 * ( recent_cpu * load_avg / (2 * load_avg + 1) ) + nice
	// 실수 곱하기 후 실수 나누기를 하므로 f (1 << 14)를 곱하거나 나눌 필요 없음
	t->recent_cpu = 2 * ( (int64_t) t->recent_cpu * load_avg /
					(2 * load_avg + TO_REAL(1)) ) +
					TO_REAL(t->nice);
}

static void update_recent_cpu_all(void) {
	ASSERT(thread_mlfqs);

	struct list_elem *e;
	struct thread *t;

	for (e = list_begin(&all_list); e != list_end(&all_list); e = list_next(e)) {
		t = list_entry(e, struct thread, elem_2);
		update_recent_cpu(t);
	}
}

static void update_priority(struct thread *t) {
	ASSERT(thread_mlfqs);

	// priority = PRI_MAX - (recent_cpu / 4) - (nice * 2),
	t->priority = PRI_MAX - TO_INT(t->recent_cpu / 4) - t->nice * 2;
	t->priority = clamp_priority(t->priority);
}

static void update_priority_all(void) {
	ASSERT(thread_mlfqs);
	
	struct list_elem *e;
	struct thread *t;
	for (e = list_begin(&all_list);
		 e != list_end(&all_list); e = list_next(e)) {
		t = list_entry(e, struct thread, elem_2);
		update_priority(t);
	}
}

static int clamp_priority(int priority) {
	if (priority < PRI_MIN)
		priority = PRI_MIN;
	else if (priority > PRI_MAX)
		priority = PRI_MAX;
	return priority;
}

static int clamp_nice(int nice) {
	if (nice < NICE_MIN)
		nice = NICE_MIN;
	else if (nice > NICE_MAX)
		nice = NICE_MAX;
	return nice;
}

// ============================= [PRCS FUNC] ===================================

// P2
// 쓰레드 구조체 내의 file_list를 초기화하고 stdin, stdout을 추가
static bool init_file_table(struct thread *t) {
	list_init(&t->fd_page_list); // file_elem, fd_elem이 저장될 페이지 리스트
	list_init(&t->file_list); // file 구조체를 저장하는 리스트
	list_init(&t->fd_list); // fd를 저장하는 리스트

	// 첫 페이지 할당
	// fd_elem으로 선언했지만 fd_page_list에 저장하는 용도로만 사용됨
	struct fd_elem *first_pg = palloc_get_page(PAL_ZERO);
	if (first_pg == NULL) {
		return false;
	}
	list_push_front(&t->fd_page_list, &first_pg->elem);

	// stdin, stdout에 해당하는 fd_elem 생성
	struct fd_elem *stdin_fde = first_pg +1;
	struct fd_elem *stdout_fde = first_pg +2;

	stdin_fde->fe = NULL;
	stdin_fde->fd = STDIN_FILENO;
	stdin_fde->std_no = STDIN_FILENO;
	list_push_back(&t->fd_list, &stdin_fde->elem);

	stdout_fde->fe = NULL;
	stdout_fde->fd = STDOUT_FILENO;
	stdout_fde->std_no = STDOUT_FILENO;
	list_push_back(&t->fd_list, &stdout_fde->elem);

	return true;
}

// P2
// old_l, new_l를 walk하며 next, prev 주소를 복사본 주소로 업데이트
// fd_page_list, file_list, fd_list에서 사용 가능
static void migrate_list(struct list *old_l, struct list *new_l) {
	struct list_elem *oe, *ne;
	struct file_elem *fe;

	oe = list_begin(old_l);
	ne = list_head(new_l);
	while (oe != list_tail(old_l)) { // next가 tail이 아닌 동안 반복
		fe = list_entry(oe, struct file_elem, elem);

		ne->next = &fe->migrate->elem;
		ne->next->prev = ne;

		ne = list_next(ne);
		oe = list_next(oe);
	}
	ne->next = list_tail(new_l);
	list_tail(new_l)->prev = ne;
}

// ============================= [MISC FUNC] ===================================

/* Idle thread.  Executes when no other thread is ready to run.

   The idle thread is initially put on the ready list by
   thread_start().  It will be scheduled once initially, at which
   point it initializes idle_thread, "up"s the semaphore passed
   to it to enable thread_start() to continue, and immediately
   blocks.  After that, the idle thread never appears in the
   ready list.  It is returned by next_thread_to_run() as a
   special case when the ready list is empty. */
static void
idle (void *idle_started_ UNUSED) {
	struct semaphore *idle_started = idle_started_;

	idle_thread = thread_current ();
	sema_up (idle_started);

	for (;;) {
		/* Let someone else run. */
		intr_disable ();
		thread_block ();

		/* Re-enable interrupts and wait for the next one.

		   The `sti' instruction disables interrupts until the
		   completion of the next instruction, so these two
		   instructions are executed atomically.  This atomicity is
		   important; otherwise, an interrupt could be handled
		   between re-enabling interrupts and waiting for the next
		   one to occur, wasting as much as one clock tick worth of
		   time.

		   See [IA32-v2a] "HLT", [IA32-v2b] "STI", and [IA32-v3a]
		   7.11.1 "HLT Instruction". */
		asm volatile ("sti; hlt" : : : "memory");
	}
}

/* Function used as the basis for a kernel thread. */
static void
kernel_thread (thread_func *function, void *aux) {
	ASSERT (function != NULL);

	intr_enable ();       /* The scheduler runs with interrupts off. */
	function (aux);       /* Execute the thread function. */
	thread_exit ();       /* If function() returns, kill the thread. */
}


/* Does basic initialization of T as a blocked thread named
   NAME. */
static void
init_thread (struct thread *t, const char *name, int priority) {
	ASSERT (t != NULL);
	ASSERT (PRI_MIN <= priority && priority <= PRI_MAX);
	ASSERT (name != NULL);

	memset (t, 0, sizeof *t);
	t->status = THREAD_BLOCKED;
	strlcpy (t->name, name, sizeof t->name);
	t->tf.rsp = (uint64_t) t + PGSIZE - sizeof (void *);
	t->priority = priority;
	t->ori_priority = priority; // P1-PS
	list_init(&t->lock_list); // P1-PS
	t->donee_t = NULL; // P1-PS
	t->wake_tick = __INT64_MAX__; // P1-AC

	// P2
	sema_init(&t->wait_sema, 0);
	sema_init(&t->reap_sema, 0);

	t->is_user = false; // user process 여부 저장 (P2)
	
	t->magic = THREAD_MAGIC;
}

/* Chooses and returns the next thread to be scheduled.  Should
   return a thread from the run queue, unless the run queue is
   empty.  (If the running thread can continue running, then it
   will be in the run queue.)  If the run queue is empty, return
   idle_thread. */
static struct thread *
next_thread_to_run (void) {
	if (list_empty (&ready_list))
		return idle_thread;
	
	// priority가 최대인 쓰레드를 반환
	struct list_elem *e = list_max(&ready_list, thread_priority_less, NULL);
	list_remove(e);
	return list_entry(e, struct thread, elem);
}

/* Use iretq to launch the thread */
void
do_iret (struct intr_frame *tf) {
	__asm __volatile(
			"movq %0, %%rsp\n"
			"movq 0(%%rsp),%%r15\n"
			"movq 8(%%rsp),%%r14\n"
			"movq 16(%%rsp),%%r13\n"
			"movq 24(%%rsp),%%r12\n"
			"movq 32(%%rsp),%%r11\n"
			"movq 40(%%rsp),%%r10\n"
			"movq 48(%%rsp),%%r9\n"
			"movq 56(%%rsp),%%r8\n"
			"movq 64(%%rsp),%%rsi\n"
			"movq 72(%%rsp),%%rdi\n"
			"movq 80(%%rsp),%%rbp\n"
			"movq 88(%%rsp),%%rdx\n"
			"movq 96(%%rsp),%%rcx\n"
			"movq 104(%%rsp),%%rbx\n"
			"movq 112(%%rsp),%%rax\n"
			"addq $120,%%rsp\n"
			"movw 8(%%rsp),%%ds\n"
			"movw (%%rsp),%%es\n"
			"addq $32, %%rsp\n"
			"iretq"
			: : "g" ((uint64_t) tf) : "memory");
}

/* Switching the thread by activating the new thread's page
   tables, and, if the previous thread is dying, destroying it.

   At this function's invocation, we just switched from thread
   PREV, the new thread is already running, and interrupts are
   still disabled.

   It's not safe to call printf() until the thread switch is
   complete.  In practice that means that printf()s should be
   added at the end of the function. */
static void
thread_launch (struct thread *th) {
	uint64_t tf_cur = (uint64_t) &running_thread ()->tf;
	uint64_t tf = (uint64_t) &th->tf;
	ASSERT (intr_get_level () == INTR_OFF);

	/* The main switching logic.
	 * We first restore the whole execution context into the intr_frame
	 * and then switching to the next thread by calling do_iret.
	 * Note that, we SHOULD NOT use any stack from here
	 * until switching is done. */
	__asm __volatile (
			/* Store registers that will be used. */
			"push %%rax\n"
			"push %%rbx\n"
			"push %%rcx\n"
			/* Fetch input once */
			"movq %0, %%rax\n"
			"movq %1, %%rcx\n"
			"movq %%r15, 0(%%rax)\n"
			"movq %%r14, 8(%%rax)\n"
			"movq %%r13, 16(%%rax)\n"
			"movq %%r12, 24(%%rax)\n"
			"movq %%r11, 32(%%rax)\n"
			"movq %%r10, 40(%%rax)\n"
			"movq %%r9, 48(%%rax)\n"
			"movq %%r8, 56(%%rax)\n"
			"movq %%rsi, 64(%%rax)\n"
			"movq %%rdi, 72(%%rax)\n"
			"movq %%rbp, 80(%%rax)\n"
			"movq %%rdx, 88(%%rax)\n"
			"pop %%rbx\n"              // Saved rcx
			"movq %%rbx, 96(%%rax)\n"
			"pop %%rbx\n"              // Saved rbx
			"movq %%rbx, 104(%%rax)\n"
			"pop %%rbx\n"              // Saved rax
			"movq %%rbx, 112(%%rax)\n"
			"addq $120, %%rax\n"
			"movw %%es, (%%rax)\n"
			"movw %%ds, 8(%%rax)\n"
			"addq $32, %%rax\n"
			"call __next\n"         // read the current rip.
			"__next:\n"
			"pop %%rbx\n"
			"addq $(out_iret -  __next), %%rbx\n"
			"movq %%rbx, 0(%%rax)\n" // rip
			"movw %%cs, 8(%%rax)\n"  // cs
			"pushfq\n"
			"popq %%rbx\n"
			"mov %%rbx, 16(%%rax)\n" // eflags
			"mov %%rsp, 24(%%rax)\n" // rsp
			"movw %%ss, 32(%%rax)\n"
			"mov %%rcx, %%rdi\n"
			"call do_iret\n"
			"out_iret:\n"
			: : "g"(tf_cur), "g" (tf) : "memory"
			);
}

/* Schedules a new process. At entry, interrupts must be off.
 * This function modify current thread's status to status and then
 * finds another thread to run and switches to it.
 * It's not safe to call printf() in the schedule(). */
static void
do_schedule(int status) {
	ASSERT (intr_get_level () == INTR_OFF);
	ASSERT (thread_current()->status == THREAD_RUNNING);
	while (!list_empty (&destruction_req)) {
		struct thread *victim =
			list_entry (list_pop_front (&destruction_req), struct thread, elem);
		palloc_free_page(victim);
	}
	thread_current ()->status = status;
	schedule ();
}

static void
schedule (void) {
	struct thread *curr = running_thread ();
	struct thread *next = next_thread_to_run ();

	ASSERT (intr_get_level () == INTR_OFF);
	ASSERT (curr->status != THREAD_RUNNING);
	ASSERT (is_thread (next));
	/* Mark us as running. */
	next->status = THREAD_RUNNING;

	/* Start new time slice. */
	thread_ticks = 0;

#ifdef USERPROG
	/* Activate the new address space. */
	process_activate (next);
#endif

	if (curr != next) {
		/* If the thread we switched from is dying, destroy its struct
		   thread. This must happen late so that thread_exit() doesn't
		   pull out the rug under itself.
		   We just queuing the page free reqeust here because the page is
		   currently used by the stack.
		   The real destruction logic will be called at the beginning of the
		   schedule(). */
		if (curr && curr->status == THREAD_DYING && curr != initial_thread) {
			ASSERT (curr != next);
			list_push_back (&destruction_req, &curr->elem);
		}

		/* Before switching the thread, we first save the information
		 * of current running. */
		thread_launch (next);
	}
}

/* Returns a tid to use for a new thread. */
static tid_t
allocate_tid (void) {
	static tid_t next_tid = 1;
	tid_t tid;

	lock_acquire (&tid_lock);
	tid = next_tid++;
	lock_release (&tid_lock);

	return tid;
}
