/*
 * Project1.c
 *
 * Created: 2024-10-09 11:21:00
 * Authors: William Lindholm, Thomas Berry
 * Advanced project
 */ 

#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/cpufunc.h>
#include <stdbool.h>


/************************************************************************/
/* Precalculated Time & Voltage values									*/
/************************************************************************/
#define THREE_VOLT 614    // 3*1023 /5 = ~613
#define EIGHT_S 2441    // 20Mhz => 0.05 uS, 0.05 * 1024 = 51.2 uS, 0.125 S = 125 000 uS, 125 000 / 51.2 ~2441 counts
#define HALF_S 9766    // 20Mhz => 0.05 uS, 0.05 * 1024 = 51.2 uS, 0.5 S = 500 000 uS, 500 000 / 51.2 ~9766 counts

#define SPLIT_LED 4    // When in split mode, split on led 4


/************************************************************************/
/* Global program modes                                                 */
/************************************************************************/
enum MODES 
{
	VOLTAGE,    // Show voltage reading (full display; 10 LED:s)
	CYLON,    // Show Cylon animation (full display; 10 LED:s)
	SPLIT    // Show Cylon animation (half display, 5 LED:s) & voltage reading (half display, 5 LED:s) 
};

enum MODES DISPLAY_MODE = CYLON;

bool ADC_READING_READY = false;
uint16_t ADC_VALUE;


/************************************************************************/
/* LED PORT definitions													*/
/************************************************************************/
struct LED_BITS
{
	PORT_t *LED_PORT;
	uint8_t bit_mapping;
};

struct LED_BITS LED_Array[10] = {
	{&PORTC, PIN5_bm}, {&PORTC, PIN4_bm}, {&PORTA, PIN0_bm}, {&PORTF, PIN5_bm}, {&PORTC, PIN6_bm}, {&PORTB, PIN2_bm}, {&PORTF, PIN4_bm}, {&PORTA, PIN1_bm}, {&PORTA, PIN2_bm}, {&PORTA, PIN3_bm}
};

// Precalculated values for non-split display are stored in precalculated_thresholds[0][0-9]
// Precalculated values for non-split display are stored in precalculated_thresholds[1][0-9]
uint16_t precalculated_thresholds[2][sizeof(LED_Array) / sizeof(LED_Array[0]) - 1];

/************************************************************************/
/* Function declarations                                                */
/************************************************************************/
void clock_init (void);
void set_leds_output(void);
void set_clr_leds(bool set);
void set_clr_leds_range(bool set, uint8_t from, uint8_t to);
void configure_TCA0(void);
void configure_ADC0(void);
void configure_button_input(void);
void display_voltage(void);
void configure_TCB0(void);
void configure_RTC(void);
void precalculate_voltage_thresholds(void);


/************************************************************************/
/* Main                                                                 */
/************************************************************************/
int main(void)
{
	clock_init();
	precalculate_voltage_thresholds();
	set_leds_output();
	set_clr_leds(0);    // ensure all LED:s are turned off by default
	configure_TCA0();
	configure_ADC0();
	configure_button_input();
	configure_RTC();
	
	sei();    // Enable global interrupts
	
	while (true) 
	{
		// show voltage reading
		if (DISPLAY_MODE != CYLON && ADC_READING_READY)
		{
			display_voltage();
			ADC_READING_READY = false;    // Reset flag, voltage reading used
		}
	}
}


/************************************************************************/
/* Precalculate the voltage values for the thermometer display			*/
/************************************************************************/
void precalculate_voltage_thresholds() {
	// Calculate for full display
	
	uint8_t total_n_leds = sizeof(LED_Array) / sizeof(LED_Array[0]) - 1;
	uint8_t split_n_leds = total_n_leds - (SPLIT_LED + 1);
	
	for (uint16_t i = 0; i < total_n_leds; i++) {
		precalculated_thresholds[0][i] = (((i+1) * 1023) / (total_n_leds + 1)); // full display values are stored in arr[0]
		precalculated_thresholds[1][i] = (((i+1) * 1023) / (split_n_leds + 1)); // split display values are stored in arr[1]
	}
}


/************************************************************************/
/* Set the CPU frequency to 20MHz, default 6MHz                         */
/************************************************************************/
void clock_init (void)
{
	// Disable CLK_PER Prescaler
	ccp_write_io( (void *) &CLKCTRL.MCLKCTRLB , (0 << CLKCTRL_PEN_bp));
}


