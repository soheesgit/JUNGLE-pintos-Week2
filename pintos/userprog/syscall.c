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
	struct thread *curr = thread_current();		 //나의 쓰레드 가져오기
	// 레지스터의 rax값에 요청되는 상태를 저장한다.
	switch (f->R.rax)
	{
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
	case SYS_WRITE:
		// (STDOUT_FILENO = rdi, buf = rsi, strlen (buf) = rdx);
		unsigned size = 0; //쓴 버퍼 사이즈

		if(f->R.rdi == 1){
			putbuf((const char *) f->R.rsi, f->R.rdx); 	// 콘솔에 쓴다.
			f->R.rax = f->R.rdx;						// write()의 반환값을 “출력한 바이트 수”로 설정.
		}else{
			struct list_elem *cur_fd_elem = list_begin(&curr->fd_list);	//내 fd_list시작을 가져옴
			while(1){ 								
				if(cur_fd_elem != list_end(&curr->fd_list)){ //fd_list가 끝날때까지
					struct FD_elem *cur_FDS_elem = list_entry(cur_fd_elem, struct FD_elem,elem); //fd_elem의 FD struct 가져오기
					if(cur_FDS_elem->FID == f->R.rdi){ 						// fd리스트에 있는 file번호와 인자로 들어온 fd값이 같으면 파일 읽기
						size = file_write(cur_FDS_elem->file, f->R.rsi, f->R.rdx);
						f->R.rax = size;
						break;
					}else{ 				 //다르면 다음 fd 읽음
					cur_fd_elem = list_next(cur_fd_elem);
					}
				}else{ 					//fd_list 끝에 도달하면
					f->R.rax = -1;		//-1 반환
					break;
				}
			}
		}
		break;
	case SYS_READ:
		//파일 읽기
		size = 0; //읽은 버퍼 사이즈

		if(f->R.rdi == 0){ //키보드로 받아서 buffer에 저장
			char *buffer = f->R.rsi;
			while(f->R.rdx > size){
				uint8_t ch = input_getc(); // uint8_t key 반환
				*buffer = (char)ch;
				buffer++;
				size++;
			}
			f->R.rax = size;
		}else{
			struct list_elem *cur_fd_elem = list_begin(&curr->fd_list);	//내 fd_list시작을 가져옴
			while(cur_fd_elem != list_end(&curr->fd_list)){ 								//fd_list가 끝날때까지
				struct FD_elem *cur_FDS_elem = list_entry(cur_fd_elem, struct FD_elem,elem); //fd_elem의 FD struct 가져오기
				if(cur_FDS_elem->FID == f->R.rdi){ 						// fd리스트에 있는 file번호와 인자로 들어온 fd값이 같으면 파일 읽기
					size = file_read(cur_FDS_elem->file, f->R.rsi, f->R.rdx);	 //파일 읽기(실제로 읽은 바이트수 반환)
					f->R.rax = size;
					break;
				}else{ 				 //다르면 다음 fd 읽음
				cur_fd_elem = list_next(cur_fd_elem);
				}
			}
		}
		break;
	case SYS_FILESIZE:
			//파일 크기 bytefh 표현하기
			struct list_elem *cur_fd_elem = list_begin(&curr->fd_list);	//내 fd_list시작을 가져옴
			while(cur_fd_elem != list_end(&curr->fd_list)){ 								//fd_list가 끝날때까지
				struct FD_elem *cur_FDS_elem = list_entry(cur_fd_elem, struct FD_elem,elem); //fd_elem의 FD struct 가져오기
				if(cur_FDS_elem->FID == f->R.rdi){ 						// fd리스트에 있는 file번호와 인자로 들어온 fd값이 같으면 파일 읽기
					f->R.rax = file_length (cur_FDS_elem->file); 		//파일 크기 반환
					break;
				}else{ 				 //다르면 다음 fd 읽음
				cur_fd_elem = list_next(cur_fd_elem);
				}
			}

		break;	
	// __attribute__
	// thread_exit ();	
	}              
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
