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
#include "threads/malloc.h"
#include "userprog/process.h"
#include "threads/vaddr.h"
#include "threads/mmu.h"

void syscall_entry (void);
void syscall_handler (struct intr_frame *);

struct FD_elem{ //FD_list 안에 들어갈 정보들
   uint64_t FID;
   struct file *file;
   struct list_elem elem;
};
uint64_t FID; //open 할때 사용할 FID 번호

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

static uint64_t open(const char *name,struct thread *curr , struct FD_elem *elem , struct file *file){
//만약 파일을 못열거나 해서 NULL을 받으면
if (file != NULL){ //open 성공
	if(elem !=NULL){ //말록이 NULL을 반환할수도 있음 
		if(list_empty(&curr->fd_list)){//만약 없으면 True
			FID = 2; //없으면 초기값 2
		}
		else{//만약 비어있지 않다면 마지막+1 로 추가!
		struct list_elem *back = list_back(&curr->fd_list);	
		struct FD_elem *FID_e = list_entry(back,struct FD_elem,elem);//가장 맨 뒤 FID 찾기
		FID = FID_e->FID; //맨뒤 elem 의 FID +1 하기
		FID +=1;
		}
	elem->file = file; //보니까 retuen 할때 잘 되면 file을 리턴하고 안되면 NULL을 리턴시키네
	elem->FID = FID; //2부터 시작해서 넣기
	list_push_back(&curr->fd_list,&elem->elem);
	return FID;
	}
}
return -1;
	//if 문 사용해서 FID2가 없다면 2로 저장하고 넣기? 이런식으로 해야할꺼 같은데?
	//여기다가 list 연동? 아니면 open 에 연동?
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

	case SYS_FORK:
		// 현재 프로세스의 '이름'(첫번째 인자값)을 복제하고 new process의 아이디를 리턴하는 함수 사용
		f->R.rax = process_fork((const char *) f->R.rdi, f);
		break;
		case SYS_EXIT:
	// 	종료
		exit(f->R.rdi);
		break;	
	case SYS_HALT: //핀토스 off 시키기
		power_off();
		break;
	case SYS_CLOSE:
		break;
	case SYS_OPEN:
		{
		struct thread *curr = thread_current();
		for(const char *p = (const char *)f->R.rdi; ; p++){ //문자열 끝까지 검사
			if(!is_user_vaddr(p) || pml4_get_page(curr->pml4, p) == NULL){
				exit(-1); //잘못된 주소면 프로세스 종료
			}
			if(*p == '\0') break;
		}
		if(is_user_vaddr(f->R.rdi)&&pml4_get_page(curr->pml4,f->R.rdi)!=NULL){
			struct FD_elem *elem = malloc(sizeof(struct FD_elem));
			//그냥 하면 덮어씌우니까 별개로 만들어야 하고 별개로 만들기 위해 말록을 통해 힙에 각각의 공간을 할당
			struct file *file = filesys_open((const char *)f->R.rdi);//open 시킴
			if(file == NULL || elem == NULL) {   
				if(file != NULL) file_close(file); 
				free(elem);                        
				f->R.rax = -1;
			}
			if(file!=NULL && elem!=NULL){ //만약 file 이랑 말록 잘 되면
			uint64_t FID_re = open((const char *)f->R.rdi,curr,elem,file);
			f->R.rax = FID_re;
			}
		}
		break;      
		}          
	}              
}                   
// exit을 하면 -> msg가 떠야함. 
// 그 다음에 프로세스 exit 을 호출하는 코드를 해야함.
// 그렇게 두줄이 들어가는 코드를 만들어러ㅏㅏ.

	// 핸들러 발생! 그러면 어떻게해야되냐??

	/*
	
	register uint64_t *num asm ("rax") = (uint64_t *) num_;
	register uint64_t *a1 asm ("rdi") = (uint64_t *) a1_;
	register uint64_t *a2 asm ("rsi") = (uint64_t *) a2_;
	register uint64_t *a3 asm ("rdx") = (uint64_t *) a3_;
	register uint64_t *a4 asm ("r10") = (uint64_t *) a4_;
	register uint64_t *a5 asm ("r8") = (uint64_t *) a5_;
	register uint64_t *a6 asm ("r9") = (uint64_t *) a6_;

	*/