/************************************************************************/
/* Set all led pins to output mode                                      */
/************************************************************************/
void set_leds_output()
{
	PORTC.DIR = PIN6_bm | PIN5_bm | PIN4_bm;    // Set PORTC pin 4, 5 and 6 as output
	PORTA.DIR = PIN3_bm | PIN2_bm | PIN1_bm | PIN0_bm;    // Set PORTA pin 0, 1, 2 and 3 as output
	PORTB.DIR = PIN2_bm;    // Set PORTB pin 2 as output
	PORTF.DIR = PIN5_bm | PIN4_bm;    // Set PORTF pin 4 and 5 as output 
}


/************************************************************************/
/* Enable and configure TCA0                                            */
/************************************************************************/
void configure_TCA0(void)
{
	TCA0.SINGLE.INTCTRL = TCA_SINGLE_OVF_bm;    // Counter overflow interrupt option
	TCA0.SINGLE.CTRLB = 0x00;    // Normal Mode selected
	TCA0.SINGLE.EVCTRL = 0x00;    // TCA0 can count events from the EVENT module - disable this option
	
	TCA0.SINGLE.PER = 9766;    // The TCA0 counter will overflow when it reaches the PER value (count 9766 initially)
	TCA0.SINGLE.CTRLA = TCA_SINGLE_CLKSEL_DIV1024_gc | TCA_SINGLE_ENABLE_bm;    // Prescale set to /1024 and enable TCA0 (start count)
}


/************************************************************************/
/* Enable and configure ADC0                                            */
/************************************************************************/
void configure_ADC0()
{
	ADC0.CTRLA = ADC_RESSEL_10BIT_gc | ADC_FREERUN_bm;    // Set 8 bit resolution and free run mode
	ADC0.CTRLC = ADC_SAMPCAP_bm | ADC_REFSEL_VDDREF_gc | ADC_PRESC_DIV64_gc;    // Enable SAMPCAP, Set reference voltage to VDD, Set ADC prescaler to div 128
	ADC0.MUXPOS = ADC_MUXPOS_AIN3_gc;    // Set input to Analog in 3
	ADC0.INTCTRL = ADC_RESRDY_bm;    // Enable interrupt on result ready
	ADC0.CTRLD = ADC_INITDLY_DLY16_gc;    // Initial delay of 16 cycles
	ADC0.CTRLA |= ADC_ENABLE_bm;    // Enable ADC
	ADC0.COMMAND = ADC_STCONV_bm;    // Start first measurement
}


/************************************************************************/
/* Configure PORTE bit 1 button                                         */
/************************************************************************/
void configure_button_input()
{
	PORTE.DIRCLR = (1<<1);    // Set port E pin 1 to input mode
	PORTE.PIN1CTRL = PORT_PULLUPEN_bm | PORT_ISC_BOTHEDGES_gc;    // Enable pull up resistor, set interrupt to both edges
}

/************************************************************************/
/* Configure and enable Real Time Counter                               */
/************************************************************************/
void configure_RTC()
{
	// 16 / (1/1024hz) = 16384 cycles for 16 second timer
	RTC.CLKSEL = RTC_CLKSEL_INT1K_gc;    // Select Internal 1.024 kHz oscillator
	RTC.PITCTRLA = RTC_PERIOD_CYC16384_gc | (1<<0);    // Interrupt every 16384 cycles, Periodic Interrupt Timer enabled
	RTC.PITINTCTRL = (1<<0);    // The periodic interrupt is enabled			
	RTC.CTRLA = RTC_PRESCALER_DIV1_gc | (1<<0);    // Select prescaler div1 and enable
}


/************************************************************************/
/* Set or clear all LED:s or a range of LED:s                           */
/************************************************************************/
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

