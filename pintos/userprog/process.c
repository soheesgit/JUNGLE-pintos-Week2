#include "userprog/process.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "userprog/gdt.h"
#include "userprog/tss.h"
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/flags.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "threads/mmu.h"
#include "threads/vaddr.h"
#include "intrinsic.h"
#ifdef VM
#include "vm/vm.h"
#endif
#include "threads/synch.h"

static struct semaphore args_done;
static void process_cleanup (void);
static bool load (const char *file_name, struct intr_frame *if_);
static void initd (void *f_name);
static void __do_fork (void *);

/* General process initializer for initd and other process. */
static void
process_init (void) {
	struct thread *current = thread_current ();
}

/* Starts the first userland program, called "initd", loaded from FILE_NAME.
 * The new thread may be scheduled (and may even exit)
 * before process_create_initd() returns. Returns the initd's
 * thread id, or TID_ERROR if the thread cannot be created.
 * Notice that THIS SHOULD BE CALLED ONCE. */
tid_t
process_create_initd (const char *file_name) {
	char *fn_copy, *temp;
	tid_t tid;
	char *save_ptr;

	/* Make a copy of FILE_NAME.
	 * Otherwise there's a race between the caller and load(). */
	fn_copy = palloc_get_page (0); // 메모리 페이지 할당
	temp = palloc_get_page (0); // 메모리 페이지 할당
	if (fn_copy == NULL)
		return TID_ERROR;
	strlcpy (fn_copy, file_name, PGSIZE); // 문자열 전체를 복사한다. 즉 공백을 포함한 문자열이 fn_copy에 저장된다.
	strlcpy (temp, file_name, PGSIZE); // 문자열 전체를 복사한다. 쪼개기 위한 일회성 복사이다.
	
	
	// file_name 안에는 문자열 전체가 들어가있으므로, file_name에서 프로그램 이름만 파싱함
	// file_name을 그대로 사용하지 않는 이유: const로 선언되었기 때문에 file_name은 손상되면 안됨.
	// fn_copy를 사용하지 않는 이유: thread_create에 fn_copy를 넘겨야 하기 떄문에 fn_copy의 값도 손상되면 안됨.

	char *program_name = strtok_r(temp, " ", &save_ptr); 

	/* Create a new thread to execute FILE_NAME. */
	sema_init(&args_done, 0);
	tid = thread_create (program_name, PRI_DEFAULT, initd, fn_copy);
	if (tid == TID_ERROR)
		palloc_free_page (fn_copy);

	// temp 는 임시로 만든 값이고, 토큰화에 사용을 했으며 현재 이용이 끝남. free를 해주지 않으면 메모리 누수가 발생함
	palloc_free_page(temp);
	return tid;
}

/* A thread function that launches first user process. */
static void
initd (void *f_name) {
#ifdef VM
	supplemental_page_table_init (&thread_current ()->spt);
#endif

	process_init ();

	if (process_exec (f_name) < 0)
		PANIC("Fail to launch initd\n");
	NOT_REACHED ();
}

/* Clones the current process as `name`. Returns the new process's thread id, or
 * TID_ERROR if the thread cannot be created. */
tid_t
process_fork (const char *name, struct intr_frame *if_ UNUSED) {
	/* Clone current thread to new thread.*/
	// thread_create를 하면 tid(thread id를 리턴. pid로 사용)

	struct thread *parent_thread = thread_current();
	parent_thread->fork_if = *if_;

	tid_t child_id = thread_create (name,
			PRI_DEFAULT, __do_fork, parent_thread);

	// 자식의 정보값을 담을 구조체 생성.
	struct child_info *child = malloc(sizeof *child);
	/* pid 값 저장*/
	child->pid = child_id;
	
	/* 초기화 */
	child->exit_status = -1;
	child->exited = false;

	// 부모의 값에 존재하는 children 배열에 추가
	list_push_back(&thread_current()->children_pid, &child->elem);

	return child_id;
}

#ifndef VM
/* 이 함수를 pml4_for_each에 전달하여 부모의 주소 공간을 복제한다.
 * 프로젝트 2에서만 사용한다. */
