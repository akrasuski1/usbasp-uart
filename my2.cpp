#include <cstdio>
#include <cstring>
#include <cstdarg>
#include <cstdint>
#include <string>

#include <stdexcept>

#include "../usbasp.2011-05-28/firmware/usbasp.h"

#include <libusb-1.0/libusb.h>


#define USB_ERROR_NOTFOUND  1
#define USB_ERROR_ACCESS    2
#define USB_ERROR_IO        3

#define	USBASP_SHARED_VID   0x16C0
#define	USBASP_SHARED_PID   0x05DC

#define dprintf(...) printf(__VA_ARGS__)

class USBasp_UART{
public:
	USBasp_UART(){
		if(open() != 0){
			throw std::runtime_error("Unable to open USBasp");
		}
		uint32_t caps=capabilities();
		dprintf("Caps: %x\n", caps);
		if(!(caps & USBASP_CAP_6_UART)){
			throw std::runtime_error("USBasp doesn't have UART capabilities.");
		}
	}
	~USBasp_UART(){
		disable();
		libusb_close(usbhandle);
	}
	void config(int baud, int flags){
		uint8_t send[4];
		int presc=12000000/8/baud - 1;
		dprintf("Baud prescaler: %d\n", presc);
		send[1]=presc>>8;
		send[0]=presc&0xFF;
		send[2]=flags&0xFF;
		transmit(true, USBASP_FUNC_UART_CONFIG, send, dummy, sizeof(dummy));
	}
	void flushrx(){
		transmit(true, USBASP_FUNC_UART_FLUSHRX, dummy, dummy, sizeof(dummy));
	}
	void flushtx(){
		transmit(true, USBASP_FUNC_UART_FLUSHTX, dummy, dummy, sizeof(dummy));
	}
	void disable(){
		transmit(true, USBASP_FUNC_UART_DISABLE, dummy, dummy, sizeof(dummy));
	}
	int read(uint8_t* buff, int len){
		return transmit(true, USBASP_FUNC_UART_RX, dummy, buff, len);
	}
	int write(uint8_t* buff, int len){
		uint8_t tmp[2];
		transmit(true,  USBASP_FUNC_UART_TX_FREE, dummy, tmp, 2);
		int avail=(tmp[0]<<8)|tmp[1];
		if(len>avail){
			len=avail;
		}
		dprintf("Received free=%d, transmitting %d bytes\n", avail, len);
		if(len<=0){
			return 0;
		}
		return transmit(false, USBASP_FUNC_UART_TX, dummy, buff, len);
	}
	int writeAll(uint8_t* buff, int len){
		int i=0;
		while(i<len){
			int rv=write(buff+i, len-i);
			if(rv<0){ return rv; }
			i+=rv;
		}
		return len;
	}

private:
	int open(){
		int errorCode = USB_ERROR_NOTFOUND;

		libusb_context* ctx;
		libusb_init(&ctx);

		libusb_device** dev_list;
		int dev_list_len = libusb_get_device_list(ctx, &dev_list);

		for (int j=0; j<dev_list_len; ++j) {
			libusb_device* dev = dev_list[j];
			libusb_device_descriptor descriptor;
			libusb_get_device_descriptor(dev, &descriptor);
			if (descriptor.idVendor == USBASP_SHARED_VID 
					&& descriptor.idProduct == USBASP_SHARED_PID) {
				uint8_t str[256];
				libusb_open(dev, &usbhandle);
				if (!usbhandle) {
					errorCode = USB_ERROR_ACCESS;
					continue;
				}
				libusb_get_string_descriptor_ascii(usbhandle, 
						descriptor.iManufacturer & 0xff, str, sizeof(str));
				dprintf("%s\n", str);
				libusb_get_string_descriptor_ascii(usbhandle, 
						descriptor.iProduct & 0xff, str, sizeof(str));
				if(std::string("USBasp")!=(const char*)str){
					libusb_close(usbhandle);
					usbhandle=NULL;
					continue;
				}
				dprintf("%s\n", str);
				
				break;
			}
		}
		libusb_free_device_list(dev_list,1);
		if (usbhandle != NULL){
			errorCode = 0;
		}
		return errorCode;
	}

	uint32_t capabilities(){
		uint8_t res[4];
		uint8_t tmp[4];
		uint32_t ret=0;
		if(transmit(true, USBASP_FUNC_GETCAPABILITIES, tmp, res, sizeof(res)) == 4){
			 ret = res[0] | ((uint32_t)res[1] << 8) | ((uint32_t)res[2] << 16) | ((uint32_t)res[3] << 24);
		}
		return ret;
	}

	int transmit(uint8_t receive, uint8_t functionid, const uint8_t* send,
			uint8_t* buffer, uint16_t buffersize){
		return libusb_control_transfer(usbhandle,
				(LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_RECIPIENT_DEVICE | (receive << 7)) & 0xff,
				functionid, 
				((send[1] << 8) | send[0]), 
				((send[3] << 8) | send[2]), 
				buffer, 
				buffersize,
				5000); // 5s timeout.
	}

	libusb_device_handle* usbhandle;
	uint8_t dummy[4];
};

#include <unistd.h>
void writeTest(USBasp_UART& usbasp){
	char c='a';
	while(1){
		c++;
		if(c>'z'){ c='a'; }
		char s[]="Hello world! I love how nice you are :) This is just some longer text"
			" so that I can truly check the speed. I'll add some more text just in case"
			" it matters. 1234567890qwertyuiopasdfghjklzxcvbnm ABCDEFGHIJKLMNOPQRSTUVWX"
			"YZ. This text has ~240 B!";
		s[1]=c;
		int rv=usbasp.writeAll((uint8_t*)s, sizeof(s));
		printf("%d %c\n", rv, c);
		//usleep(100000);
	}
}
void readTest(USBasp_UART& usbasp){
	while(1){
		usleep(100000);
		uint8_t s[200];
		int rv=usbasp.read(s, sizeof(s));
		if(rv==0){ continue; }
		printf("rv=%d\n", rv);
		for(int i=0;i<rv;i++){
			printf("%x ",s[i]);
		}
		printf("\n");
		for(int i=0;i<rv;i++){
			printf("%c", s[i]);
		}
		printf("\n");
	}
}
int main(){
	try{
		USBasp_UART usbasp;
		usbasp.config(9600, 
				USBASP_UART_PARITY_NONE | 
				USBASP_UART_BYTES_8B | 
				USBASP_UART_STOP_1BIT);
		readTest(usbasp);
	}
	catch(std::runtime_error& e){
		printf("Critical error: %s\n", e.what());
	}
}
