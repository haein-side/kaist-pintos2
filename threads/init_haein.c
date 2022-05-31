#include "threads/init.h"
#include <console.h>
#include <debug.h>
#include <limits.h>
#include <random.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "devices/kbd.h"
#include "devices/input.h"
#include "devices/serial.h"
#include "devices/timer.h"
#include "devices/vga.h"
#include "threads/interrupt.h"
#include "threads/io.h"
#include "threads/loader.h"
#include "threads/malloc.h"
#include "threads/mmu.h"
#include "threads/palloc.h"
#include "threads/pte.h"
#include "threads/thread.h"
#ifdef USERPROG
#include "userprog/process.h"
#include "userprog/exception.h"
#include "userprog/gdt.h"
#include "userprog/syscall.h"
#include "userprog/tss.h"
#endif
#include "tests/threads/tests.h"
#ifdef VM
#include "vm/vm.h"
#endif
#ifdef FILESYS
#include "devices/disk.h"
#include "filesys/filesys.h"
#include "filesys/fsutil.h"
#endif

/* Page-map-level-4 with kernel mappings only. */
uint64_t *base_pml4;

#ifdef FILESYS
/* -f: Format the file system? */
static bool format_filesys;
#endif

/* -q: Power off after kernel tasks complete? */
bool power_off_when_done;

bool thread_tests;

static void bss_init (void);
static void paging_init (uint64_t mem_end);

static char **read_command_line (void);
static char **parse_options (char **argv);
static void run_actions (char **argv);
static void usage (void);

static void print_stats (void);


int main (void) NO_RETURN;

/* Pintos main program. */
/* Pintos를 실행하면 실행되는 메인 프로세스 */
/* 커맨드 라인을 parsing한 다음, run_actions( ) 함수를 실행 */
int
main (void) {
	uint64_t mem_end;
	char **argv;

	/* Clear BSS and get machine's RAM size. */
	// .bss에는 초기화되지 않은 C전역변수와 정적변수 들어감
	// 0으로 초기화된 전역변수 및 정적변수 저장됨
	bss_init ();

	/* Break command line into arguments and parse options. */
	/* command line: pintos –v -- run ‘echo x’ */
	argv = read_command_line (); // 인자 문자열의 주소가 담긴 포인터 배열을 리턴 [0x13424, 0x23545, 0x44521]
								 // argv = ["pintos", "-q", "run", "'echo x'", NULL]
	argv = parse_options (argv); // argv를 parsing하고 추가적인 option들을 세팅한다.
  								 // "run"등의 action부터 argv에 다시 넣는다.
								 // argv = ["run", "'echo x'", NULL]

	/* Initialize ourselves as a thread so we can use locks,
	   then enable console locking. */
	thread_init ();
	console_init ();

	/* Initialize memory system. */
	mem_end = palloc_init ();
	malloc_init ();
	paging_init (mem_end);

#ifdef USERPROG
	tss_init ();
	gdt_init ();
#endif

	/* Initialize interrupt handlers. */
	intr_init ();
	timer_init ();
	kbd_init ();
	input_init ();
#ifdef USERPROG
	exception_init ();
	syscall_init ();
#endif
	/* Start thread scheduler and enable interrupts. */
	thread_start ();
	serial_init_queue ();
	timer_calibrate ();

#ifdef FILESYS
	/* Initialize file system. */
	disk_init ();
	filesys_init (format_filesys);
#endif

#ifdef VM
	vm_init ();
#endif

	printf ("Boot complete.\n");

	/* Run actions specified on kernel command line. */
	run_actions (argv); // 인자(argv)를 기준으로 run_actions() 실행

	/* Finish up. */
	if (power_off_when_done)
		power_off ();
	thread_exit ();
}

/* Clear BSS */
static void
bss_init (void) {
	/* The "BSS" is a segment that should be initialized to zeros.
	   It isn't actually stored on disk or zeroed by the kernel
	   loader, so we have to zero it ourselves.
	   초기화되지 않은 변수들은 목적파일에서 실제 디스크 공간을 차지할 필요가 없으므로 위치만 표시하게 되며,
	   런타임에 이 변수들은 메모리에 0으로 초기화되어 할당됨 (148라인)
	   The start and end of the BSS segment is recorded by the
	   linker as _start_bss and _end_bss.  See kernel.lds. */
	extern char _start_bss, _end_bss;
	memset (&_start_bss, 0, &_end_bss - &_start_bss); // 메모리의 내용(값)을 원하는 크기만큼 특정 값으로 세팅. 런타임에 메모리에 0으로 초기화되어 할당된다는 것.
}