static bool
duplicate_pte (uint64_t *pte, void *va, void *aux) {
	struct thread *current = thread_current ();
	struct thread *parent = (struct thread *) aux;
	void *parent_page;
	void *newpage;
	bool writable;

	/* 1. TODO: 부모의 페이지가 커널 페이지라면 즉시 반환한다. */

	/* 2. 부모의 최상위 페이지 테이블(PML4)에서
	 *    가상 주소 VA에 매핑된 페이지를 찾는다. */
	parent_page = pml4_get_page (parent->pml4, va);

	/* 3. TODO: 자식을 위한 새 페이지를 PAL_USER 플래그로 할당하고,
	 *    TODO: 그 결과를 NEWPAGE에 저장한다. */

	/* 4. TODO: 부모 페이지의 내용을 새 페이지에 복사한다.
	 *    TODO: 부모 페이지가 쓰기 가능한지 확인하고,
	 *    TODO: 그 결과에 따라 WRITABLE을 설정한다. */

	/* 5. 자식의 페이지 테이블에서 가상 주소 VA에 새 페이지를 매핑한다.
	 *    쓰기 권한은 WRITABLE 값에 따라 설정한다. */
	if (!pml4_set_page (current->pml4, va, newpage, writable)) {
		/* 6. TODO: 페이지 매핑에 실패하면 오류를 처리한다. */
	}
	return true;
}
#endif

/* 부모 프로세스의 실행 문맥(execution context)을 복사하는 스레드 함수.
 *
 * 힌트) parent->tf에는 프로세스의 사용자 영역(userland) 실행 문맥이 저장되어 있지 않다.
 * 따라서 process_fork()의 두 번째 인자를 이 함수에 전달해야 한다.
 */
static void
__do_fork (void *aux) {
	struct intr_frame if_;
	struct thread *parent = (struct thread *) aux;
	struct thread *current = thread_current ();
	/* TODO: 어떻게든 parent_if를 전달해야 한다. (즉, process_fork()의 if_를 전달해야 한다.) */
	struct intr_frame *parent_if;
	bool succ = true;

	/* 1. 로컬 스택의 cpu 문맥을 읽어라 */
	memcpy (&if_, parent_if, sizeof (struct intr_frame));

	/* 2. 페이지 테이블 복사 */
	current->pml4 = pml4_create();
	if (current->pml4 == NULL)
		goto error;

	// 프로세스 실행
	process_activate (current);
#ifdef VM
	supplemental_page_table_init (&current->spt);
	if (!supplemental_page_table_copy (&current->spt, &parent->spt))
		goto error;
#else
	if (!pml4_for_each (parent->pml4, duplicate_pte, parent))
		goto error;
#endif

/* TODO: 여기에 코드를 작성하세요.
 * TODO: 힌트) 파일 객체를 복제하려면
 *       include/filesys/file.h에 정의된 `file_duplicate`을 사용하세요.
 * TODO: 부모 프로세스의 자원을 성공적으로 복제할 때까지
 *       부모 프로세스가 fork()에서 반환되어서는 안 된다는 점에 유의하세요.*/

	process_init ();

	/* Finally, switch to the newly created process. */
	if (succ)
		do_iret (&if_);
error:
	thread_exit ();
}

/* Switch the current execution context to the f_name.
 * Returns -1 on fail. */
int
process_exec (void *f_name) {
	char *file_name = f_name; // 파일 이름
	bool success; // 성공 여부

	/* 우리는 쓰레드 구조체에서 intr_frame을 사용할 수 없다.
	 * 왜냐면 현재 쓰레드가 다시 스케쥴링될 때, 실행 정보가 해당 멤버에 저장이 된다. */ 
	struct intr_frame _if; 
	_if.ds = _if.es = _if.ss = SEL_UDSEG;
	_if.cs = SEL_UCSEG;
	_if.eflags = FLAG_IF | FLAG_MBS;

	/* We first kill the current context */
	process_cleanup (); // 현재 프로세스를 지운다

	/* And then load the binary */
	success = load (file_name, &_if); // 바이너리를 로드한다.

	/* If load failed, quit. */
	palloc_free_page (file_name); // 실패하면 초기화
	if (!success)
		return -1;

	/* Start switched process. */
	do_iret (&_if); // 스위치 시작!!!
	NOT_REACHED ();
}


