#include "usbasp_uart.h"

#include <unistd.h>
#include <chrono>
#include <fstream>
#include <thread>
#include <vector>

void writeTest(USBasp_UART* usbasp, int size){
	std::string s;
	char c='a';
	for(int i=0; i<size; i++){
		s+=c;
		c++;
		if(c>'z'){ c='a'; }
	}
	auto start=std::chrono::high_resolution_clock::now();
	usbasp_uart_write_all(usbasp, (uint8_t*)s.c_str(), s.size());
	auto finish=std::chrono::high_resolution_clock::now();
	auto us=std::chrono::duration_cast<std::chrono::microseconds>(finish-start).count();
	printf("%zu bytes sent in %zums\n", s.size(), us/1000);
	printf("Average speed: %lf kB/s\n", s.size()/1000.0/(us/1000000.0));
}

void readTest(USBasp_UART* usbasp, size_t size){
	auto start=std::chrono::high_resolution_clock::now();
	int us;
	std::string s;
	while(1){
		if(s.size()==0){
			start=std::chrono::high_resolution_clock::now();
		}
		if(s.size()>=size){
			auto finish=std::chrono::high_resolution_clock::now();
			us=std::chrono::duration_cast<std::chrono::microseconds>(finish-start).count();
			break;
		}
		uint8_t buff[300];
		int rv=usbasp_uart_read(usbasp, buff, sizeof(buff));
		if(rv==0){ continue; } // Nothing is available for now.
		else if(rv<0){
			fprintf(stderr, "rv=%d\n", rv);
		}
		else{
			s+=std::string((char*)buff, rv);
			fprintf(stderr, "%zu/%zu\n", s.size(), size);
		}
	}
	printf("Whole received text:\n");
	printf("%s\n", s.c_str());
	printf("%zu bytes received in %dms\n", s.size(), us/1000);
	printf("Average speed: %lf kB/s\n", s.size()/1000.0/(us/1000000.0));
}

void read_forever(USBasp_UART* usbasp){
	while(1){
		uint8_t buff[300];
		int rv=usbasp_uart_read(usbasp, buff, sizeof(buff));
		if(rv<0){
			fprintf(stderr, "read: rv=%d\n", rv);
			return;
		}
		for(int i=0;i<rv;i++){
			printf("%c",buff[i]);
		}
		fflush(stdout);
	}
}

void write_forever(USBasp_UART* usbasp){
	uint8_t buff[1024];
	while(1){
		int rv=read(STDIN_FILENO, buff, sizeof(buff));
		if(rv==0){ return; }
		else if(rv<0){
			fprintf(stderr, "write: read from stdin returned %d\n", rv);
			return;
		}
		else{
			usbasp_uart_write_all(usbasp, buff, rv);
		}
	}
}

void usage(const char* name){
	fprintf(stderr, "Usage: %s [OPTIONS]\n", name);
	fprintf(stderr, "Allows UART communication through modified USBasp.\n");
	fprintf(stderr, "Options:\n");
	fprintf(stderr, "  -r        copy UART to stdout\n");
	fprintf(stderr, "  -w        copy stdin to UART\n");
	fprintf(stderr, "  -R        perform read test (read 10kB from UART and output average speed)\n");
	fprintf(stderr, "  -W        perform write test (write 10kB to UART and output average speed)\n");
	fprintf(stderr, "  -S SIZE   set different r/w test size (in bytes)\n");
	fprintf(stderr, "  -b BAUD   set baud, default 9600\n");
	fprintf(stderr, "  -p PARITY set parity (default 0=none, 1=even, 2=odd)\n");
	fprintf(stderr, "  -B BITS   set byte size in bits, default 8\n");
	fprintf(stderr, "  -s BITS   set stop bit count, default 1\n");
	fprintf(stderr, "\n");
	fprintf(stderr, "If you want to use it as interactive terminal, use %s -rw -b 9600\n", name);
	exit(0);
}

int main(int argc, char** argv){
	if(argc==1){
		usage(argv[0]);
	}
	int baud=9600;
	int parity=USBASP_UART_PARITY_NONE;
	int bits=USBASP_UART_BYTES_8B;
	int stop=USBASP_UART_STOP_1BIT;
	
	bool should_test_read=false;
	bool should_test_write=false;
	bool should_read=false;
	bool should_write=false;
	int test_size=(10*1024);

	opterr=0;
	int c;

	while( (c=getopt(argc, argv, "rwRWS:b:p:B:s:"))!=-1){
		switch(c){
		case 'r':
			should_read=true;
			break;
		case 'w':
			should_write=true;
			break;
		case 'R':
			should_test_read=true;
			break;
		case 'W':
			should_test_write=true;
			break;
		case 'S':
			sscanf(optarg, "%d", &test_size);
			break;
		case 'b':
			sscanf(optarg, "%d", &baud);
			break;
		case 'p':
			sscanf(optarg, "%d", &parity);
			switch(parity){
			default: fprintf(stderr, "Bad parity, falling back to default.\n");
			case 0:	parity=USBASP_UART_PARITY_NONE;	break;
			case 1:	parity=USBASP_UART_PARITY_EVEN;	break;
			case 2:	parity=USBASP_UART_PARITY_ODD ;	break;
			}
			break;
		case 'B':
			sscanf(optarg, "%d", &bits);
			switch(bits){
			case 5:	bits=USBASP_UART_BYTES_5B;	break;
			case 6:	bits=USBASP_UART_BYTES_6B;	break;
			case 7:	bits=USBASP_UART_BYTES_7B;	break;
			default: fprintf(stderr, "Bad byte size, falling back to default.\n");
			case 8:	bits=USBASP_UART_BYTES_8B;	break;
			case 9:	bits=USBASP_UART_BYTES_9B;	break;
			}
			break;
		case 's':
			sscanf(optarg, "%d", &stop);
			switch(stop){
			default: fprintf(stderr, "Bad stop bit count, falling back to default.\n");
			case 1: stop=USBASP_UART_STOP_1BIT;	break;
			case 2: stop=USBASP_UART_STOP_2BIT;	break;
			}
			break;
		default:
			usage(argv[0]);
			break;
		}
	}

	USBasp_UART usbasp;
	int rv;
	if((rv=usbasp_uart_config(&usbasp, baud, parity | bits | stop)) < 0){
		fprintf(stderr, "Error %d while initializing USBasp\n", rv);
		if(rv==USBASP_NO_CAPS){
			fprintf(stderr, "USBasp has no UART capabilities.\n");
		}
	}
	if(should_test_read){
		fprintf(stderr, "Reading...\n");
		readTest(&usbasp, test_size);
	}
	if(should_test_write){
		fprintf(stderr, "Writing...\n");
		writeTest(&usbasp, test_size);
	}
	std::vector<std::thread> threads;
	if(should_read){
		threads.push_back(std::thread([&]{read_forever(&usbasp);}));
	}
	if(should_write){
		threads.push_back(std::thread([&]{write_forever(&usbasp);}));
	}
	for(auto& thr : threads){
		thr.join();
	}
}
