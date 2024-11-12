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

/* Led related globals */
bool ADC_READING_READY;
uint16_t ADC_VALUE;



/************************************************************************/
/* Function declarations                                                */
/************************************************************************/
void CLOCK_init (void);
static void configure_USART3(void);
void sendmsg (char *s);
void configure_ADC0(void);
void print_adc_voltage(void);
void print_adc_value(void);
void print_available_commands(void);
bool queue_is_empty(void);


int main(void)
{
	char ch;
	char str_buffer[32];
	
	// Flags
	bool continuous_adc_reporting = false;
	bool continuous_timer_reporting = false;
	bool continuous_lowpulse_reporting = false;
	bool continuous_highpulse_reporting = false;

	CLOCK_init();
    configure_USART3();
	configure_ADC0();

    sei(); /* Enable Global Interrupts */
    
    while (1)
    {
        if (USART3.STATUS & USART_RXCIF_bm)
		{	/* If a character has been received, read it - this structure allows other code to run */
			ch = USART3.RXDATAL;
			switch (ch)
			{
				case 'A':
				case 'a':
					print_adc_value();
					break;
				case 'V':
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
		
		// Continuous reporting
		if (continuous_adc_reporting)
		{
			if (ADC_READING_READY && queue_is_empty())
			{
				print_adc_voltage();
				ADC_READING_READY = false;
			}
		}
    }        
}

void CLOCK_init (void)
{
	/* Do not use low frequency clock, disable CLK_PER Prescaler */
	ccp_write_io( (void *) &CLKCTRL.MCLKCTRLB , (0 << CLKCTRL_PEN_bp));
	/* If set from the fuses during programming, the CPU will now run at 20MHz (default is /6) */
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
	ADC0.CTRLA = ADC_RESSEL_10BIT_gc | ADC_FREERUN_bm;    // Set 10 bit resolution and free run mode
	ADC0.CTRLC = ADC_SAMPCAP_bm | ADC_REFSEL_VDDREF_gc | ADC_PRESC_DIV64_gc;    // Enable SAMPCAP, Set reference voltage to VDD, Set ADC prescaler to div 128
	ADC0.MUXPOS = ADC_MUXPOS_AIN3_gc;    // Set input to Analog in 3
	ADC0.INTCTRL = ADC_RESRDY_bm;    // Enable interrupt on result ready
	ADC0.CTRLD = ADC_INITDLY_DLY16_gc;    // Initial delay of 16 cycles
	ADC0.CTRLA |= ADC_ENABLE_bm;    // Enable ADC
	ADC0.COMMAND = ADC_STCONV_bm;    // Start first measurement
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