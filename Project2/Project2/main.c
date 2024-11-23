/*
 * Project2.c
 *
 * Created: 2024-11-06 11:04:34
 * Author : William Lindholm & Thomas Berry
 */ 

#define F_CPU 20000000
#define USART3_BAUD_RATE(BAUD_RATE) ((float)(F_CPU * 64 / (16 * (float)BAUD_RATE)) + 0.5)
#define BAUD_RATE 115200
#define QUEUE_SIZE 512

#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/cpufunc.h>
#include <util/delay.h>
#include <util/delay.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdbool.h>
#include <ctype.h>


/// TODO
// ENABLE EVENT CONTROLLED START CONVERSION FOR ADC

/************************************************************************/
/* Globals                                                              */
/************************************************************************/

/* Led related globals */
struct LED_BITS
{
	PORT_t *LED_PORT;
	uint8_t bit_mapping;
};

struct LED_BITS LED_Array[10] = {
	{&PORTC, PIN5_bm}, {&PORTC, PIN4_bm}, {&PORTA, PIN0_bm}, {&PORTF, PIN5_bm}, {&PORTC, PIN6_bm}, {&PORTB, PIN2_bm}, {&PORTF, PIN4_bm}, {&PORTA, PIN1_bm}, {&PORTA, PIN2_bm}, {&PORTA, PIN3_bm}
};


/* Serial com related globals */
uint16_t qcntr = 0, sndcntr = 0;   /*indexes into the queue*/
unsigned char queue[QUEUE_SIZE];       /*character queue*/

/* ADC related globals */
bool ADC_READING_READY;
uint16_t ADC_VALUE;

/* 555 Timer related globals */
bool TIMER_READING_READY;
uint16_t TCB0_HIGH_PULSE;
uint16_t TCB0_LOW_PULSE;
uint16_t TCB0_PERIOD;

/* Servo related globals */
uint8_t SERVO_SPEED;


/************************************************************************/
/* Function declarations                                                */
/************************************************************************/
void CLOCK_init (void);
static void configure_USART3(void);
void sendmsg (char *s);
void configure_ADC0(void);
void configure_EVSYS();
void configure_TCA0();
void configure_TCB0(void);
void configure_TCB1(void);
void configure_TCB2(void);

void print_tcb0_low_pulse(void);
void print_tcb0_high_pulse(void);
void print_adc_voltage(void);
void print_adc_value(void);
void print_available_commands(void);
void print_tcb0_timer_period(void);
int16_t calculate_servo_move_threshold(void);
bool queue_is_empty(void);



int main(void)
{
	char ch;
	char str_buffer[32];
	
	// Flags
	bool continuous_adc_reporting = false;
	bool continuous_timer_reporting = false;

	CLOCK_init();
	configure_EVSYS();
	configure_TCB0();
	configure_TCB1();
    configure_USART3();
	configure_TCB2();
	configure_ADC0();
	configure_TCA0();

    sei(); /* Enable Global Interrupts */
    
    while (1)
    {
        if (USART3.STATUS & USART_RXCIF_bm)
		{	/* If a character has been received, read it - this structure allows other code to run */
			ch = USART3.RXDATAL;
			
			// if received character is a digit
			if (isdigit(ch))
			{
				char	str_buffer[16];
				sprintf(str_buffer, "Speed: %c\n", ch);
				sendmsg(str_buffer);
				SERVO_SPEED = ch - '0'; // Convert to integer representation in ASCII
			}
			else 
			{
				// if received character is a letter
				switch (ch)
				{
					case 'L':
					case 'l':
						print_tcb0_low_pulse();
						break;
					case 'H':
					case 'h':
						print_tcb0_high_pulse();
						break;
					case 'T':
					case 't':
						print_tcb0_timer_period();
						break;
					case 'A':
					case 'a':
						print_adc_value();
						break;
					case 'C':
					case 'c':
						continuous_timer_reporting = true;
						break;
					case 'E':
					case 'e':
						continuous_timer_reporting = false;
						break;
					case 'V': // print ADC voltage
					case 'v':
						print_adc_voltage();
						break;
					case 'M': // Start continuous reporting of ADC voltage in mV
					case 'm':
						continuous_adc_reporting = true;
						break;
					case 'N': // Stop continuous reporting of ADC
					case 'n':
						continuous_adc_reporting = false;
						sprintf(str_buffer, "Stopped\n");
						sendmsg(str_buffer);
						break;
					default:
						print_available_commands();
						break;
				}
			}
			
		}
		
		// Continuous reporting
		if (continuous_adc_reporting && ADC_READING_READY && queue_is_empty())
		{
			print_adc_voltage();
			ADC_READING_READY = false;
		}
		if (continuous_timer_reporting && TIMER_READING_READY && queue_is_empty())
		{
			print_tcb0_timer_period();
			TIMER_READING_READY = false;
		}
    }        
}

