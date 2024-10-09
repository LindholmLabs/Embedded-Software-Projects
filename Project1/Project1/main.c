/*
 * Project1.c
 *
 * Created: 2024-10-09 11:21:00
 * Authors: William Lindholm, Thomas Berry
 */ 

#define BINARY_SETTINGS 1

#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/cpufunc.h>

/* Use a struct to make the association between PORTs and bits connected to the LED array more explicit */
struct LED_BITS
{
	PORT_t *LED_PORT;
	uint8_t bit_mapping;
};

enum DIRECTION
{
	LEFT,
	RIGHT
};

struct LED_BITS LED_Array[10] = {
	{&PORTC, PIN5_bm}, {&PORTC, PIN4_bm}, {&PORTA, PIN0_bm}, {&PORTF, PIN5_bm}, {&PORTC, PIN6_bm}, {&PORTB, PIN2_bm}, {&PORTF, PIN4_bm}, {&PORTA, PIN1_bm}, {&PORTA, PIN2_bm}, {&PORTA, PIN3_bm}
};

uint8_t CURRENT_CYLON_LED = 0;		// Index of the current LED
enum DIRECTION CURRENT_CYLON_DIRECTION = LEFT;

void CLOCK_init (void);
void InitialiseLED_PORT_bits(void);
void Set_Clear_Ports(uint8_t set);
void TCA0_init_bits(void);


void CLOCK_init (void)
{
	/* Disable CLK_PER Prescaler */
	ccp_write_io( (void *) &CLKCTRL.MCLKCTRLB , (0 << CLKCTRL_PEN_bp));
	/* If set from the fuses during device programming, the CPU will now run at 20MHz (default is /6) */
}

void InitialiseLED_PORT_bits()
{
	PORTC.DIR = PIN6_bm | PIN5_bm | PIN4_bm;  /*(1<<6) | (1<<5) | (1<<4); 0x70;*/		/* PC4-UNO D1 (TXD1), PC5-UNO D0 (RXD1), PC6 - UNO D4  */
	PORTA.DIR = PIN3_bm | PIN2_bm | PIN1_bm | PIN0_bm; /*(1<<1) | (1<<0);   0x0f; */      /* PA1-UNO D7, PA0 - UNO D2, PA2- LED8, PA3 - LED9  */
	PORTB.DIR = PIN2_bm; /*0x04;*/		/* PB2 - UNO D5 */
	PORTF.DIR = PIN5_bm | PIN4_bm; /*(1<<5) | (1<<4);   0x30; */		/* PF5 - UNO D3, PF4 UNO D6 */
	/* Later use PIN6_bm etc */
}

/* Function to set or clear all LED port bits, 1 - set, 0 - clear */
void Set_Clear_Ports(uint8_t set) {
	
	uint8_t i;
	
	for (i = 0; i <= 9; i += 1)
	{
		if (set)
		LED_Array[i].LED_PORT->OUTSET = LED_Array[i].bit_mapping;
		else
		LED_Array[i].LED_PORT->OUTCLR = LED_Array[i].bit_mapping;
	}
}

void Toggle_Ports(void)
{
	uint8_t i;
	
	for (i = 0; i <= 9; i += 1)
	{
		LED_Array[i].LED_PORT->OUTTGL = LED_Array[i].bit_mapping;
	}
}

int main(void)
{
	CLOCK_init();
	
	/* set UNO D0-D7 to all outputs, also LED8 and LED9  */
	InitialiseLED_PORT_bits();
	
	Set_Clear_Ports(0);
	
	TCA0_init_bits();
	
	sei();
	
	while (1)
	{
	}
}

void TCA0_init_bits(void)
{

	#ifdef BINARY_SETTINGS
	
	TCA0.SINGLE.INTCTRL = 0b00000001;		/* Counter overflow interrupt option */
	TCA0.SINGLE.CTRLB = 0b00000000;			/* Normal Mode selected - TOP value in PER register */
	TCA0.SINGLE.EVCTRL = 0b00000000;		/* TCA0 can count events from the EVENT module - disable this option */
	
	/* Now set the PER register to 9766, our top value */
	/* This is because 20MHz/1024  gives TCA0clk period = 51.2us and 51.2us*9766 = 0.5000192s */
	
	TCA0.SINGLE.PER = 9766;					/* The TCA0 counter will overflow when it reaches the PER value */
	TCA0.SINGLE.CTRLA = 0b00001111;			/* Prescale set to /1024 and enable TCA0 (start count) */

	#else
	
	/* Same implementation using explicit bit naming: */
	TCA0.SINGLE.INTCTRL = TCA_SINGLE_OVF_bm;		 //Counter overflow interrupt option
	TCA0.SINGLE.CTRLB = 0x00;			 // Normal Mode selected - TOP value in PER register
	TCA0.SINGLE.EVCTRL = 0x00;		 // TCA0 can count events from the EVENT module - disable this option
	
	
	TCA0.SINGLE.PER = 9766;			// The TCA0 counter will overflow when it reaches the PER value
	TCA0.SINGLE.CTRLA = TCA_SINGLE_CLKSEL_DIV1024_gc | TCA_SINGLE_ENABLE_bm;			// Prescale set to /1024 and enable TCA0 (start count)
	
	#endif

}

void Toggle_Cylon_Direction() {
	CURRENT_CYLON_DIRECTION = (CURRENT_CYLON_DIRECTION == LEFT) ? RIGHT : LEFT;
}

ISR(TCA0_OVF_vect)
{
	if (CURRENT_CYLON_DIRECTION == LEFT) 
	{
		LED_Array[CURRENT_CYLON_LED].LED_PORT->OUTCLR = LED_Array[CURRENT_CYLON_LED].bit_mapping;
		LED_Array[CURRENT_CYLON_LED+1].LED_PORT->OUTSET = LED_Array[CURRENT_CYLON_LED+1].bit_mapping;
		CURRENT_CYLON_LED += 1;	
	}
	else 
	{
		LED_Array[CURRENT_CYLON_LED].LED_PORT->OUTCLR = LED_Array[CURRENT_CYLON_LED].bit_mapping;
		LED_Array[CURRENT_CYLON_LED-1].LED_PORT->OUTSET = LED_Array[CURRENT_CYLON_LED-1].bit_mapping;
		CURRENT_CYLON_LED -= 1;
	}
	
	if (CURRENT_CYLON_LED >= 9 || CURRENT_CYLON_LED <= 0) {
		Toggle_Cylon_Direction();
	}
	
	//Toggle_Ports();
	/* The interrupt flag has to be cleared manually */
	
	#ifdef BINARY_SETTINGS
	TCA0.SINGLE.INTFLAGS = 0b00000001;   /* Writing 1 to the flag bit clears it */
	#else
	/* Same implementation using explicit bit naming: */
	TCA0.SINGLE.INTFLAGS = TCA_SINGLE_OVF_bm;
	#endif
	
}