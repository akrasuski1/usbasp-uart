#include "uart.h"

#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/atomic.h>

// This struct and its corresponding functions assume there
// are two threads: writer and reader, at most one of each.
//
// For rx: reader is USB code, and writer is RXC interrupt.
// For tx: reader is UDRE interrupt, and reader is USB code.
typedef struct ringBuffer{
	volatile uint8_t* volatile write;
	volatile uint8_t* volatile read;
	volatile uint8_t* const start;
	volatile uint8_t* const end;
} ringBuffer;

// Called by writer only, so only the rb->read part
// needs to be atomic.
static int8_t ringBufferFull(ringBuffer* rb){
	volatile uint8_t* next;
	volatile uint8_t* read;
	next=rb->write+1;
	if(next==rb->end){next=rb->start;}
/* Interrupts blocked for ~5 clocks.
    1304:       f8 94           cli
    1306:       42 81           ldd     r20, Z+2        ; 0x02
    1308:       53 81           ldd     r21, Z+3        ; 0x03
    130a:       8f bf           out     0x3f, r24       ; 63
*/
	ATOMIC_BLOCK(ATOMIC_RESTORESTATE){
		read=rb->read;
	}
	return next==read;
}

// Called by reader only, so just the rb->write part
// needs to be atomic.
static int ringBufferEmpty(ringBuffer* rb){
	volatile uint8_t* read;
	volatile uint8_t* write;
	read=rb->read;
/* Interrupts blocked for ~5 clocks.
    1326:       f8 94           cli
    1328:       20 81           ld      r18, Z
    132a:       31 81           ldd     r19, Z+1        ; 0x01
    132c:       6f bf           out     0x3f, r22       ; 63
*/
	ATOMIC_BLOCK(ATOMIC_RESTORESTATE){
		write=rb->write;
	}
	return read==write;
}

// Called only by writer, so reading rb->write does not need synchronization.
// Note that this function assumes there is place in the buffer.
static void ringBufferWrite(ringBuffer* rb, uint8_t c){
	volatile uint8_t* write;
	write=rb->write;
	*write++=c;
	if(write==rb->end){write=rb->start;}
/* Interrupts blocked for ~5 clocks.
    134e:       f8 94           cli
    1350:       90 93 63 00     sts     0x0063, r25
    1354:       80 93 62 00     sts     0x0062, r24
    1358:       2f bf           out     0x3f, r18       ; 63
*/
	ATOMIC_BLOCK(ATOMIC_RESTORESTATE){
		// Don't remove this block, it is needed! I wasted lots of time
		// tracking down race condition appearing here - pointers are 16-bit
		// and hence non-atomic, and rb->write is used in comparisons, for 
		// example in ringBufferFull function.
		rb->write=write;
	}
}

// Called by reader only, so reading rb->read does not need synchronization.
// Note that this function assumes buffer is not empty.
static uint8_t ringBufferRead(ringBuffer* rb){
	uint8_t c;
	volatile uint8_t* read;
	read=rb->read;
	c=*read++;
	if(read==rb->end){read=rb->start;}
/* Interrupts blocked for ~5 clocks.
    13dc:       f8 94           cli
    13de:       90 93 6d 00     sts     0x006D, r25
    13e2:       80 93 6c 00     sts     0x006C, r24
    13e6:       3f bf           out     0x3f, r19       ; 63
*/
	ATOMIC_BLOCK(ATOMIC_RESTORESTATE){
		rb->read=read;
	}
	return c;
}

static uint8_t tx_buff[RINGBUFFER_TX_SIZE];
static uint8_t rx_buff[RINGBUFFER_RX_SIZE];

static ringBuffer tx={
	tx_buff,
	tx_buff,
	tx_buff,
	tx_buff+RINGBUFFER_TX_SIZE
};

static ringBuffer rx={
	rx_buff,
	rx_buff,
	rx_buff,
	rx_buff+RINGBUFFER_RX_SIZE
};

void __vector_usart_rxc_wrapped() __attribute__ ((signal));
void __vector_usart_rxc_wrapped(){
	uint8_t c=UDR;
	if(!ringBufferFull(&rx)){
		ringBufferWrite(&rx, c);
	}
	// Reenable interrupt.
	UCSRB|=1<<RXCIE;
}

// This cannot be ISR_NOBLOCK, since the interrupt would go
// into infinite loop, since we wouldn't get up to reading
// UDR register. Instead, we use assembly to do the job
// manually and then jump to the real handler.
// USB interrupt is delayed only 3 clocks or so.
ISR(USART_RXC_vect, ISR_NAKED){
	// Disable this interrupt by clearing its Interrupt Enable flag.
	__asm__ volatile("cbi %0, %1"::
			"I"(_SFR_IO_ADDR(UCSRB)),"I"(RXCIE));
	__asm__ volatile("sei"::);
	__asm__ volatile("rjmp __vector_usart_rxc_wrapped"::);
}

void __vector_usart_udre_wrapped() __attribute__ ((signal));
void __vector_usart_udre_wrapped(){
	if(!ringBufferEmpty(&tx)){
		UDR=ringBufferRead(&tx);
		UCSRB|=(1<<UDRIE); // Enable this interrupt back.
	}
}

