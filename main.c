#include <stdio.h>
#include "ge-rs232.h"

ge_rs232_status_t
received_message(void* context, const uint8_t* data, uint8_t len,struct ge_rs232_s* instance) {
	fprintf(stderr,"Got message, type %d\n",data[0]);
	return GE_RS232_STATUS_OK;
}

ge_rs232_status_t send_byte(void* context, uint8_t byte,struct ge_rs232_s* instance) {
	fprintf(stdout,"%c",(char)byte);
	return GE_RS232_STATUS_OK;
}

int main(int argc, const char* argv[]) {
	fprintf(stderr,"works\n");

	struct ge_rs232_s interface_data;
	ge_rs232_t interface = ge_rs232_init(&interface_data);
	interface->received_message = &received_message;
	interface->send_byte = &send_byte;
	uint8_t byte;

	for(byte = fgetc(stdin);!feof(stdin);byte=fgetc(stdin)) {
		ge_rs232_receive_byte(interface,byte);
	}

	return 0;
}
