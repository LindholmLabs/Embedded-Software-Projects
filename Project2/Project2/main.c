/*
 * Project2.c
 *
 * Created: 2024-11-06 11:04:34
 * Author : Willi
 */ 

#define F_CPU 20000000
#define USART3_BAUD_RATE(BAUD_RATE) ((float)(F_CPU * 64 / (16 * (float)BAUD_RATE)) + 0.5)

#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/cpufunc.h>
#include <util/delay.h>
#include <stdio.h>
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
unsigned char qcntr = 0,sndcntr = 0;   /*indexes into the queue*/
unsigned char queue[50];       /*character queue*/

/* Led related globals */
bool ADC_READING_READY;
uint16_t ADC_VALUE;



/************************************************************************/
/* Function declarations                                                */
/************************************************************************/
void CLOCK_init (void);
static void USART3_init(void);
void sendmsg (char *s);
void configure_ADC0(void);


int main(void)
{
	char	ch;
	char	str_buffer[60];
	uint32_t adc_reading_mv;

	
	CLOCK_init();
    USART3_init();
	configure_ADC0();

    sei(); /* Enable Global Interrupts */
    
    while (1)
    {
        if (USART3.STATUS & USART_RXCIF_bm)
		{	/* If a character has been received, read it - this structure allows other code to run */
			ch = USART3.RXDATAL;
			switch (ch)
			{
				case 'a':
					adc_reading_mv = ((uint32_t)ADC_VALUE * 5000) / 1023;
					sprintf(str_buffer, "ADC0 = %d, %lumV\n", ADC_VALUE, adc_reading_mv);
					sendmsg(str_buffer);
					break;
				case 'b':
				case 'B':
					sprintf(str_buffer, "That was a B or a b\n");
					sendmsg(str_buffer);
					break;
				default:
					sprintf(str_buffer, "That was neither 'a' or 'b'\n");
					sendmsg(str_buffer);
					break;
			}
		}
		/* Even if a character has not been received, code inserted here can still run */
    }        
}

void CLOCK_init (void)
{
	/* Do not use low frequency clock, disable CLK_PER Prescaler */
	ccp_write_io( (void *) &CLKCTRL.MCLKCTRLB , (0 << CLKCTRL_PEN_bp));
	/* If set from the fuses during progamming, the CPU will now run at 20MHz (default is /6) */
}

static void USART3_init(void)
{
	PORTB.DIR &= ~PIN5_bm;		/* this is the RX input */
	PORTB.DIR |= PIN4_bm;		/* this is the TX output */
	USART3.BAUD = (uint16_t)USART3_BAUD_RATE(9600);
	USART3.CTRLB |= (USART_TXEN_bm | USART_RXEN_bm);
	PORTMUX.USARTROUTEA |= PORTMUX_USART3_ALT1_gc;
	
	USART3.CTRLA = USART_TXCIE_bm;
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
	qcntr = 0;    /*preset indices*/
	sndcntr = 1;  /*set to one because first character already sent*/
	
	while (*s)
		queue[qcntr++] = *s++;   /*put characters into queue*/
	
	USART3.TXDATAL = queue[0];  /*send first character to start process*/
}

ISR(USART3_TXC_vect)
{
	/*send next character and increment index*/
	USART3.STATUS |= USART_TXCIF_bm;
	if (qcntr != sndcntr)
		USART3.TXDATAL = queue[sndcntr++];
	/* Stop sending when the queue is empty. TXC interrupts only happen when a character 
	   has been transmitted. Stoppping sending stops the interrupts */
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