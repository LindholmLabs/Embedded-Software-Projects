/*
 * Project1.c
 *
 * Created: 2024-10-09 11:21:00
 * Authors: William Lindholm, Thomas Berry
 */ 

#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/cpufunc.h>
#include <stdbool.h>

/* Use a struct to make the association between PORTs and bits connected to the LED array more explicit */
struct LED_BITS
{
	PORT_t *LED_PORT;
	uint8_t bit_mapping;
};

struct LED_BITS LED_Array[10] = {
	{&PORTC, PIN5_bm}, {&PORTC, PIN4_bm}, {&PORTA, PIN0_bm}, {&PORTF, PIN5_bm}, {&PORTC, PIN6_bm}, {&PORTB, PIN2_bm}, {&PORTF, PIN4_bm}, {&PORTA, PIN1_bm}, {&PORTA, PIN2_bm}, {&PORTA, PIN3_bm}
};

void clock_init (void);
void set_leds_output(void);
void set_clr_leds(bool set);
void configure_TCA0(void);
void configure_ADC0(void);

int main(void)
{
	clock_init();
	set_leds_output();
	set_clr_leds(0); // ensure all leds are turned off by default
	configure_TCA0();
	configure_ADC0();
	
	sei(); // Global interrupts enable
	
	for (;;){}
}

/* Set the CPU frequency to 20Mhz, default 6Mhz */
void clock_init (void)
{
	// Disable CLK_PER Prescaler
	ccp_write_io( (void *) &CLKCTRL.MCLKCTRLB , (0 << CLKCTRL_PEN_bp));
}

/* Set all led pins to output mode */
void set_leds_output()
{
	PORTC.DIR = PIN6_bm | PIN5_bm | PIN4_bm;
	PORTA.DIR = PIN3_bm | PIN2_bm | PIN1_bm | PIN0_bm;
	PORTB.DIR = PIN2_bm;
	PORTF.DIR = PIN5_bm | PIN4_bm;
}

/* Enable and configure TCA0 */
void configure_TCA0(void)
{
	TCA0.SINGLE.INTCTRL = TCA_SINGLE_OVF_bm;									//Counter overflow interrupt option
	TCA0.SINGLE.CTRLB = 0x00;													// Normal Mode selected - TOP value in PER register
	TCA0.SINGLE.EVCTRL = 0x00;													// TCA0 can count events from the EVENT module - disable this option
	
	TCA0.SINGLE.PER = 9766;														// The TCA0 counter will overflow when it reaches the PER value
	TCA0.SINGLE.CTRLA = TCA_SINGLE_CLKSEL_DIV1024_gc | TCA_SINGLE_ENABLE_bm;	// Prescale set to /1024 and enable TCA0 (start count)
}

/* Enable and configure ADC0 */
void configure_ADC0()
{
	ADC0.CTRLA = ADC_RESSEL_8BIT_gc | ADC_FREERUN_bm; // Set 8 bit resolution and freerun mode
	ADC0.CTRLC = VREF_AC0REFSEL_AVDD_gc | ADC_PRESC_DIV128_gc; // Set reference voltage to 5V and set ADC prescaler to div 128
	ADC0.MUXPOS = ADC_MUXPOS_AIN3_gc; // set input to Analog in 3
	ADC0.CTRLA |= ADC_ENABLE_bm; // Enable ADC
}

/* Set or clear all LED port bits, 1 - set, 0 - clear */
void set_clr_leds(bool set) {
	
	uint8_t i;
	for (i = 0; i <= 9; i += 1)
	{
		if (set)
		{
			LED_Array[i].LED_PORT->OUTSET = LED_Array[i].bit_mapping;
		}
		else
		{
			LED_Array[i].LED_PORT->OUTCLR = LED_Array[i].bit_mapping;
		}
	}
}


/* Interrupt for cylon animation */
ISR(TCA0_OVF_vect)
{
	static uint8_t i = 0;		// Index of the current LED
	static bool direction = 1;	// direction of the cylon. 0 = left, 1 = right
	
	if (direction)
	{
		LED_Array[i].LED_PORT->OUTCLR = LED_Array[i].bit_mapping;
		LED_Array[i+1].LED_PORT->OUTSET = LED_Array[i+1].bit_mapping;
		i += 1;	
	}
	else 
	{
		LED_Array[i].LED_PORT->OUTCLR = LED_Array[i].bit_mapping;
		LED_Array[i-1].LED_PORT->OUTSET = LED_Array[i-1].bit_mapping;
		i -= 1;
	}
	
	if (i >= 9 || i <= 0) {
		direction = !direction; // reverse direction
	}
	
	TCA0.SINGLE.INTFLAGS = TCA_SINGLE_OVF_bm;
}