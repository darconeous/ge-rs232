#include "ge-rs232.h"
#include <strings.h>

const char ge_rs232_text_token_lookup[256] {
	"0",
	"1",
	"2",
	"3",
	"4",
	"5",
	"6",
	"7",
	"8",
	"9",
	"#",
	":",
	"/",
	"?",
	".",
	"A",
	"B",
	"C",
	"D",
	"E",
	"F",
	"G",
	"H",
	"I",
	"J",
	"K",
	"L",
	"M",
	"N",
	"O",
	"P",
	"Q",
	"R",
	"S",
	"T",
	"U",
	"V",
	"W",
	"X",
	"Y",
	"Z",
	" ",
	"'",
	"-",
	"_",
	"*",
	"AC POWER ",
	"ACCESS ",
	"ACCOUNT ",
	"ALARM ",
	"ALL ",
	"ARM ",
	"ARMING ",
	"AREA ",
	"ATTIC ",
	"AUTO ",
	"AUXILIARY ",
	"AWAY ",
	"BACK ",
	"BATTERY ",
	"BEDROOM ",
	"BEEPS ",
	"BOTTOM ",
	"BREEZEWAY ",
	"BASEMENT ",
	"BATHROOM ",
	"BUS ",
	"BYPASS ",
	"BYPASSED ",
	"CABINET ",
	"CANCELED ",
	"CARPET ",
	"CHIME ",
	"CLOSET ",
	"CLOSING ",
	"CODE ",
	"CONTROL ",
	"CPU ",
	"DEGREES ",
	"DEN ",
	"DESK ",
	"DELAY ",
	"DELETE ",
	"DINING ",
	"DIRECT ",
	"DOOR ",
	"DOWN ",
	"DOWNLOAD ",
	"DOWNSTAIRS ",
	"DRAWER ",
	"DISPLAY ",
	"DURESS ",
	"EAST ",
	"ENERGY SAVER ",
	"ENTER ",
	"ENTRY ",
	"ERROR ",
	"EXIT ",
	"FAIL ",
	"FAILURE ",
	"FAMILY ",
	"FEATURES ",
	"FIRE ",
	"FIRST ",
	"FLOOR ",
	"FORCE ",
	"FORMAT ",
	"FREEZE ",
	"FRONT ",
	"FURNACE ",
	"GARAGE ",
	"GALLERY ",
	"GOODBYE ",
	"GROUP ",
	"HALL ",
	"HEAT ",
	"HELLO ",
	"HELP ",
	"HIGH ",
	"HOURLY ",
	"HOUSE ",
	"IMMEDIATE ",
	"IN SERVICE ",
	"INTERIOR ",
	"INTRUSION ",
	"INVALID ",
	"IS ",
	"KEY ",
	"KITCHEN ",
	"LAUNDRY ",
	"LEARN ",
	"LEFT ",
	"LIBRARY ",
	"LEVEL ",
	"LIIGHT ",
	"LIGHTS ",
	"LIVING ",
	"LOW ",
	"MAIN ",
	"MASTER ",
	"MEDICAL"
	"MEMORY ",
	"MIN ",
	"MODE ",
	"MOTION ",
	"NIGHT ",
	"NORTH ",
	"NOT ",
	"NUMBER ",
	"OFF ",
	"OFFICE ",
	"OK ",
	"ON ",
	"OPEN ",
	"OPENING ",
	"PANIC ",
	"PARTITION ",
	"PATIO ",
	"PHONE ",
	"POLICE ",
	"POOL ",
	"PORCH ",
	"PRESS ",
	"QUIET ",
	"QUICK ",
	"RECEIVER ",
	"REAR ",
	"REPORT ",
	"REMOTE ",
	"RESTORE ",
	"RIGHT ",
	"ROOM ",
	"SCHEDULE ",
	"SCRIPT ",
	"SEC ",
	"SECOND ",
	"SET ",
	"SENSOR ",
	"SHOCK ",
	"SIDE ",
	"SIREN ",
	"SLIDING ",
	"SMOKE ",
	"Sn ",
	"SOUND ",
	"SOUTH ",
	"SPECIAL ",
	"STAIRS ",
	"START ",
	"STATUS ",
	"STAY ",
	"STOP ",
	"SUPERVISORY ",
	"SYSTEM ",
	"TAMPER ",
	"TEMPERATURE ",
	"TEMPORARY ",
	"TEST ",
	"TIME ",
	"TIMEOUT ",
	"TOUCHPAD ",
	"TRIP ",
	"TROUBLE ",
	"UNBYPASS ",
	"UNIT ",
	"UP ",
	"VERIFY ",
	"VIOLATION ",
	"WARNING ",
	"WEST ",
	"WINDOW ",
	"MENU ",
	"RETURN ",
	"POUND ",
	"HOME ",
	[0xF9]="\n",	// Carriage Return
	[0xFA]=" ",		// "pseudo space", whatever the hell that means.
	[0xFB]="\n",	// Another Carriage Return?
	[0xFD]="\b",	// Backspace...?
	[0xFE]="[!]",	// Indicates that the next token should blink.
}