/* Populates the page table with the kernel virtual mapping,
 * and then sets up the CPU to use the new page directory.
 * Points base_pml4 to the pml4 it creates. */
static void
paging_init (uint64_t mem_end) {
	uint64_t *pml4, *pte;
	int perm;
	pml4 = base_pml4 = palloc_get_page (PAL_ASSERT | PAL_ZERO);

	extern char start, _end_kernel_text;
	// Maps physical address [0 ~ mem_end] to
	//   [LOADER_KERN_BASE ~ LOADER_KERN_BASE + mem_end].
	for (uint64_t pa = 0; pa < mem_end; pa += PGSIZE) {
		uint64_t va = (uint64_t) ptov(pa);

		perm = PTE_P | PTE_W;
		if ((uint64_t) &start <= va && va < (uint64_t) &_end_kernel_text)
			perm &= ~PTE_W;

		if ((pte = pml4e_walk (pml4, va, 1)) != NULL)
			*pte = pa | perm;
	}

	// reload cr3
	pml4_activate(0);
}

/* Breaks the kernel command line into words and returns them as
   an argv-like array. */
/* argv = ["pintos", "-q", "run", "'echo x'", NULL] */
static char **
read_command_line (void) {
	static char* argv[LOADER_ARGS_LEN / 2 + 1]; // 뒤에 NULL을 위해 64 + 1
	char *p, *end;
	int argc;
	int i;

	argc = *(uint32_t *) ptov (LOADER_ARG_CNT); // 인자의 개수를 가진 물리주소 -> 가상메모리 주소로 
	p = ptov (LOADER_ARGS); // 명령어 시작 위치
	end = p + LOADER_ARGS_LEN; 
	for (i = 0; i < argc; i++) {
		if (p >= end)
			PANIC ("command line arguments overflow");

		argv[i] = p;
		p += strnlen (p, end - p) + 1; // a b c d NULL: \0 만날 때까지 탐색
	}
	argv[argc] = NULL;

	/* Print kernel command line. */
	printf ("Kernel command line:");
	for (i = 0; i < argc; i++)
		if (strchr (argv[i], ' ') == NULL)  // 검색대상 문자열, 존재하는지 확인할 문자(아스키값으로): 존재하지 않으면
			printf (" %s", argv[i]);
		else
			printf (" '%s'", argv[i]);
	printf ("\n");

	return argv; // 인자 문자열의 주소가 담긴 포인터 배열을 리턴 [0x13424, 0x23545, 0x44521]
}

/* Parses options in ARGV[]
   and returns the first non-option argument. */
static char **
parse_options (char **argv) {
	for (; *argv != NULL && **argv == '-'; argv++) {
		char *save_ptr;
		char *name = strtok_r (*argv, "=", &save_ptr);
		char *value = strtok_r (NULL, "", &save_ptr);

		if (!strcmp (name, "-h"))
			usage ();
		else if (!strcmp (name, "-q"))
			power_off_when_done = true;
#ifdef FILESYS
		else if (!strcmp (name, "-f"))
			format_filesys = true;
#endif
		else if (!strcmp (name, "-rs"))
			random_init (atoi (value));
		else if (!strcmp (name, "-mlfqs"))
			thread_mlfqs = true;
#ifdef USERPROG
		else if (!strcmp (name, "-ul"))
			user_page_limit = atoi (value);
		else if (!strcmp (name, "-threads-tests"))
			thread_tests = true;
#endif
		else
			PANIC ("unknown option `%s' (use -h for help)", name);
	}

	return argv;
}

/* Runs the task specified in ARGV[1]. */
static void
run_task (char **argv) { // run_task(["run", "'echo x'", NULL])
	const char *task = argv[1]; // task = "echo x"

	printf ("Executing '%s':\n", task);
#ifdef USERPROG
	if (thread_tests){ // parse_options에서 name이 -threads-tests일 경우, thread_tests가 true로 설정되어 있음
		run_test (task); // Project1에서 실행했던 테스트 실행
	} else {
		process_wait (process_create_initd (task)); /* 유저 프로세스 실행되도록 프로세스 생성을 시작하고 프로세스 종료를 대기 */
	}
#else
	run_test (task);
#endif
	printf ("Execution of '%s' complete.\n", task);
}