void CLOCK_init (void)
{
	/* Do not use low frequency clock, disable CLK_PER Prescaler */
	ccp_write_io( (void *) &CLKCTRL.MCLKCTRLB , (0 << CLKCTRL_PEN_bp));
	/* If set from the fuses during programming, the CPU will now run at 20MHz (default is /6) */
}

void configure_EVSYS()
{
	// Set TCB0 to measure PW from PE3
	EVSYS.CHANNEL4 = EVSYS_GENERATOR_PORT0_PIN3_gc; // Select PE3 as event generator
	EVSYS.USERTCB0 = EVSYS_CHANNEL_CHANNEL4_gc; // Select TCB0 as channel4 user
	EVSYS.USERTCB1 = EVSYS_CHANNEL_CHANNEL4_gc; // Select TCB1 as channel4 user
	
	// Set ADC0 to measure when called by TCB2
	EVSYS.CHANNEL2 = EVSYS_GENERATOR_TCB2_CAPT_gc;
	EVSYS.USERADC0 = EVSYS_CHANNEL_CHANNEL2_gc;
}

static void configure_USART3(void)
{
	// Assign the USART3 transmit complete interrupt high priority (1)
	CPUINT.LVL1VEC = USART3_TXC_vect_num; 
	
	PORTB.DIR &= ~PIN5_bm;		/* this is the RX input */
	PORTB.DIR |= PIN4_bm;		/* this is the TX output */
	USART3.BAUD = (uint16_t)USART3_BAUD_RATE(BAUD_RATE); // set the baud rate
	USART3.CTRLB |= (USART_TXEN_bm | USART_RXEN_bm); // enable transmitter and receiver
	PORTMUX.USARTROUTEA |= PORTMUX_USART3_ALT1_gc; // USART3 on PB[5:4]
	
	USART3.CTRLA = USART_TXCIE_bm; // Transmit Complete Interrupt Enable
}

/************************************************************************/
/* Enable and configure ADC0                                            */
/************************************************************************/
void configure_ADC0()
{
	ADC0.CTRLA = ADC_RESSEL_10BIT_gc;    // Set 10 bit resolution
	ADC0.EVCTRL = ADC_STARTEI_bm; // Enable event controlled start conversion
	ADC0.CTRLC = ADC_SAMPCAP_bm | ADC_REFSEL_VDDREF_gc | ADC_PRESC_DIV64_gc;    // Enable SAMPCAP, Set reference voltage to VDD, Set ADC prescaler to div 64
	ADC0.MUXPOS = ADC_MUXPOS_AIN3_gc;    // Set input to Analog in 3
	ADC0.INTCTRL = ADC_RESRDY_bm;    // Enable interrupt on result ready
	ADC0.CTRLD = ADC_INITDLY_DLY0_gc;    // Initial delay of 0 cycles
	ADC0.CTRLA |= ADC_ENABLE_bm;    // Enable ADC
}

/************************************************************************/
/* Enable and configure TCB0 for PW measurement                   */
/************************************************************************/
void configure_TCB0() 
{
	// Event on falling edge to increase resolution for high pulse
	TCB0.CTRLA = TCB_CLKSEL_CLKDIV2_gc | TCB_ENABLE_bm; // Select CLK_PER/2 source and enable
	TCB0.CTRLB = TCB_CNTMODE_FRQPW_gc; // Set mode to PW measurment mode
	TCB0.INTCTRL = TCB_CAPT_bm; // Capture Interrupt Enable
	TCB0.EVCTRL = (1<<TCB_EDGE_bp) | (1<<TCB_CAPTEI_bp); // Event on falling edge & enable capture event input
}

