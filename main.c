#include <stdio.h>
#include "ge-rs232.h"

ge_rs232_status_t
received_message(void* context, const uint8_t* data, uint8_t len,struct ge_rs232_s* instance) {
/*
#define GE_RS232_PTA_PANEL_TYPE             (0x01)
#define GE_RS232_PTA_AUTOMATION_EVENT_LOST  (0x02)
#define GE_RS232_PTA_EQUIP_LIST_ZONE_DATA   (0x03)
#define GE_RS232_PTA_EQUIP_LIST_PARTITION_DATA  (0x04)
#define GE_RS232_PTA_EQUIP_LIST_SUPERBUS_DEV_DATA   (0x05)
#define GE_RS232_PTA_EQUIP_LIST_SUPERBUS_CAP_DATA   (0x06)
#define GE_RS232_PTA_EQUIP_LIST_OUTPUT_DATA (0x07)
#define GE_RS232_PTA_EQUIP_LIST_USER_DATA   (0x09)
#define GE_RS232_PTA_EQUIP_LIST_SCHEDULE_DATA   (0x0A)
#define GE_RS232_PTA_EQUIP_LIST_SCHEDULED_EVENT_DATA    (0x0B)
#define GE_RS232_PTA_EQUIP_LIST_LIGHT_TO_SENSOR_DATA    (0x0C)
#define GE_RS232_PTA_EQUIP_LIST_COMPLETE    (0x08)
#define GE_RS232_PTA_CLEAR_AUTOMATION_DYNAMIC_IMAGE (0x20)
#define GE_RS232_PTA_ZONE_STATUS            (0x21)
#define GE_RS232_PTA_SUBCMD                 (0x22)
#   define GE_RS232_PTA_SUBCMD_LEVEL        (0x01)
#   define GE_RS232_PTA_SUBCMD_ALARM_TROUBLE    (0x02)
#   define GE_RS232_PTA_SUBCMD_ENTRY_EXIT_DELAY (0x03)
#   define GE_RS232_PTA_SUBCMD_SIREN_SETUP  (0x04)
#   define GE_RS232_PTA_SUBCMD_SIREN_SYNC   (0x05)
#   define GE_RS232_PTA_SUBCMD_SIREN_GO     (0x06)
#   define GE_RS232_PTA_SUBCMD_TOUCHPAD_DISPLAY     (0x09)
#   define GE_RS232_PTA_SUBCMD_SIREN_STOP   (0x0B)
#   define GE_RS232_PTA_SUBCMD_FEATURE_STATE    (0x0C)
#   define GE_RS232_PTA_SUBCMD_TEMPERATURE  (0x0D)
#   define GE_RS232_PTA_SUBCMD_TIME_AND_DATE    (0x0E)
#define GE_RS232_PTA_SUBCMD2                    (0x23)
#   define GE_RS232_PTA_SUBCMD2_LIGHTS_STATE    (0x01)
#   define GE_RS232_PTA_SUBCMD2_USER_LIGHTS (0x02)
#   define GE_RS232_PTA_SUBCMD2_KEYFOB      (0x03)

*/
	if(data[0]==GE_RS232_PTA_ZONE_STATUS) {
		fprintf(stderr,"[ZONE_STATUS]");
	} else if(data[0]==GE_RS232_PTA_CLEAR_AUTOMATION_DYNAMIC_IMAGE) {
		fprintf(stderr,"[CLEAR_AUTOMATION_DYNAMIC_IMAGE]");
	} else if(data[0]==GE_RS232_PTA_PANEL_TYPE) {
		fprintf(stderr,"[PANEL_TYPE]");
	} else if(data[0]==GE_RS232_PTA_SUBCMD) {
		switch(data[1]) {
			case GE_RS232_PTA_SUBCMD_LEVEL:
				fprintf(stderr,"[LEVEL]");
				break;
			case GE_RS232_PTA_SUBCMD_ALARM_TROUBLE:
				fprintf(stderr,"[ALARM/TROUBLE]");
				break;
			case GE_RS232_PTA_SUBCMD_ENTRY_EXIT_DELAY:
				fprintf(stderr,"[EXIT_DELAY]");
				break;
			case GE_RS232_PTA_SUBCMD_SIREN_SETUP:
				fprintf(stderr,"[SIREN_SETUP]");
				break;
			case GE_RS232_PTA_SUBCMD_SIREN_SYNC:
				fprintf(stderr,"[SIREN_SYNC]");
				break;
			case GE_RS232_PTA_SUBCMD_SIREN_GO:
				fprintf(stderr,"[SIREN_GO]");
				break;
			case GE_RS232_PTA_SUBCMD_TOUCHPAD_DISPLAY:
				fprintf(stderr,"[TOUCHPAD_DISPLAY] PN:%d AN:%d MT:%d",data[2],data[3],data[4]);
				len-=5;
				data+=5;
				fprintf(stderr," MSG:\"");
				while(len--) {
					fprintf(stderr,"%s",ge_rs232_text_token_lookup[*data++]);
				}
				len=0;
				fprintf(stderr,"\"");
				break;
			case GE_RS232_PTA_SUBCMD_SIREN_STOP:
				fprintf(stderr,"[SIREN_STOP]");
				break;
			case GE_RS232_PTA_SUBCMD_FEATURE_STATE:
				fprintf(stderr,"[FEATURE_STATE]");
				break;
			case GE_RS232_PTA_SUBCMD_TEMPERATURE:
				fprintf(stderr,"[TEMPERATURE]");
				break;
			case GE_RS232_PTA_SUBCMD_TIME_AND_DATE:
				fprintf(stderr,"[TIME_AND_DATE]");
				break;
		}
	}

	if(len) {
		fprintf(stderr," { ");
		while(len--) {
			fprintf(stderr,"%02X ",*data++);
		}
		fprintf(stderr,"}");
	}
	fprintf(stderr,"\n");

	return GE_RS232_STATUS_OK;
}

ge_rs232_status_t send_byte(void* context, uint8_t byte,struct ge_rs232_s* instance) {
	fprintf(stdout,"%c",(char)byte);
	fflush(stdout);
	return GE_RS232_STATUS_OK;
}

int main(int argc, const char* argv[]) {
	fprintf(stderr,"works\n");

	struct ge_rs232_s interface_data;
	ge_rs232_t interface = ge_rs232_init(&interface_data);
	interface->received_message = &received_message;
	interface->send_byte = &send_byte;
	uint8_t byte = GE_RS232_ATP_DYNAMIC_DATA_REFRESH;
	ge_rs232_status_t status;

	setvbuf(stdout, NULL, _IONBF, 0);
	setvbuf(stdin, NULL, _IONBF, 0);

	ge_rs232_send_message(interface,&byte,1);
	fprintf(stdout,"%c",(char)GE_RS232_NAK);
	fflush(stderr);

	for(byte = fgetc(stdin);!feof(stdin);byte=fgetc(stdin)) {
		status = ge_rs232_receive_byte(interface,byte);
		if(status==GE_RS232_STATUS_NAK)
			fprintf(stderr,"N\n");
		else if(status==GE_RS232_STATUS_JUNK)
			fprintf(stderr,"X");
		else if(status)
			fprintf(stderr,"[%d]",status);

		fflush(stderr);
	}

	return 0;
}
