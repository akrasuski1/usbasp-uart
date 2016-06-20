

all: usbasp_uart

usbasp_uart: usbasp_uart.c usbasp_uart.h main.cpp
	g++ -O2 -Wall -Wextra -std=c++14 usbasp_uart.c main.cpp -lpthread -lusb-1.0 -o usbasp_uart

clean:
	rm -f usbasp_uart