/* Waits for thread TID to die and returns its exit status.  If
 * it was terminated by the kernel (i.e. killed due to an
 * exception), returns -1.  If TID is invalid or if it was not a
 * child of the calling process, or if process_wait() has already
 * been successfully called for the given TID, returns -1
 * immediately, without waiting.
 *
 * This function will be implemented in problem 2-2.  For now, it
 * does nothing. */
int
process_wait (tid_t child_tid UNUSED) {
	/* XXX: Hint) The pintos exit if process_wait (initd), we recommend you
	 * XXX:       to add infinite loop here before
	 * XXX:       implementing the process_wait. */
    sema_down(&args_done);

	return 0;
}

/* Exit the process. This function is called by thread_exit (). */
void
process_exit (void) {
	struct thread *curr = thread_current ();
	/* TODO: Your code goes here.
	 * TODO: Implement process termination message (see
	 * TODO: project2/process_termination.html).
	 * TODO: We recommend you to implement process resource cleanup here. */


	// 테스트 통과를 위한 임시 코드 구현이다.
	bool is_user_process = curr->pml4 != NULL;
	process_cleanup();

	if (is_user_process)
		sema_up(&args_done);
}

/* Free the current process's resources. */
static void
process_cleanup (void) { // 그냥 PML4를 destroy 하는 함수
	struct thread *curr = thread_current ();

#ifdef VM
	supplemental_page_table_kill (&curr->spt);
#endif

	uint64_t *pml4;
	/* Destroy the current process's page directory and switch back
	 * to the kernel-only page directory. */
	pml4 = curr->pml4;
	if (pml4 != NULL) {
		/* Correct ordering here is crucial.  We must set
		 * cur->pagedir to NULL before switching page directories,
		 * so that a timer interrupt can't switch back to the
		 * process page directory.  We must activate the base page
		 * directory before destroying the process's page
		 * directory, or our active page directory will be one
		 * that's been freed (and cleared). */
		curr->pml4 = NULL;
		pml4_activate (NULL);
		pml4_destroy (pml4);
	}
}

/* Sets up the CPU for running user code in the nest thread.
 * This function is called on every context switch. */
void
process_activate (struct thread *next) {
	/* Activate thread's page tables. */
	pml4_activate (next->pml4);

	/* Set thread's kernel stack for use in processing interrupts. */
	tss_update (next);
}

/* We load ELF binaries.  The following definitions are taken
 * from the ELF specification, [ELF1], more-or-less verbatim.  */

/* ELF types.  See [ELF1] 1-2. */
#define EI_NIDENT 16

#define PT_NULL    0            /* Ignore. */
#define PT_LOAD    1            /* Loadable segment. */
#define PT_DYNAMIC 2            /* Dynamic linking info. */
#define PT_INTERP  3            /* Name of dynamic loader. */
#define PT_NOTE    4            /* Auxiliary info. */
#define PT_SHLIB   5            /* Reserved. */
#define PT_PHDR    6            /* Program header table. */
#define PT_STACK   0x6474e551   /* Stack segment. */

#define PF_X 1          /* Executable. */
#define PF_W 2          /* Writable. */
#define PF_R 4          /* Readable. */

/* Executable header.  See [ELF1] 1-4 to 1-8.
 * This appears at the very beginning of an ELF binary. */
struct ELF64_hdr {
	unsigned char e_ident[EI_NIDENT];
	uint16_t e_type;
	uint16_t e_machine;
	uint32_t e_version;
	uint64_t e_entry;
	uint64_t e_phoff;
	uint64_t e_shoff;
	uint32_t e_flags;
	uint16_t e_ehsize;
	uint16_t e_phentsize;
	uint16_t e_phnum;
	uint16_t e_shentsize;
	uint16_t e_shnum;
	uint16_t e_shstrndx;
};