/************************************************************************/
/* Enable and configure TCB1 for timer stop detection                   */
/************************************************************************/
void configure_TCB1()
{
	// Configure TCB1 to start counting on falling edge.
	// And timeout if value reaches top before next rising edge.
	// (Detect if oscillation stopped on 555 timer)
	// (Configured as PE3 event user)
	TCB1.CTRLA = TCB_CLKSEL_CLKDIV2_gc | TCB_ENABLE_bm; // set clock source to CLP_PER / 2 and enable
	TCB1.CTRLB = TCB_CNTMODE_TIMEOUT_gc; // Enable timeout check mode
	TCB1.INTCTRL = TCB_CAPT_bm; // Capture Interrupt Enable
	TCB1.CCMP = 65535;
	TCB1.EVCTRL = (1<<TCB_EDGE_bp) | (1<<TCB_CAPTEI_bp); // Event on falling edge & enable capture event input
}

/************************************************************************/
/* Enable and configure TCB2                                            */
/************************************************************************/
void configure_TCB2()
{
	TCB2.CTRLA = TCB_CLKSEL_CLKDIV2_gc | TCB_ENABLE_bm; // set clock source to CLP_PER / 2 and enable
	TCB2.CTRLB = TCB_CNTMODE_INT_gc; // enable periodic interrupt mode (this is default)
	TCB2.INTCTRL = TCB_CAPT_bm; // Capture Interrupt Enable
	TCB2.CCMP = 50000; // (5*10^-3)/(1/(10*10^6)) = 50000 -> 5mS
}

void configure_TCA0()
{
	TCA0.SINGLE.CTRLA = TCA_SINGLE_CLKSEL_DIV16_gc; // Set clock to clock peripheral with div 16 prescaler
	TCA0.SINGLE.CTRLB = TCA_SINGLE_CMP0EN_bm | TCA_SINGLE_WGMODE_SINGLESLOPE_gc; // W0-0 output and single slope PWM mode
	TCA0.SINGLE.PER = 24999; // Set to 50Hz PWM frequency, 20mS
	TCA0.SINGLE.CMP0 = 1250; // Set to 1mS, -90 deg
	TCA0.SINGLE.CTRLA |= TCA_SINGLE_ENABLE_bm; // Enable TCA0
	PORTA.DIRSET = PIN0_bm; // Set PA0 as output
	
}

/*this function loads the queue and */
/*starts the sending process*/
void sendmsg (char *s)
{	
	uint16_t msg_len = 0;
	char *temp = s;
	
	while (*temp)
	{
		temp++;
		msg_len++;
	}
		
	uint8_t available_space = (sndcntr > qcntr) ? (sndcntr - qcntr - 1) : (QUEUE_SIZE - qcntr + sndcntr - 1);
	
	if (msg_len > available_space) // prevent it from adding any more to the buffer if its full.
		return;
	
	// Send message with circular buffer
    while (*s)
    {
		// Prevent new message from overwriting data in queue
        if (((qcntr + 1) % QUEUE_SIZE) != sndcntr)
        {
            queue[qcntr] = *s++;
            qcntr = (qcntr + 1) % QUEUE_SIZE; // wrap around after hitting pos QUEUE_SIZE
        }
        else
        {
            break;
        }
    }

    if (qcntr != sndcntr && !(USART3.STATUS & USART_TXCIF_bm))
    {
        USART3.TXDATAL = queue[sndcntr];
        sndcntr = (sndcntr + 1) % QUEUE_SIZE;
    }
}

bool queue_is_empty()
{
	return (qcntr == sndcntr);
}

/************************************************************************/
/* Allows printing of ADC information                                   */
/************************************************************************/

// Print the current ADC volatage reading (mV)
void print_adc_voltage() 
{
	char	str_buffer[16];
	uint32_t adc_reading_mv;
	adc_reading_mv = ((uint32_t)ADC_VALUE * 5000) / 1023;
	sprintf(str_buffer, "ADC0 = %dmV\n", (uint16_t)adc_reading_mv);
	sendmsg(str_buffer);
} 

// Print the current ADC value (0-1024)
void print_adc_value() 
{
	char	str_buffer[16];
	sprintf(str_buffer, "ADC0 = %d\n", ADC_VALUE);
	sendmsg(str_buffer);
}

/************************************************************************/
/* Printing of 555 timer information                                    */
/************************************************************************/
void print_tcb0_low_pulse()
{
	char	str_buffer[16];
	sprintf(str_buffer, "Low = %dmS\n", TCB0_LOW_PULSE);
	sendmsg(str_buffer);
}

void print_tcb0_high_pulse() 
{
	char	str_buffer[16];
	sprintf(str_buffer, "High = %dmS\n", TCB0_HIGH_PULSE);
	sendmsg(str_buffer);
}

void print_tcb0_timer_period()
{
	char	str_buffer[16];
	sprintf(str_buffer, "Timer = %dmS\n", TCB0_PERIOD);
	sendmsg(str_buffer);
}

