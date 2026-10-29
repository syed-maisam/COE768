/*----------------------------------------------------------------------------
 * CMSIS-RTOS 'main' function template
 *---------------------------------------------------------------------------*/

#define osObjectsPublic                     // define objects in main module
#include "cmsis_os.h"
#include "osObjects.h"                      // RTOS object definitions
#include "LPC17xx.h"
#include <stdio.h>
#include "LED.h"
#include "GLCD.h"
//#define __USE_LCD 0
#define __FI 1
 
extern int Init_Thread (void);
 

/*
 * main: initialize and start the system
 */
int main (void) {
  osKernelInitialize ();                    // initialize CMSIS-RTOS
	LED_Init();
//printf("\nInitializing...\n");
	
#ifdef __USE_LCD
  GLCD_Init();                               /* Initialize graphical LCD (if enabled */
	
  GLCD_Clear(White);                         /* Clear graphical LCD display   */
  GLCD_SetBackColor(Black);
  GLCD_SetTextColor(White);
  GLCD_DisplayString(0, 0, __FI, "     COE718 Demo    ");
	GLCD_SetTextColor(White);
  GLCD_DisplayString(1, 0, __FI, "     lab3a - part 1   ");
  GLCD_SetBackColor(Black);
  GLCD_SetTextColor(White);
	GLCD_DisplayString(3, 0, __FI, (unsigned char*)"Thread:        ");
#endif
  Init_Thread ();
  osKernelStart ();                         // start thread execution 
	osDelay(osWaitForever);

}