struct ELF64_PHDR {
	uint32_t p_type;
	uint32_t p_flags;
	uint64_t p_offset;
	uint64_t p_vaddr;
	uint64_t p_paddr;
	uint64_t p_filesz;
	uint64_t p_memsz;
	uint64_t p_align;
};

/* Abbreviations */
#define ELF ELF64_hdr
#define Phdr ELF64_PHDR

static bool setup_stack (struct intr_frame *if_);
static bool validate_segment (const struct Phdr *, struct file *);
static bool load_segment (struct file *file, off_t ofs, uint8_t *upage,
		uint32_t read_bytes, uint32_t zero_bytes,
		bool writable);

/* Loads an ELF executable from FILE_NAME into the current thread.
 * Stores the executable's entry point into *RIP
 * and its initial stack pointer into *RSP.
 * Returns true if successful, false otherwise. */
static bool
load (const char *file_name, struct intr_frame *if_) {
	struct thread *t = thread_current ();
	struct ELF ehdr;
	struct file *file = NULL;
	off_t file_ofs;
	bool success = false;
	int i;

	/* Allocate and activate page directory. */
	// 페이지 테이블 생성 및 활성화
	t->pml4 = pml4_create ();
	if (t->pml4 == NULL)
		goto done;
	process_activate (thread_current ());

	char *now_pointer, *save_pointer;
	
	//  스트링을 토큰화를해서 앞에 파일네임을 넘겨줘야함! grap foo bar
	/* Open executable file. */

	char *fn_copy = palloc_get_page (0); // 메모리 페이지 할당
	strlcpy (fn_copy, file_name, PGSIZE);
	char *program_name = strtok_r(fn_copy, " ", &save_pointer);
	file = filesys_open (program_name);
	if (file == NULL) {
		printf ("load: %s: open failed\n", program_name);
		goto done;
	}

	/* Read and verify executable header. */
	// 파일, 메모리 주소, 바이트 수를 통해서 읽어온다.

	if (file_read (file, &ehdr, sizeof ehdr) != sizeof ehdr
			|| memcmp (ehdr.e_ident, "\177ELF\2\1\1", 7)
			|| ehdr.e_type != 2
			|| ehdr.e_machine != 0x3E // amd64
			|| ehdr.e_version != 1
			|| ehdr.e_phentsize != sizeof (struct Phdr)
			|| ehdr.e_phnum > 1024) {
		printf ("load: %s: error loading executable\n", file_name);
		goto done;
	}

	// 프로그램의 헤더를 읽고 메모리에 적재
	// 헤더파일을 읽는 이유: ELF 헤더에는 실행 파일을 해석하는데 필요한 기본 정보가 들어있다.
	/* Read program headers. */
	file_ofs = ehdr.e_phoff; // 프로그램 헤더 테이블이 파일의 어느 위치에 있는지
	for (i = 0; i < ehdr.e_phnum; i++) { // 프로그램의 헤더가 몇개인지. 
		struct Phdr phdr;

		if (file_ofs < 0 || file_ofs > file_length (file))
			goto done;
		file_seek (file, file_ofs); // 파일을 읽을 위치를 변경한다.
		if (file_read (file, &phdr, sizeof phdr) != sizeof phdr)  
			goto done;
		file_ofs += sizeof phdr; // 파일의 오프셋(크기)가 올라간다.
		switch (phdr.p_type) {
			case PT_NULL:
			case PT_NOTE:
			case PT_PHDR:
			case PT_STACK:
			default:
				/* Ignore this segment. */
				break;
			case PT_DYNAMIC:
			case PT_INTERP:
			case PT_SHLIB:
				goto done;
			case PT_LOAD:
				if (validate_segment (&phdr, file)) {
					bool writable = (phdr.p_flags & PF_W) != 0;
					uint64_t file_page = phdr.p_offset & ~PGMASK;
					uint64_t mem_page = phdr.p_vaddr & ~PGMASK;
					uint64_t page_offset = phdr.p_vaddr & PGMASK;
					uint32_t read_bytes, zero_bytes;
					if (phdr.p_filesz > 0) {
						/* Normal segment.
						 * Read initial part from disk and zero the rest. */
						read_bytes = page_offset + phdr.p_filesz;
						zero_bytes = (ROUND_UP (page_offset + phdr.p_memsz, PGSIZE)
								- read_bytes);
					} else {
						/* Entirely zero.
						 * Don't read anything from disk. */
						read_bytes = 0;
						zero_bytes = ROUND_UP (page_offset + phdr.p_memsz, PGSIZE);
					}
					if (!load_segment (file, file_page, (void *) mem_page,
								read_bytes, zero_bytes, writable))
						goto done;
				}
				else
					goto done;
				break;
		}
	}

	setup_stack(if_);

	// 파일의 시작 선언
	// 파일의 시작부터 파일의 끝을 만날때까지
	
	char *fn_copy2 = palloc_get_page (0); // 메모리 페이지 할당
	int size = 0;
	
	char **double_pointer = palloc_get_page(0);
	// 원본 문자열의 길이를 반환함
	strlcpy (fn_copy2, file_name, PGSIZE);

	/* 한 번 배열을 순회하면서 문자열 단위로 나누어준다. 
		double_pointer 배열에 저장한다. */
	for (now_pointer = strtok_r(fn_copy2, " ", &save_pointer); now_pointer != NULL; now_pointer = strtok_r(NULL, " ", &save_pointer)) {
		/*
		새로운값을 할당한다
		char now_data = malloc(sizeof(strlen(now_pointer)) + 1);
		그 이후 문자열을 malloc된 값에 넣는다.
		strlcpy(now_data, now_pointer, strlen(now_pointer) + 1);

		malloc()으로 확보한 공간은 커널 측 메모리입니다.
		사용자 프로그램이 사용할 문자열은 사용자 스택에도 존재해야합니다.
		malloc이 아니라 memcpy를 통해 복사해줍니다.
		*/

		size++;
		*double_pointer = now_pointer; 
		double_pointer++;
	}


	/* 실질적으로 스택에 업로드가 되는 시작점 */

	/* 스택에 문자열들의 값을 한 번 넣어준 다음에, argv 배열에 맞게도 넣어줘야한다.
	   문자열을 삽입하면서 주소를 배열에 넣어야한다. */
	char **str_addr = palloc_get_page(0);
	char **str_addr_backup = str_addr; // str_addr의 시작 주소 백업
	
	/* 1 . 문자열을 역순으로 저장 */
	for (int i = 0; i < size; i++) {
		char *now_pointer = *--double_pointer;

		int len = strlen(now_pointer) + 1; // 문자열의 길이를 추출한다.
		if_->rsp -= len;  // 스택의 높이를 문자열의 길이만큼 내려준다.
		memcpy((void *)if_->rsp, now_pointer, len); // 스택의 주소에 now_pointer의 값을 len만큼 넘겨준다.
		
		*str_addr = (char *) if_->rsp;
		str_addr++;
	}

	/* 3. 문자열을 삽입했기 때문에 데이터의 시작 주소를 8의 배수 위치에 맞추는 작업 필요 */
	size_t padding = if_->rsp % 8; // rsp의 값을 8으로 나눈 나머지 값
	if_->rsp = if_->rsp - padding; // 스택 포인터를 내려준다
	memset((void *)if_->rsp, 0, padding); // 0으로 값을 채워준다

	/* 스택에 값을 넣기 전에 NULL 값 삽입 */
	if_->rsp -= 8;
	*(char **) if_->rsp = NULL;

	/* 3. 스택의 argv 배열의 값(넣은 문자열의 주소값)을 저장. */
	for (int i = 0; i < size; i++) {
		if_->rsp -= 8;
		*(char **) if_->rsp = *str_addr_backup;
		str_addr_backup++;
	}

	uint64_t argv_addr = if_->rsp; // 스택에 다 넣은 이후인 rsp 값을 저장한다.

	/* 4. 마지막에 가짜 반환주소 0을 넣는다. */
	if_->rsp -= 8;
	// *(char **) if_->rsp = 0;
	memset((void *)if_->rsp, 0, 8);


	/* argc, argv값을 rdi와 rsi에 각각 저장*/
	if_->R.rdi = size; // argc
	if_->R.rsi = (uint64_t) argv_addr; // 사용자 스택의 argv 배열 주소

	/* Start address. */
	if_->rip = ehdr.e_entry;
	success = true;

	palloc_free_page(fn_copy2);
	palloc_free_page(double_pointer);
	palloc_free_page(fn_copy);
	palloc_free_page(str_addr - size); 

done:
	/* We arrive here whether the load is successful or not. */
	file_close (file);
	return success;
}


