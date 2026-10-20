#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"
#include "filesys/filesys.h" //filesys

void syscall_entry (void);
void syscall_handler (struct intr_frame *);

struct FD_elem{ //FD_list 안에 들어갈 정보들
   int FID;
   struct file *file;
   struct list_elem elem;
};


/* System call.
 *
 * Previously system call services was handled by the interrupt handler
 * (e.g. int 0x80 in linux). However, in x86-64, the manufacturer supplies
 * efficient path for requesting the system call, the `syscall` instruction.
 *
 * The syscall instruction works by reading the values from the the Model
 * Specific Register (MSR). For the details, see the manual. */

#define MSR_STAR 0xc0000081         /* Segment selector msr */
#define MSR_LSTAR 0xc0000082        /* Long mode SYSCALL target */
#define MSR_SYSCALL_MASK 0xc0000084 /* Mask for the eflags */

void
syscall_init (void) {
	write_msr(MSR_STAR, ((uint64_t)SEL_UCSEG - 0x10) << 48  |
			((uint64_t)SEL_KCSEG) << 32);
	write_msr(MSR_LSTAR, (uint64_t) syscall_entry);

	/* The interrupt service rountine should not serve any interrupts
	 * until the syscall_entry swaps the userland stack to the kernel
	 * mode stack. Therefore, we masked the FLAG_FL. */
	write_msr(MSR_SYSCALL_MASK,
			FLAG_IF | FLAG_TF | FLAG_DF | FLAG_IOPL | FLAG_AC | FLAG_NT);
}

static void exit(uint64_t status){
	printf("%s: exit(%d)\n", thread_current() -> name, (int) status);
	thread_exit();
}

/* The main system call interface */
void
syscall_handler (struct intr_frame *f UNUSED) {
	
	// 레지스터의 rax값에 요청되는 상태를 저장한다.
	switch (f->R.rax)
	{
	case SYS_WRITE:
		// (int fd // rdx, const void *buffer // rsi, unsigned size)
		// f->R.rsi; 이게 배열의 시작주소이다.
		// f->R.rdx 이게 출력할 바이트 수

		// (STDOUT_FILENO = rdi, buf = rsi, strlen (buf) = rdx);

		putbuf((const char *) f->R.rsi, f->R.rdx); // 콘솔에 쓴다.
		f->R.rax = f->R.rdx; // write()의 반환값을 “출력한 바이트 수”로 설정.
		break;
	case SYS_EXIT:
	// 	종료
		exit(f->R.rdi);
		break;	
	case SYS_HALT: //핀토스 off 시키기
		power_off();
		break;
	case SYS_OPEN:
		/*
			지금 문제점
			1. 파일 이거 어떻게 연동사키지?
			2. 지금 잘되고 있는건가?
			3. 리스트에는 들어갈꺼 같은데 ID는 자동인가? 어떻게 할까
		*/
		struct thread *curr = thread_current();
		struct FD_elem elem;
		bool sysNULL = filesys_open(f->rsp);//open 시킴
		static uint64_t FID = 2; //2부터 시작
		//만약 파일을 못열거나 해서 NULL을 받으면
		if (sysNULL == NULL){
			break;
		}
		elem.file = sysNULL; //보니까 retuen 할때 잘 되면 file을 리턴하고 안되면 NULL을 리턴시키네
		elem.FID = FID; //2부터 시작해서 넣기
		list_push_back(&curr->fd_list,&elem.elem);
		FID +=1 ;
		//여기다가 list 연동? 아니면 open 에 연동?
		break;
	}
	

	// __attribute__
	// thread_exit ();
}
// exit을 하면 -> msg가 떠야함. 
// 그 다음에 프로세스 exit 을 호출하는 코드를 해야함.
// 그렇게 두줄이 들어가는 코드를 만들어러ㅏㅏ.

	// 핸들러 발생! 그러면 어떻게해야되냐??

	/*
	1. 유저모드에서 커널모드로 가고싶은 것. 그러면 커널모드로 전환. 권한UP 
	-> 호출자의 레지스터에는 핸들러로 전달되는 struct intr_frame을 통해 접근할 수 있습니다. 이 구조체는 커널 스택에 있습니다.
	-> 커널 모드로 바꿔주기.
	
	
	2. 시스템 콜 번호 확인 
		-> 시스템 콜을 요청하면 읽기/쓰기 처럼 요청. 그러면 유저모드와 커널모드 사이에서 정수값으로 판단.
		-> process.c 파일에서 수정. 왜냐면 load에서 커널모드로 해서 불러오기를 해야하지않나?

	-> process_exec()를 여기서 호출.
	process_exec()의 일!
		3. 인자 값 처리
			-> process_Exec에서 하지 않을까요? 잘모름.
		4. 해당 기능 수행 (ex. 파일 쓰기)
			-> 파일 읽기를 목표로.
		5. 반환 값 설정
			-> RAX 설정. 레지스터 설정할때 RAX에 결과를 넣는다.
			->  intr_frame -> gp_registers-> rax 에 값을 넣기.

	process_exec() 끝.
	6. 다시 사용자 모드로 복귀

	*/
