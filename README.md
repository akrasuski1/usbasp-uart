# USBasp UART

This repository contains a modification to latest (2011) USBasp firmware. According to official 
[schematic](http://www.fischl.de/usbasp/bilder/usbasp_circuit.png), the UART
lines of the ATmega are connected to the ISP socket, however the firmware did not seem to use them. UART is 
often used as main debug element in embedded programming, so having ISP programmer double as debugging unit would
save much time for amateurs. 

I modified official USBasp code and added UART capabilities. It turned out to be pretty complex task - if we want to
achieve satisfactory speeds, we have to use ring buffer. But it takes significantly more processor time in interrupt
compared to ordinary fire-and-forget or busy-wait-until-done approaches. Since USB interface is very strict about timing,
I couldn't afford having interrupts disabled for such a long time. Hence, the ISRs are written partially in assembly,
and partially in plain C. There have to be many critical sections, since writing and reading pointers is not atomic
in AVR architecture. I optimized them as much as possible (since all of them are basically sections with interrupts
disabled, and we wante to minimize that time) and checked generated assembly - and I'm proud to say that
the maximum interrupt latency is less than a couple of processor cycles in worst case, which fits within bounds 
stated by V-USB library used for communication with computer - 25 cycles. Generated assembly snippets are available 
as comments in `uart.c` file. In practice, the connection with computer is steady and packets are not lost 
(which is a huge improvement from development time, when device would be disconnected after a couple of seconds).

## Installing firmware
To put new firmware on the USBasp, you need to have a second programmer or other way of ISP programming. You may use
cheap parallel port programmer, or like me, use Arduino as ISP to program the USBasp itself. You will need to do at least
one, and possibly two hardware modifications. First of them is soldering goldpins in jumper 2 place - this should be
labelled as `JP2` in your programmer. Putting a jumper here makes USBasp boot in self-programming mode, allowing you
to install new firmware.

The next step is compiling firmware:
```
cd firmware
make main.hex
```
A basic script for `avrdude` is also in the makefile, but you will probably need to modify it for your needs. 
See `firmware/Makefile` for more details (especially ISP and PORT variables). Then, you can `make fuses && make flash`.

The programmer should be OK now, with one possible catch. Official circuit shows that there is a 1kOhm resistor
between Tx line and ISP connector. For some applications this is no problem, but my cheap USB-UART converter didn't
detect such weak signal. To fix this problem, you have two options: either desolder this resistor and replace it
with one of smaller resistance (or even short both ends), or add a second resistor in parallel. I used the
second option:

![Resistor](resistor.jpg)

Now your USBasp should be ready for using UART (don't forget to take off JP2 jumper).

## Terminal program

Putting firmware on programmer is not enough though. You still need a way to communicate between computer and 
the device. For this reason, I've created a simple terminal utility working as a UART terminal. Building it should be
easy, at least on Unix:
```
cd terminal
make
```

Running newly compiled binary without arguments shows help:
```
./usbasp_uart 
Usage: ./usbasp_uart [OPTIONS]
Allows UART communication through modified USBasp.
Options:
  -r        copy UART to stdout
  -w        copy stdin to UART
  -R        perform read test (read 10kB from UART and output average speed)
  -W        perform write test (write 10kB to UART and output average speed)
  -S SIZE   set different r/w test size (in bytes)
  -b BAUD   set baud, default 9600
  -p PARITY set parity (default 0=none, 1=even, 2=odd)
  -B BITS   set byte size in bits, default 8
  -s BITS   set stop bit count, default 1

If you want to use it as interactive terminal, use ./usbasp_uart -rw -b 9600
```

## Library

The program above will probably work out of the box only on Unix. Current version is not ported to Windows yet,
but the basic functionality of the library is separated from utility code and written in pure C
for maximum compability. See `terminal/usbasp_uart.h` file for available functions - this code should compile even
on Windows, allowing developer to interface with the driver even on this system. Note that `libusb-1.0` is a dependency
(also used in avrdude code, so you probably already have it installed).

## Benchmark

The terminal utility I wrote contains code used for benchmarking UART speed. Although technically we can use any baud
rate supported by AVR chip at 12Mhz (such as 250000 baud), the ring buffer inside the chip will quickly fill and USB
bus will saturate, not keeping up with the UART, thus leading to lost characters. Note that this will happen only with
reading UART - writing to it has built-in flow control, so characters won't be lost in normal situations. When reading
AND writing, the USB bandwidth will be divided between both directions, thus decreasing both speeds.

In order to test the true speed (the speed at which no characters are lost in practice), I've wrote a simple AVR code
and put it into a second ATmega:
```
#include <avr/io.h>
#include <util/delay.h>

#define BAUD 250000UL
#define DLY  80

int main(){
	UCSR0A=(1<<U2X0);
	UCSR0B=(1<<RXEN0)|(1<<TXEN0);
	UCSR0C=(1<<UCSZ01)|(1<<UCSZ00);
	UBRR0 =F_CPU/8/BAUD-1;

	char c='a';
	while(1){
		UDR0=c++;
		if(c>'z'){c='a';}
		_delay_us(DLY);
	}
}
```
Since baud is set to a big value (250000), character sending speed will be dominated by the `_delay_us(DLY)`. By varying
`DLY` macro, I was able to change true speed and see at which point the transmission starts being lossy.

It turns out the above code, with `DLY` set to 80us is close to optimal. Running benchmark code on it gives the following
result:
```
$ ./usbasp_uart -R -S 100000 -b 250000
www.fischl.de
USBasp
Caps: 41
Baud prescaler: 5
Reading...
3/100000
33/100000
64/100000
94/100000
131/100000
145/100000
176/100000
206/100000
237/100000
[...]
99892/100000
99923/100000
99954/100000
99985/100000
100015/100000
Whole received text:
uvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrst
uvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrst
uvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrst
[...]
uvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrst
uvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrst
uvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijkl
100015 bytes received in 8084ms
Average speed: 12.371533 kB/s
```
Write test:
```
$ ./usbasp_uart -W -S 100000 -b 250000www.fischl.de
USBasp
Caps: 41
Baud prescaler: 5
Writing...
Received free=255, transmitting 255 bytes
Received free=255, transmitting 255 bytes
Received free=255, transmitting 255 bytes
Received free=255, transmitting 255 bytes
Received free=255, transmitting 255 bytes
Received free=255, transmitting 255 bytes
[...]
Received free=255, transmitting 255 bytes
Received free=255, transmitting 75 bytes
100000 bytes sent in 12404ms
Average speed: 8.061793 kB/s
```
Reading UART using UART-USB confirms that 100000 bytes were successfully sent.

To sum up: 
* max. read speed is approximately 12.4kB/s, which corresponds to constant stream of UART at 124000 baud. Note that
this result was obtained using artificially slowed UART at baud 250000 and real results may be different. In practice,
I've seen some minor losses using constant stream of 125000 (though this is dangerously close to the edge). I can
recommend 57.6k or 76.8k bauds as being pretty fast and fully error-free (in my testing)
* max. write speed is about 8.0kB/s. Note that at slower baud rates this speed will be dominated by UART sending rate - for
example, you cannot reach more than 960B/s using baud 9600.