void set_clr_leds_range(bool set, uint8_t from, uint8_t to) {
	
	for (uint8_t i = from; i <= to; i++)
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


/************************************************************************/
/* Display voltage reading (thermometer)								*/
/************************************************************************/
void display_voltage()
{	
	uint8_t bot_led;
	uint8_t top_led;

	if (DISPLAY_MODE == SPLIT)
	{
		bot_led = SPLIT_LED + 1;    // Start from SPLIT_LED
		top_led = sizeof(LED_Array) / sizeof(LED_Array[0]) - 1;    // Use LEDs from SPLIT_LED to the end
	}
	else
	{
		bot_led = 0;
		top_led = sizeof(LED_Array) / sizeof(LED_Array[0]) - 1;
	}

	uint8_t n_leds = top_led - bot_led + 1;    // Number of LEDs used for voltage display

	set_clr_leds_range(0, bot_led, top_led);    // Clear only the LEDs used for voltage display
	
	
	uint8_t i;
	for (i = 0; i < n_leds; i++) {
		if (ADC_VALUE < precalculated_thresholds[DISPLAY_MODE == SPLIT ? 1 : 0][i]) {
			break;
		}
	}
	
	set_clr_leds_range(1, bot_led, bot_led + i);
}


/************************************************************************/
/* Trigger next Cylon LED												*/
/************************************************************************/
void display_cylon()
{	
	uint8_t top_led;
	uint8_t bot_led = 0;

	static uint8_t i = 0;    // Index of the current LED
	static bool direction = 1;    // direction of the cylon. 0 = left, 1 = right

	// Handle split display mode
	if (DISPLAY_MODE == SPLIT)
	{
		top_led = SPLIT_LED;
	}
	else
	{
		top_led = sizeof(LED_Array) / sizeof(LED_Array[0]) - 1;    // length of a LED array
	}
	
	// Switch off all cylon LED:s
	set_clr_leds_range(0, bot_led, top_led);
	
	// If i ends up outside the range (during switch to split) reset position
	if (i < bot_led || i > top_led)
	{
		i = bot_led;
	}
	
	// Move to the next LED
	i = direction ? i+1 : i-1;
	
	// Change direction if outside bounds
	if (i >= top_led || i <= bot_led) 
	{
		direction = !direction;
	}

	// Switch on the new LED
	LED_Array[i].LED_PORT->OUTSET = LED_Array[i].bit_mapping;
}


/************************************************************************/
/* Button interrupt for voltage display									*/
/************************************************************************/
ISR(PORTE_PORT_vect) 
{	
	PORTE.INTFLAGS = PIN1_bm;    // Clear interrupt flag
	
	if (DISPLAY_MODE == SPLIT)    // Ignore button interrupt when in split mode
	{
		return;
	}
	
	if ((PORTE.IN & PIN1_bm) == 0)    // Falling edge
	{
		//set_clr_leds(0);    // Clear 
		DISPLAY_MODE = VOLTAGE;
	}
	else    // Rising edge
	{	
		DISPLAY_MODE = CYLON;
		set_clr_leds(0);    // Clear voltage reading from display
		display_cylon();    // restart cylon (prevent delay)
	}
}


/************************************************************************/
/* Timer Counter A ISR (trigger cylon animation)                        */
/************************************************************************/
ISR(TCA0_OVF_vect)
{
	if (DISPLAY_MODE != VOLTAGE)
	{
		display_cylon();
	}
	
	TCA0.SINGLE.INTFLAGS = TCA_SINGLE_OVF_bm;    // Clear interrupt flag
}

/************************************************************************/
/* ADC0 result ready ISR (handle voltage reading measurements)          */
/************************************************************************/
ISR(ADC0_RESRDY_vect)
{
	// adjust speed of cylon based on voltage
	if (ADC0.RES <= THREE_VOLT) 
	{	
		TCA0.SINGLE.PERBUF = EIGHT_S;
	}
	else
	{
		TCA0.SINGLE.PERBUF = HALF_S;
	}
	
	ADC_READING_READY = true;    // Set flag informing that new voltage value is ready
	ADC_VALUE = ADC0.RES;    // Store the reading of the ADC result in a global variable
	
	ADC0.INTFLAGS = ADC_RESRDY_bm;    // Clear interrupt flag
}

/************************************************************************/
/* ISR for RTC, Change to split mode every 16 seconds                   */
/************************************************************************/
ISR(RTC_PIT_vect)
{
	if (DISPLAY_MODE == SPLIT) 
	{
		set_clr_leds(0);    // Clear voltage reading after changing back to cylon mode
		DISPLAY_MODE = CYLON;
	}
	else
	{
		set_clr_leds(0);    // Clear lingering LED:s 
		DISPLAY_MODE = SPLIT;
	}
	
	RTC.PITINTFLAGS = (1<<0);    // Clear interrupt flag
}