/* Checks whether PHDR describes a valid, loadable segment in
 * FILE and returns true if so, false otherwise. */
static bool
validate_segment (const struct Phdr *phdr, struct file *file) {
	/* p_offset and p_vaddr must have the same page offset. */
	if ((phdr->p_offset & PGMASK) != (phdr->p_vaddr & PGMASK))
		return false;

	/* p_offset must point within FILE. */
	if (phdr->p_offset > (uint64_t) file_length (file))
		return false;

	/* p_memsz must be at least as big as p_filesz. */
	if (phdr->p_memsz < phdr->p_filesz)
		return false;

	/* The segment must not be empty. */
	if (phdr->p_memsz == 0)
		return false;

	/* The virtual memory region must both start and end within the
	   user address space range. */
	if (!is_user_vaddr ((void *) phdr->p_vaddr))
		return false;
	if (!is_user_vaddr ((void *) (phdr->p_vaddr + phdr->p_memsz)))
		return false;

	/* The region cannot "wrap around" across the kernel virtual
	   address space. */
	if (phdr->p_vaddr + phdr->p_memsz < phdr->p_vaddr)
		return false;

	/* Disallow mapping page 0.
	   Not only is it a bad idea to map page 0, but if we allowed
	   it then user code that passed a null pointer to system calls
	   could quite likely panic the kernel by way of null pointer
	   assertions in memcpy(), etc. */
	if (phdr->p_vaddr < PGSIZE)
		return false;

	/* It's okay. */
	return true;
}

