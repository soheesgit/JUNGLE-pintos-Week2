#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"

void syscall_entry (void);
void syscall_handler (struct intr_frame *);

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

/* The main system call interface */
void
syscall_handler (struct intr_frame *f UNUSED) {
	
	// 레지스터의 rax값에 요청되는 상태를 저장한다.
	switch (f->R.rax)
	{
	case SYS_WRITE:
		// f->R.rdi 이게 argc의 값. argc는 개수이다.
		// f->R.rsi; 이게 배열의 시작주소이다.
		putbuf(f->R.rsi, f->R.rdi); // 버퍼에 쓴다.
		break;
	}

	// 준강강의: 1. rdi가 잘 넘어갔는가? 2. rdi
	/*
	msg()를 위해 시스템 콜을 출력해야함.
	intr_frame -> 사실상 기존 유저모드가 저장되는 공간을 전달해준것임. 끝나면 다시 intr_frame로 전환을 해주는건가?

	1. 유저영역에서 메시지를 출력해야함.
	2. 유저가 시스템 콜 요청
	-> systemcall에 포인터를 보내는데 버퍼를 넣어주고 주소 u를 보내준다. 
		-> 주소u: 출력할 데이터를 넣은 주소 공간을 보내줌.
		-> MPL4를 가져온 다음에 커널 모드에서 유저 모드로 출력(write)를 해줌.
		-> 시스템콜 종료.
	*/

	// TODO: Your implementation goes here.
	printf ("system call!\n");
	thread_exit ();
}


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