/* Executes all of the actions specified in ARGV[]
   up to the null pointer sentinel.
   argv[]에 명시된 작업(action)을 수행
   각 action에 해당하는 함수를 실행
*/
static void
run_actions (char **argv)  { // argv = ["run", "'echo x'", NULL] 사실 각 인자들의 주소가 담겨 있음
	/* An action. */
	struct action {
		char *name;                       /* Action name. */
		int argc;                         /* # of args, including action name. */
		void (*function) (char **argv);   /* 실행할 함수 */
	};

	/* Table of supported actions. */
	/* actions 리스트에 지원되는 동작 리스트 저장 */
	static const struct action actions[] = {
		{"run", 2, run_task}, // "run"과 동일한 argv가 들어와야 run_task() 함수가 실행됨. 2개의 인자가 필요, run_task라는 함수 실행
#ifdef FILESYS
		{"ls", 1, fsutil_ls},
		{"cat", 2, fsutil_cat},
		{"rm", 2, fsutil_rm},
		{"put", 2, fsutil_put},
		{"get", 2, fsutil_get},
#endif
		{NULL, 0, NULL},
	};

	/* 입력받은 커맨드라인이 actions 리스트의 action과 인자 수 일치하는지 확인 */
	while (*argv != NULL) { // NULL 만날 때까지 argv 하나하나씩 실행 (["run"->"'echo x'"->NULL])
		const struct action *a;
		int i;

		/* Find action name. */
		/* 인자로 받은 *argv와 원래 저장되어 있던 actions 리스트의 동작이랑 매칭 되는지 확인 */
		// 만약 *argv = "run", a->name = "run"
		for (a = actions; ; a++)
			if (a->name == NULL)
				PANIC ("unknown action `%s' (use -h for help)", *argv);
			else if (!strcmp (*argv, a->name)) // *argv와 a->name이 같다면
				break;						   // 인자로 받은 *argv와 저장되어 있던 action이랑 비교 -> 같다면 0을 반환하고, 다르면 음수 혹은 양수를 반환

		/* Check for required arguments. */
		/* *argv 개수가 잘 들어왔는지 확인 */
		for (i = 1; i < a->argc; i++)
			if (argv[i] == NULL)
				PANIC ("action `%s' requires %d argument(s)", *argv, a->argc - 1);

		/* Invoke action and advance. */
		/* 함수 호출 */
		a->function (argv); // run_task(["run", "'echo x'", NULL]) -> run_task 함수가 호출됨
		argv += a->argc;    // a->argc = 2
	}

}

/* Prints a kernel command line help message and powers off the
   machine. */
static void
usage (void) {
	printf ("\nCommand line syntax: [OPTION...] [ACTION...]\n"
			"Options must precede actions.\n"
			"Actions are executed in the order specified.\n"
			"\nAvailable actions:\n"
#ifdef USERPROG
			"  run 'PROG [ARG...]' Run PROG and wait for it to complete.\n"
#else
			"  run TEST           Run TEST.\n"
#endif
#ifdef FILESYS
			"  ls                 List files in the root directory.\n"
			"  cat FILE           Print FILE to the console.\n"
			"  rm FILE            Delete FILE.\n"
			"Use these actions indirectly via `pintos' -g and -p options:\n"
			"  put FILE           Put FILE into file system from scratch disk.\n"
			"  get FILE           Get FILE from file system into scratch disk.\n"
#endif
			"\nOptions:\n"
			"  -h                 Print this help message and power off.\n"
			"  -q                 Power off VM after actions or on panic.\n"
			"  -f                 Format file system disk during startup.\n"
			"  -rs=SEED           Set random number seed to SEED.\n"
			"  -mlfqs             Use multi-level feedback queue scheduler.\n"
#ifdef USERPROG
			"  -ul=COUNT          Limit user memory to COUNT pages.\n"
#endif
			);
	power_off ();
}


/* Powers down the machine we're running on,
   as long as we're running on Bochs or QEMU. */
void
power_off (void) {
#ifdef FILESYS
	filesys_done ();
#endif

	print_stats ();

	printf ("Powering off...\n");
	outw (0x604, 0x2000);               /* Poweroff command for qemu */
	for (;;);
}

/* Print statistics about Pintos execution. */
static void
print_stats (void) {
	timer_print_stats ();
	thread_print_stats ();
#ifdef FILESYS
	disk_print_stats ();
#endif
	console_print_stats ();
	kbd_print_stats ();
#ifdef USERPROG
	exception_print_stats ();
#endif
}
