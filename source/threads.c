#include "threads.h"
#include "main.h"

void PendSV_Handler( void ) __attribute__ (( naked ));

#define THREAD_PSP	0xFFFFFFFD

/* Thread Control Block */
typedef struct {
	void *stack;
	void *orig_stack;
	uint8_t in_use;
} tcb_t;

static tcb_t tasks[MAX_TASKS];
static int lastTask;
static int first = 1;

/* FIXME: Without naked attribute, GCC will corrupt r7 which is used for stack
 * pointer. If so, after restoring the tasks' context, we will get wrong stack
 * pointer.
 */
void PendSV_Handler( void )
{
	/* Save the old task's context */
	asm volatile("MRS R0, PSP \n");
	asm volatile("STMDB   R0!, {R4, R5, R6, R7, R8, R9, R10, R11, LR} \n");
	asm volatile("MOV %0, R0 \n" : "=r" (tasks[lastTask].stack));

	/* Find a new task to run */
	while (1) {
		lastTask++;
		if (lastTask == MAX_TASKS)
			lastTask = 0;
		if (tasks[lastTask].in_use) {
			/* Move the task's stack pointer address into r0 */
			asm volatile("MOV R0, %0\n" : : "r" (tasks[lastTask].stack));

			/* Restore the new task's context */
			asm volatile("LDMIA R0!, {R4, R5, R6, R7, R8, R9, R10, R11, LR} \n");
			asm volatile("MSR PSP, R0 \n");

			/* Jump to the task*/
			asm volatile("BX LR\n");
		}
	}
}


void SysTick_Handler(void)
{
	SCB->ICSR |= SCB_ICSR_PENDSVSET_Msk;
}

void thread_start()
{
	lastTask = 0;

	/* Save kernel context */
	asm volatile("MRS    IP, PSR \n");
	asm volatile("PUSH   {R4, R5, R6, R7, R8, R9, R10, R11, IP, LR} \n");

	/* Load user task's context from the stack */
	asm volatile("MOV     R0, %0\n" : : "r" (tasks[lastTask].stack));

	/* Put stack into PSP */
	asm volatile("MSR    PSP, R0 \n");

	/* Set SPSEL to PSP */
	asm volatile("MRS R0, CONTROL \n");
	asm volatile("ORRS R0, R0, #0x2 \n");
	asm volatile("MSR  CONTROL, R0 \n");
	asm volatile("ISB \n");

	/* Pop from Process Stack Pointer the Registers
	 * stack[0] - R4
	 * stack[1] - R5
	 * stack[2] - R6
	 * stack[3] - R7
	 * stack[4] - R8
	 * stack[5] - R9
	 * stack[6] - R10
	 * stack[7] - R11
	 * stack[8] - LR
	 * */
	asm volatile("POP {R4, R5, R6, R7, R8, R9, R10, R11, LR} \n");
	/* Pop again. R0 is used as the first parameter in a function.
	 * stack[9] - userdata
	 * */
	asm volatile("POP {R0} \n");

	/* Jump to LR */
	asm volatile("BX      LR\n");
}

int thread_create(void (*run)(void *), void *userdata)
{
	/* Find a free thing */
	int threadId = 0;
	uint32_t *stack;

	for (threadId = 0; threadId < MAX_TASKS; threadId++) {
		if (tasks[threadId].in_use == 0)
			break;
	}

	if (threadId == MAX_TASKS)
		return -1;

	/* Create the stack */
	stack = (uint32_t *) malloc(STACK_SIZE * sizeof(uint32_t));
	tasks[threadId].orig_stack = stack;
	if (stack == 0)
		return -1;

	stack += STACK_SIZE - 32; /* End of stack, minus what we are about to push */
	if (first) {
		stack[8] =  (unsigned int) run;
		stack[9] =  (unsigned int) userdata;
		first = 0;
	} else {
		stack[8] =  (unsigned int) THREAD_PSP;
		stack[9] =  (unsigned int) userdata;
		stack[14] = (unsigned) &thread_self_terminal;
		stack[15] = (unsigned int) run;
		stack[16] = (unsigned int) 0x01000000; /* PSR Thumb bit */
	}

	/* Construct the control block */
	tasks[threadId].stack = stack;
	tasks[threadId].in_use = 1;

	return threadId;
}

void thread_kill(int thread_id)
{
	tasks[thread_id].in_use = 0;

	/* Free the stack */
	free(tasks[thread_id].orig_stack);
}

void thread_self_terminal()
{
	/* This will kill the stack.
	 * For now, disable context switches to save ourselves.
	 */
	asm volatile("CPSID   I\n");
	thread_kill(lastTask);
	asm volatile("CPSIE   I\n");

	/* And now wait for death to kick in */
	while (1);
}