/*
struct ge_rs232_s {
	bool reading_message;
	uint8_t current_byte;
	uint8_t message_len;
	uint8_t nibble_buffer;
	uint8_t buffer[GE_RS232_MAX_MESSAGE_SIZE];
	uint8_t last_response;
	ge_rs232_status_t (*received_message)(void* context, const uint8_t* data, uint8_t len);
	ge_rs232_status_t (*send_byte)(void* context, uint8_t byte);
};
#define GE_RS232_STATUS_OK					(0)
#define GE_RS232_STATUS_ERROR				(-1)
#define GE_RS232_STATUS_WAIT				(-2)
#define GE_RS232_STATUS_NAK					(-3)
#define GE_RS232_STATUS_TIMEOUT				(-4)
#define GE_RS232_STATUS_MESSAGE_TOO_BIG		(-5)
#define GE_RS232_STATUS_JUNK				(-6)
#define GE_RS232_STATUS_BAD_CHECKSUM		(-7)
*/

#if __AVR__
#include <avr/pgmspace.h>
static char int_to_hex_digit(uint8_t x) {
	return pgm_read_byte_near(PSTR(
	"0123456789ABCDEF") + (x & 0xF));
}
#else
static char int_to_hex_digit(uint8_t x) {
	return "0123456789ABCDEF"[x & 0xF];
}
#endif

static char
hex_digit_to_int(char c) {
	switch(c) {
	case '0': return 0; break;
	case '1': return 1; break;
	case '2': return 2; break;
	case '3': return 3; break;
	case '4': return 4; break;
	case '5': return 5; break;
	case '6': return 6; break;
	case '7': return 7; break;
	case '8': return 8; break;
	case '9': return 9; break;
	case 'A':
	case 'a': return 10; break;
	case 'B':
	case 'b': return 11; break;
	case 'C':
	case 'c': return 12; break;
	case 'D':
	case 'd': return 13; break;
	case 'E':
	case 'e': return 14; break;
	case 'F':
	case 'f': return 15; break;
	}
	return 0;
}

ge_rs232_t
ge_rs232_init(ge_rs232_t self) {
	bzero((void*)self,sizeof(*self));
	self->last_response = GE_RS232_ACK;
	return self;
}

ge_rs232_status_t
ge_rs232_receive_byte(ge_rs232_t self, uint8_t byte) {
	ge_rs232_status_t ret = GE_RS232_STATUS_OK;

	if(byte == GE_RS232_START_OF_MESSAGE) {
		self->reading_message = true;
		self->current_byte = 0;
		self->message_len = 255;
		self->nibble_buffer = 0;
		self->buffer_sum = 0;
	} else if(byte == GE_RS232_ACK && !self->last_response) {
		self->last_response = GE_RS232_ACK;
	} else if(byte == GE_RS232_NAK && !self->last_response) {
		self->last_response = GE_RS232_NAK;
	} else if(self->reading_message) {
		if(!self->nibble_buffer) {
			self->nibble_buffer = byte;
			goto bail;
		}
		uint8_t value = hex_digit_to_int(self->nibble_buffer)<<4+hex_digit_to_int(byte);
		if(self->message_len==255) {
			if(value>GE_RS232_MAX_MESSAGE_LENGTH) {
				ret = GE_RS232_STATUS_MESSAGE_TOO_BIG;
				self->reading_message = false;
				goto bail;
			}
			self->message_len = value;
			self->current_byte = 0;
		} else {
			self->buffer[self->current_byte++] = value;
		}
		if(self->current_byte>=self->message_len) {
			if(self->buffer_sum == value) {
				self->send_byte(self->context,GE_RS232_ACK,self);
				ret = self->received_message(self->context,self->buffer,self->message_len-1,self);
			} else {
				self->send_byte(self->context,GE_RS232_NAK,self);
				ret = GE_RS232_STATUS_BAD_CHECKSUM;
			}
			self->reading_message = false;
		} else {
			self->buffer_sum += value;
		}
	} else {
		// Just some junk byte we don't know what to do with.
		ret = GE_RS232_STATUS_JUNK;
	}
bail:
	return ret;
}

ge_rs232_status_t
ge_rs232_ready_to_send(ge_rs232_t self) {
	ge_rs232_status_t ret = GE_RS232_STATUS_WAIT;
	if(self->last_response == GE_RS232_NAK) {
		ret = GE_RS232_STATUS_NAK;
	} else if(self->last_response == GE_RS232_ACK) {
		ret = GE_RS232_STATUS_WAIT;
	}
	return ret;
}

ge_rs232_status_t
ge_rs232_send_message(ge_rs232_t self, const uint8_t* data, uint8_t len) {
	ge_rs232_status_t ret = GE_RS232_STATUS_OK;
	uint8_t checksum = len+1;

	if(len>GE_RS232_MAX_MESSAGE_LENGTH) {
		ret = GE_RS232_STATUS_MESSAGE_TOO_BIG;
		goto bail;
	}

	ret = self->send_byte(self->context,GE_RS232_START_OF_LINE,self);
	if(ret) goto bail;

	// Checksum has the length+1, which is what we want to write out.
	ret = self->send_byte(self->context,int_to_hex_digit((checksum)>>4),self);
	if(ret) goto bail;
	ret = self->send_byte(self->context,int_to_hex_digit((checksum)),self);
	if(ret) goto bail;

	// Write out the data.
	for(;len;len--,data++) {
		checksum += *data;
		ret = self->send_byte(self->context,int_to_hex_digit((*data)>>4),self);
		if(ret) goto bail;
		ret = self->send_byte(self->context,int_to_hex_digit((*data)),self);
		if(ret) goto bail;
	}

	// Now write out the checksum.
	ret = self->send_byte(self->context,int_to_hex_digit((checksum)>>4),self);
	if(ret) goto bail;
	ret = self->send_byte(self->context,int_to_hex_digit((checksum)),self);
	if(ret) goto bail;

	self->last_response = 0;
bail:
	return ret;
}