// This cannot be ISR_NOBLOCK, since UDRE is level sensitive.
// Therefore, we clear the interrupt manually and then jump
// into the real handler. USB interrupt delay is about 3 clocks.
ISR(USART_UDRE_vect, ISR_NAKED){
	// Disable this interrupt by clearing its Interrupt Enable flag.
	__asm__ volatile("cbi %0, %1"::
			"I"(_SFR_IO_ADDR(UCSRB)),"I"(UDRIE));
	// Now we can enable interrupts without infinite recursion.
	__asm__ volatile("sei"::);
	// Finally, we jump into the actual handler.
	__asm__ volatile("rjmp __vector_usart_udre_wrapped"::);
}

void uart_dbg(){
	int c=tx.write-tx.read;
	if(c<0){ c+= RINGBUFFER_TX_SIZE; }
	uart_putc('s');
	uart_putc(c);
}

// This is called by USB thread only, which is writer of tx
// ringBuffer, so only rb->read needs to be atomic.
uint16_t uart_tx_freeplaces(){
	volatile uint8_t* read;
	volatile uint8_t* write;
	write=tx.write;
/* Interrupts blocked for ~5 clocks.
    14c2:       f8 94           cli
    14c4:       80 91 6c 00     lds     r24, 0x006C
    14c8:       90 91 6d 00     lds     r25, 0x006D
    14cc:       78 94           sei
*/
	ATOMIC_BLOCK(ATOMIC_FORCEON){
		read=tx.read;
	}
	int16_t res=read-write;
	if(res<=0){ res+=RINGBUFFER_RX_SIZE; }
	return res-1;
}

// Returns 1 if OK.
uint8_t uart_putc(uint8_t c){
	if(ringBufferFull(&tx)){
		return 0;
	}
	ringBufferWrite(&tx, c);
	UCSRB|=(1<<UDRIE); // Enable UDRE interrupt.
	return 1;
}

// Returns 1 if OK.
uint8_t uart_getc(uint8_t* c){
	if(ringBufferEmpty(&rx)){
		return 0;
	}
	*c=ringBufferRead(&rx);
	return 1;
}

void uart_disable(){
	UCSRB=0;
}

// Called by USB thread, which is reader of rx ringBuffer,
// so only rb->write needs synchronization.
void uart_flush_rx(){
	volatile uint8_t* write;
	// Make avr-gcc store rx pointer in registers before atomic block.
	(void)rx.write;
/* Interrupts blocked for ~5 cycles.
    15f4:       f8 94           cli
    15f6:       80 81           ld      r24, Z
    15f8:       91 81           ldd     r25, Z+1        ; 0x01
    15fa:       78 94           sei
*/
	ATOMIC_BLOCK(ATOMIC_FORCEON){
		write=rx.write;
	}
	rx.read=write;
}

// Called by USB thread, which is writer of tx ringBuffer,
// so only rb->read needs synchronization.
void uart_flush_tx(){
	volatile uint8_t* read;
	// Make avr-gcc store tx pointer in registers before atomic block.
	(void)tx.read;
/* Interrupts blocked for ~5 cycles.
    160a:       f8 94           cli
    160c:       82 81           ldd     r24, Z+2        ; 0x02
    160e:       93 81           ldd     r25, Z+3        ; 0x03
    1610:       78 94           sei
*/
	ATOMIC_BLOCK(ATOMIC_FORCEON){
		read=tx.read;
	}
	tx.write=read;
}

void uart_config(uint16_t baud, uint8_t par, uint8_t stop, uint8_t bytes){
	uart_disable();

	PORTD|=1<<1; // Tx initially high.
	DDRD |=1<<1; // Tx as output.

	uart_flush_tx();
	uart_flush_rx();

	// Turn 2x mode.
	UCSRA=(1<<U2X);

	uint8_t byte=0;
	switch(par){
	case USBASP_UART_PARITY_EVEN: byte|=(1<<UPM1); break;
	case USBASP_UART_PARITY_ODD:  byte|=(1<<UPM1)|(1<<UPM0); break;
	default: break;
	}

	if(stop == USBASP_UART_STOP_2BIT){
		byte|=(1<<USBS);
	}

	switch(bytes){
	case USBASP_UART_BYTES_6B: byte|=(1<<UCSZ0); break;
	case USBASP_UART_BYTES_7B: byte|=(1<<UCSZ1); break;
	case USBASP_UART_BYTES_8B: byte|=(1<<UCSZ1)|(1<<UCSZ0); break;
	case USBASP_UART_BYTES_9B: byte|=(1<<UCSZ2)|(1<<UCSZ1)|(1<<UCSZ0); break;
	default: break;
	}

	UCSRC=byte;
	
	UBRRL=baud&0xFF;
	UBRRH=baud>>8;

	// Turn on RX/TX and RX interrupt.
	UCSRB=(1<<RXCIE)|(1<<RXEN)|(1<<TXEN);
}
