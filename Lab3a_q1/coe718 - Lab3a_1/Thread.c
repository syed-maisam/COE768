#include "cmsis_os.h"                                           // CMSIS RTOS header file
#include "osObjects.h"                      // RTOS object definitions
#include "LPC17xx.h"
#include <stdio.h>
#include "LED.h"
#include "GLCD.h"

//
#define __USE_LCD 0
#define __FI 1

/*----------------------------------------------------------------------------
 *      Sample threads
 *---------------------------------------------------------------------------*/
osMutexDef(thread_mutex_object);
osMutexId thread_mutex;

int counta=25;
int countb=0;
//unsigned int countc=0;
unsigned int flag = 0;
unsigned int i;
unsigned int j;
unsigned int k;

void owndelay(int time) {
	int x = 0;
	while (x++ < (time*10000)) {}
}
  
void Thread1 (void const *argument); // thread function
void Thread2 (void const *argument); // thread function
void Thread3 (void const *argument); // thread function


osThreadId tid_Thread; // thread id
osThreadDef (Thread1, osPriorityNormal, 1, 0);   // thread object

osThreadId tid2_Thread; // thread id
osThreadDef (Thread2, osPriorityNormal, 1, 0);   // thread object

osThreadId tid3_Thread; // thread id
osThreadDef (Thread3, osPriorityNormal, 1, 0); 

int Init_Thread (void) {
  thread_mutex = osMutexCreate(osMutex(thread_mutex_object));
  tid_Thread = osThreadCreate (osThread(Thread1), NULL);
	tid2_Thread = osThreadCreate (osThread(Thread2), NULL);
  tid3_Thread = osThreadCreate (osThread(Thread3), NULL);
	if(!tid_Thread) return(-1);
	if(!tid2_Thread) return(-1);
	if(!tid3_Thread) return(-1);
  
  return(0);
}

//Thread 3 is a toggle thread, it toggles a integer flag value which is used for other methods 
void Thread3 (void const *argument) {
	  for(k =0; k<50; k++) {
			osMutexWait(thread_mutex, osWaitForever);
			LED_Off(1);
			#ifdef __USE_LCD
			GLCD_DisplayString(6, 0, __FI, (unsigned char*)"Thread:   3    ");
		  #endif
			LED_On(2);
			flag ^= 1; //flipping flag value from 0 to 1
			//printf("Process -> Thread ID: 3 , LED_on = 2, flag = %d", flag);
			osMutexRelease(thread_mutex);
			
			owndelay(10);
			
  }
}

void Thread2 (void const *argument) {
	  for(j=0; j<50; j++) {
			osMutexWait(thread_mutex, osWaitForever);
			LED_Off(0);
			#ifdef __USE_LCD
			GLCD_DisplayString(6, 0, __FI, (unsigned char*)"Thread:   2    ");
		  #endif
			LED_On(1);
			
			if(flag == 1){
				countb++;
			}
			if(countb>25){
				countb = 0;
			}
			
			//printf("Process -> Thread ID: 2 , LED_on = 1, countb = %d,  +%d", countb, flag);
			osMutexRelease(thread_mutex);
			
			owndelay(10);
  }
}

void Thread1 (void const *argument) {
	 for(i=0; i<50; i++) {
			osMutexWait(thread_mutex, osWaitForever);
			LED_Off(2);
			#ifdef __USE_LCD
			GLCD_DisplayString(6, 0, __FI, (unsigned char*)"Thread:   1    ");
		  #endif
			LED_On(0);
			
		  if(flag == 1){
				counta--;
		  }
		  if(counta<0){
				counta = 25;
		  }
			
			//printf("Process -> Thread ID: 1 , LED_on = 0, counta = %d,  -%d", counta, flag);
			osMutexRelease(thread_mutex);
			
			owndelay(10);
		 
  }                                          
}