#ifndef VM
/* Codes of this block will be ONLY USED DURING project 2.
 * If you want to implement the function for whole project 2, implement it
 * outside of #ifndef macro. */

/* load() helpers. */
static bool install_page (void *upage, void *kpage, bool writable);

/* FILE의 오프셋 OFS에서 시작하는 세그먼트를
 * 가상 주소 UPAGE에 로드한다.
 *
 * 총 READ_BYTES + ZERO_BYTES 바이트의 가상 메모리를
 * 다음과 같이 초기화한다.
 *
 * - UPAGE에서 시작하는 READ_BYTES 바이트는
 *   FILE의 오프셋 OFS부터 읽어와야 한다.
 *
 * - UPAGE + READ_BYTES에서 시작하는 ZERO_BYTES 바이트는
 *   0으로 초기화해야 한다.
 *
 * 이 함수가 초기화한 페이지들은 WRITABLE이 true이면
 * 사용자 프로세스가 쓸 수 있어야 하며,
 * 그렇지 않으면 읽기 전용이어야 한다.
 *
 * 성공하면 true를 반환하고,
 * 메모리 할당 오류 또는 디스크 읽기 오류가 발생하면
 * false를 반환한다. */
bool load_segment (struct file *file, off_t ofs, uint8_t *upage,
		uint32_t read_bytes, uint32_t zero_bytes, bool writable) {
	ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
	ASSERT (pg_ofs (upage) == 0);
	ASSERT (ofs % PGSIZE == 0);

	file_seek (file, ofs);
	while (read_bytes > 0 || zero_bytes > 0) {
		/* Do calculate how to fill this page.
		 * We will read PAGE_READ_BYTES bytes from FILE
		 * and zero the final PAGE_ZERO_BYTES bytes. */
		size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
		size_t page_zero_bytes = PGSIZE - page_read_bytes;

		/* Get a page of memory. */
		uint8_t *kpage = palloc_get_page (PAL_USER);
		if (kpage == NULL)
			return false;

		/* Load this page. */
		if (file_read (file, kpage, page_read_bytes) != (int) page_read_bytes) {
			palloc_free_page (kpage);
			return false;
		}
		memset (kpage + page_read_bytes, 0, page_zero_bytes);

		/* Add the page to the process's address space. */
		if (!install_page (upage, kpage, writable)) {
			printf("fail\n");
			palloc_free_page (kpage);
			return false;
		}

		/* Advance. */
		read_bytes -= page_read_bytes;
		zero_bytes -= page_zero_bytes;
		upage += PGSIZE;
	}
	return true;
}