// Print the available commands 
void print_available_commands()
{
	char str_buffer[256];
	sprintf(
		str_buffer,
		"Undefined instruction.\n"
		"A/a = ADC value.\n"
		"V/v = ADC voltage reading (mV).\n"
		"M/m = Continuous ADC reporting (mV).\n"
		"N/n = Stop ADC reporting.\n"
	);
	sendmsg(str_buffer);
	
	_delay_ms(50);
	
	sprintf(
	str_buffer,
	"T/t = Report timer period (mS)\n"
	"L/l = Report low pulse\n"
	"H/h = Report high pulse\n"
	"C/c Continuously report timer period\n"
	"E/e Stop continuously reporting timer period\n"
	);
	sendmsg(str_buffer);
}

int16_t calculate_servo_move_threshold() {
	switch (SERVO_SPEED)
	{
		case 0:
			return -1;
		case 1: // 1 / 5*10^-3 = 200
			return 200;
		case 2: // 0.75 / 5*10^-3 = 150
			return 150;
		case 3: // 0.5 / 5*10^-3 = 100
			return 100;
		case 4: // 0.4 / 5*10^-3 = 80
			return 80;
		case 5: // 0.25 / 5*10^-3 = 50
			return 50;
		case 6: // 0.2 / 5*10^-3 = 40
			return 40;
		case 7: // 0.15 / 5*10^-3 = 30
			return 30;
		case 8: // 0.1 / 5*10^-3 = 20
			return 20;
		case 9: // 0.05 / 5*10^-3 = 10
			return 10;
		default:
			return -1;
	}
}

ISR(USART3_TXC_vect)
{
	 // Clear the transmit complete flag
	 USART3.STATUS |= USART_TXCIF_bm;

	 // Continue sending if there is data in the queue
	 if (sndcntr != qcntr)
	 {
		 // Load the next character from the queue
		 USART3.TXDATAL = queue[sndcntr];
		 sndcntr = (sndcntr + 1) % QUEUE_SIZE;  // Move sndcntr forward with wrap-around
	 }
}

/************************************************************************/
/* ADC0 result ready ISR (handle voltage reading measurements)          */
/************************************************************************/
ISR(ADC0_RESRDY_vect)
{
	ADC_READING_READY = true;    // Set flag informing that new voltage value is ready
	ADC_VALUE = ADC0.RES;    // Store the reading of the ADC result in a global variable
	ADC0.INTFLAGS = ADC_RESRDY_bm;    // Clear interrupt flag
}


ISR(TCB0_INT_vect)
{
	TCB0.INTFLAGS = 1; // Reset interrupt flag
	uint16_t ccmp = TCB0.CCMP;
	uint16_t cnt = TCB0.CNT;
	
	// 1 / 1MHz = 0.1uS per tick
	TCB0_PERIOD = 0.1 * cnt; // calculate timer period in uS 
	
	TCB0_LOW_PULSE = 0.1 * ccmp;
	TCB0_HIGH_PULSE = 0.1 * (cnt - ccmp);
	
	TIMER_READING_READY = true;
}

ISR(TCB1_INT_vect)
{
	TCB1.INTFLAGS = 1; // Reset interrupt flag
	char	str_buffer[16];
	sprintf(str_buffer, "STOPPED\n");
	sendmsg(str_buffer);
}

ISR(TCB2_INT_vect)
{
	TCB2.INTFLAGS = 1; // clear interrupt flag

	static uint16_t sw_counter = 0; // software counter, decides when to increment desired_pos
	static uint8_t desired_pos = 0; // (value: 0 - 25)
	static uint32_t desired_step;
	desired_step = 700 + ((2700 * (uint32_t)desired_pos)/25); // calculate next desired step (value: 1250-2500)
	static int8_t direction = 1;
	
	sw_counter++;
	
	if (desired_pos == 25)
	{
		direction = -1;
	} 
	else if (desired_pos == 0) 
	{
		direction = 1;	
	}
	
	int16_t threshold = calculate_servo_move_threshold();
	
	if (threshold == -1) { // no movement
		return;
	} 
	else if (sw_counter > threshold)
	{
		char	str_buffer[16];
		sprintf(str_buffer, "to %lu\n", desired_step);
		sendmsg(str_buffer);
		sw_counter = 0;
		desired_pos += direction;
	}
	
	TCA0.SINGLE.CMP0BUF = desired_step;
}