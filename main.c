#include <stdio.h>
#include "ge-rs232.h"
#include <string.h>

ge_rs232_status_t refresh_equipment_list(ge_rs232_t interface) {
	uint8_t msg[] = {
		GE_RS232_ATP_EQUIP_LIST_REQUEST,
		0x03,
	};
	return ge_rs232_send_message(interface,msg,sizeof(msg));
}

ge_rs232_status_t dynamic_data_refresh(ge_rs232_t interface) {
	uint8_t msg[] = {
		GE_RS232_ATP_DYNAMIC_DATA_REFRESH,
	};
	return ge_rs232_send_message(interface,msg,sizeof(msg));
}

ge_rs232_status_t toggle_chime(ge_rs232_t interface) {
	uint8_t msg[] = {
		GE_RS232_ATP_KEYPRESS,
		0x01,	// Partition
		0x00,	// Area
		0x07,	// Keypad "7"
		0x01,	// Keypad "1"
	};
	return ge_rs232_send_message(interface,msg,sizeof(msg));
}

ge_rs232_status_t
received_message(void* context, const uint8_t* data, uint8_t len,struct ge_rs232_s* interface) {
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
#define GE_RS232_ZONE_STATUS_TRIPPED		(1<<0)
#define GE_RS232_ZONE_STATUS_FAULT			(1<<1)
#define GE_RS232_ZONE_STATUS_ALARM			(1<<2)
#define GE_RS232_ZONE_STATUS_TROUBLE		(1<<3)
#define GE_RS232_ZONE_STATUS_BYPASSED		(1<<4)

	static uint8_t last_msg[GE_RS232_MAX_MESSAGE_SIZE];
	static uint8_t last_msg_len;

	if(data[0]==GE_RS232_PTA_SUBCMD && (
		data[1]==GE_RS232_PTA_SUBCMD_SIREN_SYNC
	)) {
		return GE_RS232_STATUS_OK;
	}

	if(data[0]==GE_RS232_PTA_SUBCMD &&
		data[1]==GE_RS232_PTA_SUBCMD_TOUCHPAD_DISPLAY &&
		data[2]!=1
	) {
		return GE_RS232_STATUS_OK;
	}

	if(last_msg_len == len && 0==memcmp(data,last_msg,len)) {
		return GE_RS232_STATUS_OK;
	}
	memcpy(last_msg,data,len);
	last_msg_len = len;

	if(data[0]==GE_RS232_PTA_AUTOMATION_EVENT_LOST) {
		fprintf(stderr,"[AUTOMATION_EVENT_LOST]");
		ge_rs232_status_t status = ge_rs232_ready_to_send(interface);
		if(status != GE_RS232_STATUS_WAIT) {
			ge_rs232_status_t status = dynamic_data_refresh(interface);
			fprintf(stderr," refresh send status = %d",status);
		} else {
			fprintf(stderr," UNABLE TO SEND REFRESH: NOT READY");
		}
		len=0;
	} else if(data[0]==GE_RS232_PTA_ZONE_STATUS) {
		fprintf(stderr,"[ZONE_STATUS]");
		int zone = (data[3]<<8)+data[4];
		fprintf(stderr," PN:%d AREA:%d ZONE:%d STATUS:",data[1],data[2],zone);
		if(data[5]&GE_RS232_ZONE_STATUS_TRIPPED)
			fprintf(stderr,"[TRIPPED]");
		if(data[5]&GE_RS232_ZONE_STATUS_FAULT)
			fprintf(stderr,"[FAULT]");
		if(data[5]&GE_RS232_ZONE_STATUS_ALARM)
			fprintf(stderr,"[ALARM]");
		if(data[5]&GE_RS232_ZONE_STATUS_TROUBLE)
			fprintf(stderr,"[TROUBLE]");
		if(data[5]&GE_RS232_ZONE_STATUS_BYPASSED)
			fprintf(stderr,"[BYPASSED]");
		len = 0;
	} else if(data[0]==GE_RS232_PTA_EQUIP_LIST_ZONE_DATA) {
		fprintf(stderr,"[ZONE_INFO]");
		int zone = (data[4]<<8)+data[5];
		fprintf(stderr," PN:%d AREA:%d ZONE:%d TYPE:%d GROUP:%d STATUS:",data[1],data[2],zone,data[3]);
		if(data[7]&GE_RS232_ZONE_STATUS_TRIPPED)
			fprintf(stderr,"[TRIPPED]");
		if(data[7]&GE_RS232_ZONE_STATUS_FAULT)
			fprintf(stderr,"[FAULT]");
		if(data[7]&GE_RS232_ZONE_STATUS_ALARM)
			fprintf(stderr,"[ALARM]");
		if(data[7]&GE_RS232_ZONE_STATUS_TROUBLE)
			fprintf(stderr,"[TROUBLE]");
		if(data[7]&GE_RS232_ZONE_STATUS_BYPASSED)
			fprintf(stderr,"[BYPASSED]");
		fprintf(stderr," TEXT:\"");
		len-=8;
		data+=8;
		while(len--) {
			const char* str = ge_rs232_text_token_lookup[*data++];
			if(str) {
				if(str[0]=='\n') {
					if(len)
						fprintf(stderr," | ");
				} else {
					fprintf(stderr,"%s",str);
				}
			} else {
				fprintf(stderr,"\\x%02x",data[-1]);
			}
		}
		len=0;
		fprintf(stderr,"\"");

	} else if(data[0]==GE_RS232_PTA_CLEAR_AUTOMATION_DYNAMIC_IMAGE) {
		fprintf(stderr,"[CLEAR_AUTOMATION_DYNAMIC_IMAGE]");
		len = 0;

	} else if(data[0]==GE_RS232_PTA_PANEL_TYPE) {
		fprintf(stderr,"[PANEL_TYPE]");
		/*
		ge_rs232_status_t status = ge_rs232_ready_to_send(interface);
		if(status != GE_RS232_STATUS_WAIT) {
			ge_rs232_status_t status = refresh_equipment_list(interface);
			fprintf(stderr," refresh send status = %d",status);
		} else {
			fprintf(stderr," UNABLE TO SEND REFRESH: NOT READY");
		}
		*/
	} else if(data[0]==GE_RS232_PTA_SUBCMD) {
		char *str = NULL;
		switch(data[1]) {
			case GE_RS232_PTA_SUBCMD_LEVEL:
				fprintf(stderr,"[ARMING_LEVEL]");
				fprintf(stderr," PN:%d AN:%d",data[2],data[3]);
				fprintf(stderr," UNh:%d UNl:%d AL:%d",data[4],data[5],data[6]);
				len=0;
				break;
			case GE_RS232_PTA_SUBCMD_ALARM_TROUBLE:
				fprintf(stderr,"[ALARM/TROUBLE]");
				fprintf(stderr," PN:%d AN:%d",data[2],data[3]);
				fprintf(stderr," ST:%d",data[4]);
				fprintf(stderr," ZONE:%d",data[7]);
				fprintf(stderr," ALARM:%d.%d",data[8],data[9]);
				switch(data[8]) {
					case 1: // General Alarm
					case 2: // Alarm Canceled
						asprintf(&str,"/home/pi/bin/report-alarm %d %d %d",data[8],data[9],data[7]);
						break;
					case 15: // System Trouble
					default: break;
				}
				if(str) {
					fprintf(stderr," ");
					system(str);
					free(str);
				}
				break;
			case GE_RS232_PTA_SUBCMD_ENTRY_EXIT_DELAY:
				fprintf(stderr,"[EXIT_DELAY]");
				fprintf(stderr," PN:%d AN:%d",data[2],data[3]);
				break;
			case GE_RS232_PTA_SUBCMD_SIREN_SETUP:
				fprintf(stderr,"[SIREN_SETUP]");
				fprintf(stderr," PN:%d AN:%d",data[2],data[3]);
				break;
			case GE_RS232_PTA_SUBCMD_SIREN_SYNC:
//				fprintf(stderr,"[SIREN_SYNC]");
				return GE_RS232_STATUS_OK;
				len=0;
				break;
			case GE_RS232_PTA_SUBCMD_SIREN_GO:
				fprintf(stderr,"[SIREN_GO]");
				len=0;
				break;
			case GE_RS232_PTA_SUBCMD_TOUCHPAD_DISPLAY:
				if(data[2]!=1)
					return GE_RS232_STATUS_OK;
				fprintf(stderr,"[TOUCHPAD_DISPLAY]");
				fprintf(stderr," PN:%d AN:%d",data[2],data[3]);
				fprintf(stderr," MT:%d MSG:\"",data[4]);
				len-=5;
				data+=5;
				while(len--) {
					const char* str = ge_rs232_text_token_lookup[*data++];
					if(str) {
						if(str[0]=='\n') {
							if(len)
								fprintf(stderr," | ");
						} else {
							fprintf(stderr,"%s",str);
						}
					} else {
						fprintf(stderr,"\\x%02x",data[-1]);
					}
				}
				len=0;
				fprintf(stderr,"\"");
				break;
			case GE_RS232_PTA_SUBCMD_SIREN_STOP:
				fprintf(stderr,"[SIREN_STOP]");
				len=0;
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
	ge_rs232_status_t status;

	setvbuf(stdout, NULL, _IONBF, 0);
	setvbuf(stdin, NULL, _IONBF, 0);

	uint8_t byte = 0;
	//refresh_equipment_list(interface);
	//toggle_chime(interface);
	//dynamic_data_refresh(interface);

	for(byte = fgetc(stdin);!feof(stdin);byte=fgetc(stdin)) {
		if(byte==GE_RS232_NAK) {
			fprintf(stderr,"GOT NAK\n");
		}
		if(byte==GE_RS232_ACK) {
			fprintf(stderr,"GOT ACK\n");
		}
		status = ge_rs232_receive_byte(interface,byte);
		if(status==GE_RS232_STATUS_NAK)
			fprintf(stderr,"N\n");
		else if(status==GE_RS232_STATUS_JUNK)
			fprintf(stderr,"X");
		else if(status)
			fprintf(stderr,"[%d]",status);


		status = ge_rs232_ready_to_send(interface);
		if(status && status != GE_RS232_STATUS_WAIT ) {
			fprintf(stderr,"{%d}",status);
			dynamic_data_refresh(interface);
			//refresh_equipment_list(interface);
		}
		fflush(stderr);
	}

	return 0;
}