/* Create a minimal stack by mapping a zeroed page at the USER_STACK */
static bool
setup_stack (struct intr_frame *if_) {
	uint8_t *kpage;
	bool success = false;

	kpage = palloc_get_page (PAL_USER | PAL_ZERO);
	if (kpage != NULL) {
		success = install_page (((uint8_t *) USER_STACK) - PGSIZE, kpage, true);
		if (success)
			if_->rsp = USER_STACK;
		else
			palloc_free_page (kpage);
	}
	return success;
}

/* Adds a mapping from user virtual address UPAGE to kernel
 * virtual address KPAGE to the page table.
 * If WRITABLE is true, the user process may modify the page;
 * otherwise, it is read-only.
 * UPAGE must not already be mapped.
 * KPAGE should probably be a page obtained from the user pool
 * with palloc_get_page().
 * Returns true on success, false if UPAGE is already mapped or
 * if memory allocation fails. */
static bool
install_page (void *upage, void *kpage, bool writable) {
	struct thread *t = thread_current ();

	/* Verify that there's not already a page at that virtual
	 * address, then map our page there. */
	return (pml4_get_page (t->pml4, upage) == NULL
			&& pml4_set_page (t->pml4, upage, kpage, writable));
}
#else
/* From here, codes will be used after project 3.
 * If you want to implement the function for only project 2, implement it on the
 * upper block. */

static bool
lazy_load_segment (struct page *page, void *aux) {
	/* TODO: Load the segment from the file */
	/* TODO: This called when the first page fault occurs on address VA. */
	/* TODO: VA is available when calling this function. */
}

/* Loads a segment starting at offset OFS in FILE at address
 * UPAGE.  In total, READ_BYTES + ZERO_BYTES bytes of virtual
 * memory are initialized, as follows:
 *
 * - READ_BYTES bytes at UPAGE must be read from FILE
 * starting at offset OFS.
 *
 * - ZERO_BYTES bytes at UPAGE + READ_BYTES must be zeroed.
 *
 * The pages initialized by this function must be writable by the
 * user process if WRITABLE is true, read-only otherwise.
 *
 * Return true if successful, false if a memory allocation error
 * or disk read error occurs. */
static bool
load_segment (struct file *file, off_t ofs, uint8_t *upage,
		uint32_t read_bytes, uint32_t zero_bytes, bool writable) {
	ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
	ASSERT (pg_ofs (upage) == 0);
	ASSERT (ofs % PGSIZE == 0);

	while (read_bytes > 0 || zero_bytes > 0) {
		/* Do calculate how to fill this page.
		 * We will read PAGE_READ_BYTES bytes from FILE
		 * and zero the final PAGE_ZERO_BYTES bytes. */
		size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
		size_t page_zero_bytes = PGSIZE - page_read_bytes;

		/* TODO: Set up aux to pass information to the lazy_load_segment. */
		void *aux = NULL;
		if (!vm_alloc_page_with_initializer (VM_ANON, upage,
					writable, lazy_load_segment, aux))
			return false;

		/* Advance. */
		read_bytes -= page_read_bytes;
		zero_bytes -= page_zero_bytes;
		upage += PGSIZE;
	}
	return true;
}

/* Create a PAGE of stack at the USER_STACK. Return true on success. */
static bool
setup_stack (struct intr_frame *if_) {
	bool success = false;
	void *stack_bottom = (void *) (((uint8_t *) USER_STACK) - PGSIZE);

	/* TODO: Map the stack on stack_bottom and claim the page immediately.
	 * TODO: If success, set the rsp accordingly.
	 * TODO: You should mark the page is stack. */
	/* TODO: Your code goes here */

	return success;
}
#endif /* VM */
