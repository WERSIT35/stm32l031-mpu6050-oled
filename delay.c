#include "delay.h"

/* MUST be volatile: it is decremented in SysTick_Handler and polled in
   delay(). Without volatile the compiler is free to hoist the load out of
   the while loop and spin forever as soon as optimisation is enabled. */
volatile uint32_t delay_time = 0;

void delay_tick(void){
    if(delay_time)
        --delay_time;
}

void delay(uint32_t ms){
    delay_time = ms;
    while(delay_time){};
}
