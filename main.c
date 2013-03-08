
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "ge-rs232.h"
#include <poll.h>
#include <termios.h>
#include <stdarg.h>
#include <fcntl.h>

#define USE_SYSLOG		1

#if USE_SYSLOG
#include <syslog.h>
#endif

#define LOG_LEVEL_EMERGENCY		(0)
#define LOG_LEVEL_ALERT		(1)
#define LOG_LEVEL_CRITICAL	(2)
#define LOG_LEVEL_ERROR		(3)
#define LOG_LEVEL_WARNING		(4)
#define LOG_LEVEL_NOTICE		(5)
#define LOG_LEVEL_INFO		(6)
#define LOG_LEVEL_DEBUG		(7)

int current_log_level = LOG_LEVEL_INFO;

void log_msg(int level,const char* format, ...) {
	va_list			ap;

	if(level>current_log_level)
		return;

	va_start(ap, format);

#if USE_SYSLOG
	static bool did_start_syslog;
	if(!did_start_syslog) {
		openlog("ge-rs232",LOG_PERROR|LOG_CONS,LOG_DAEMON);
		did_start_syslog = true;
	}
	vsyslog(level,format,ap);

#else
	char* formatted = NULL;
	struct timeval	tp;
	gettimeofday(&tp, NULL);
	vasprintf(&formatted, format, ap);

	if(formatted) {
		struct tm tmv;

		localtime_r((time_t *)&(tp.tv_sec), &tmv);

		fprintf(stderr,"[%d] %04d-%02d-%02d %02d:%02d:%02d%s %s\n",
			level,
			tmv.tm_year+1900,
			tmv.tm_mon,
			tmv.tm_mday,
			tmv.tm_hour,
			tmv.tm_min,
			tmv.tm_sec,
			tmv.tm_zone,
			formatted
		);
		free(formatted);
	}
#endif
}

typedef struct interface_context_s {
	struct ge_rs232_s interface;
	struct ge_queue_s queue;
} *interface_context_t;

ge_rs232_status_t
received_message(interface_context_t context, const uint8_t* data, uint8_t len,struct ge_rs232_s* instance) {
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
		// Uncommend the next line to ignore all touchpad display
		// info messages for all partitions except partition 1.
		//return GE_RS232_STATUS_OK;
	}

	if(last_msg_len == len && 0==memcmp(data,last_msg,len)) {
		// Skip duplicates.
		return GE_RS232_STATUS_OK;
	}
	memcpy(last_msg,data,len);
	last_msg_len = len;

	if(data[0]==GE_RS232_PTA_AUTOMATION_EVENT_LOST) {
		log_msg(LOG_LEVEL_NOTICE,"[AUTOMATION_EVENT_LOST]");

		static const uint8_t refresh_equipment_msg[] = { GE_RS232_ATP_EQUIP_LIST_REQUEST };

		ge_queue_message(&context->queue,refresh_equipment_msg,sizeof(refresh_equipment_msg),NULL,NULL);

		return 0;
	} else if(data[0]==GE_RS232_PTA_EQUIP_LIST_COMPLETE) {
		log_msg(LOG_LEVEL_NOTICE,"[EQUIP_LIST_COMPLETE]");

		static const uint8_t dynamic_data_refresh_msg[] = { GE_RS232_ATP_DYNAMIC_DATA_REFRESH };

		ge_queue_message(&context->queue,dynamic_data_refresh_msg,sizeof(dynamic_data_refresh_msg),NULL,NULL);

		return 0;
	} else if(data[0]==GE_RS232_PTA_ZONE_STATUS) {
		int zonei = (data[3]<<8)+data[4];

		log_msg(LOG_LEVEL_NOTICE,"[ZONE_STATUS] ZONE:%02d STATUS:%s%s%s%s%s",
			zonei,
			data[5]&GE_RS232_ZONE_STATUS_TRIPPED?"T":"-",
			data[5]&GE_RS232_ZONE_STATUS_FAULT?"F":"-",
			data[5]&GE_RS232_ZONE_STATUS_ALARM?"A":"-",
			data[5]&GE_RS232_ZONE_STATUS_TROUBLE?"R":"-",
			data[5]&GE_RS232_ZONE_STATUS_BYPASSED?"B":"-"
		);

		return 0;
	} else if(data[0]==GE_RS232_PTA_EQUIP_LIST_ZONE_DATA) {
		uint16_t zonei = (data[4]<<8)+data[5];

		log_msg(LOG_LEVEL_NOTICE,"[EQUIP_LIST_ZONE_INFO] ZONE:%d PN:%d AREA:%d TYPE:%d GROUP:%d STATUS:%s%s%s%s%s TEXT:\"%s\"",
			zonei,
			data[1],
			data[2],
			data[6],
			data[3],
			"?",
			data[7]&GE_RS232_ZONE_STATUS_FAULT?"F":"-",
			data[7]&GE_RS232_ZONE_STATUS_ALARM?"A":"-",
			data[7]&GE_RS232_ZONE_STATUS_TROUBLE?"R":"-",
			data[7]&GE_RS232_ZONE_STATUS_BYPASSED?"B":"-",
			ge_text_to_ascii_one_line(data+8,len-8)
		);
		return 0;
	} else if(data[0]==GE_RS232_PTA_EQUIP_LIST_SUPERBUS_DEV_DATA) {
		log_msg(LOG_LEVEL_INFO,
			"[EQUIP_LIST_SUPERBUS_DEV_DATA] PN:%d AREA:%d UNIT-ID:0x%06x UN:%d STATUS:%s",
			data[1],
			data[2],
			(data[3]<<16)+(data[4]<<8)+data[5],
			data[7],
			data[6]?"FAILURE":"OK"
		);
		len = 0;
	} else if(data[0]==GE_RS232_PTA_EQUIP_LIST_SUPERBUS_CAP_DATA) {
		const char* cap_list[] = {
			[0x00]="Power Supervision",
			[0x01]="Access Control",
			[0x02]="Analog Smoke",
			[0x03]="Audio Listen-In",
			[0x04]="SnapCard Supervision",
			[0x05]="Microburst",
			[0x06]="Dual Phone Line",
			[0x07]="Energy Management",
			[0x08]="Input Zones",
			[0x09]="Automation",
			[0x0A]="Phone Interface",
			[0x0B]="Relay Outputs",
			[0x0C]="RF Receiver",
			[0x0D]="RF Transmitter",
			[0x0E]="Parallel Printer",
			[0x0F]="UNKNOWN-0x0F",
			[0x10]="LED Touchpad",
			[0x11]="1-Line/2-Line/BLT Touchpad",
			[0x12]="GUI Touchpad",
			[0x13]="Voice Evacuation",
			[0x14]="Pager",
			[0x15]="Downloadable code/data",
			[0x16]="JTECH Premise Pager",
			[0x17]="Cryptography",
			[0x18]="LED Display",
		};

		log_msg(LOG_LEVEL_INFO,
			"[EQUIP_LIST_SUPERBUS_CAP_DATA] UNIT-ID:0x%06x CAP:\"%s\" DATA:%d",
			(data[1]<<16)+(data[2]<<8)+data[3],
			data[4]<(sizeof(cap_list)/sizeof(*cap_list))?cap_list[data[4]]:"unknown",
			data[5]
		);
	} else if(data[0]==GE_RS232_PTA_CLEAR_AUTOMATION_DYNAMIC_IMAGE) {
		log_msg(LOG_LEVEL_NOTICE,"[CLEAR_AUTOMATION_DYNAMIC_IMAGE]");
		static const uint8_t dynamic_data_refresh_msg[] = { GE_RS232_ATP_DYNAMIC_DATA_REFRESH };
		ge_queue_message(&context->queue,dynamic_data_refresh_msg,sizeof(dynamic_data_refresh_msg),NULL,NULL);
		return 0;

	} else if(data[0]==GE_RS232_PTA_PANEL_TYPE) {
		log_msg(LOG_LEVEL_INFO,
			"[PANEL_TYPE]"
		);
	} else if(data[0]==GE_RS232_PTA_SUBCMD) {
		char *str = NULL;
		switch(data[1]) {
			case GE_RS232_PTA_SUBCMD_LEVEL:
				log_msg(LOG_LEVEL_NOTICE,
					"[ARMING_LEVEL] PN:%d AREA:%d USER:%d LEVEL:%d",
					data[2],
					data[3],
					(data[4]<<8)+data[5],
					data[6]
				);
				return 0;
				len=0;
				break;
			case GE_RS232_PTA_SUBCMD_ALARM_TROUBLE:
				log_msg(LOG_LEVEL_ALERT,
					data[5]?"[ALARM/TROUBLE] PN:%d AREA:%d ST:%d UNIT-ID:0x%06x ALARM:%d.%d ESD:%d"
					:"[ALARM/TROUBLE] PN:%d AREA:%d ST:%d ZONE:%d ALARM:%d.%d ESD:%d",
					data[2],
					data[3],
					data[4],
					(data[5]<<16)+(data[6]<<8)+data[7],
					data[8],
					data[9],
					(data[10]<<8)+data[11]
				);
//				switch(data[8]) {
//					case 1: // General Alarm
//					case 2: // Alarm Canceled
//						asprintf(&str,"/home/pi/bin/report-alarm %d %d %d",data[8],data[9],data[7]);
//						break;
//					case 15: // System Trouble
//					default: break;
//				}
//				if(str) {
//					fprintf(stderr," ");
//					system(str);
//					free(str);
//				}
				return 0;
				break;
			case GE_RS232_PTA_SUBCMD_ENTRY_EXIT_DELAY:
				log_msg(LOG_LEVEL_INFO,
					"[%s_%s_DELAY] PN:%d AREA:%d EXT:%d SECONDS:%d",
					data[4]&(1<<7)?"END":"BEGIN",
					data[4]&(1<<6)?"EXIT":"ENTRY",
					data[2],
					data[3],
					data[4]&0x3,
					(data[5]<<8)+data[6]
				);
				return 0;
				break;
			case GE_RS232_PTA_SUBCMD_SIREN_SETUP:
				log_msg(LOG_LEVEL_INFO,
					"[SIREN_SETUP] PN:%d AREA:%d RP:%d CD:%02x%02x%02x%02x",
					data[2],
					data[3],
					data[4],
					data[5],
					data[6],
					data[7],
					data[8]
				);
				return 0;
				break;
			case GE_RS232_PTA_SUBCMD_SIREN_SYNC:
				log_msg(LOG_LEVEL_DEBUG,
					"[SIREN_SYNC]"
				);
				return GE_RS232_STATUS_OK;
				len=0;
				break;
			case GE_RS232_PTA_SUBCMD_SIREN_GO:
				log_msg(LOG_LEVEL_DEBUG,
					"[SIREN_GO]"
				);
				return 0;
				break;
			case GE_RS232_PTA_SUBCMD_TOUCHPAD_DISPLAY:
				//if(data[2]!=1)
				//	return GE_RS232_STATUS_OK;

				log_msg((data[2]==1)?LOG_LEVEL_INFO:LOG_LEVEL_DEBUG,
					"[TOUCHPAD_DISPLAY] PN:%d AREA:%d MT:%d MSG:\"%s\"",
					data[2],
					data[3],
					data[4],
					ge_text_to_ascii_one_line(data+5,len-5)
				);
				return 0;
				break;
			case GE_RS232_PTA_SUBCMD_SIREN_STOP:
				log_msg(LOG_LEVEL_DEBUG,
					"[SIREN_STOP]"
				);
				len=0;
				return 0;
				break;
			case GE_RS232_PTA_SUBCMD_FEATURE_STATE:
				{
				log_msg(LOG_LEVEL_DEBUG,
					"[FEATURE_STATE] PN:%d FS:0x%02X",
					data[2],
					data[4]
				);
				}
				return 0;
				break;
			case GE_RS232_PTA_SUBCMD_TEMPERATURE:
				log_msg(LOG_LEVEL_INFO,
					"[TEMPERATURE] PN:%d AREA:%d CUR:%d°F LOW:%d°f HIGH:%d°F",
					data[2],
					data[3],
					data[4],
					data[5]
				);
				len=0;
				break;
			case GE_RS232_PTA_SUBCMD_TIME_AND_DATE:
				log_msg(LOG_LEVEL_INFO,
					"[TIME_AND_DATE] %04d-%02d-%02d %02d:%02d",
					data[6]+2000,
					data[4],
					data[5],
					data[2],
					data[3]
				);
				len=0;
				break;
		}
	} else if(data[0]==GE_RS232_PTA_SUBCMD2) {
		char *str = NULL;
		switch(data[1]) {
			case GE_RS232_PTA_SUBCMD2_LIGHTS_STATE:
				log_msg(LOG_LEVEL_INFO,
					"[LIGHTS_STATE] PN:%d AREA:%d STATE:%d_%d%d%d%d%d%d%d%d",
					data[2],
					data[3],
					!!(data[4]&(1<<0)),
					!!(data[4]&(1<<1)),
					!!(data[4]&(1<<2)),
					!!(data[4]&(1<<3)),
					!!(data[4]&(1<<4)),
					!!(data[4]&(1<<5)),
					!!(data[4]&(1<<6)),
					!!(data[4]&(1<<7)),
					!!(data[5]&(1<<0)),
					!!(data[5]&(1<<1))
				);
				len=0;
				break;
			case GE_RS232_PTA_SUBCMD2_USER_LIGHTS:
				log_msg(LOG_LEVEL_INFO,
					data[5]?"[USER_LIGHTS] PN:%d AREA:%d ST:%d UNIT-ID:0x%06x LIGHT:%d STATE:%d"
					:"[USER_LIGHTS] PN:%d AREA:%d ST:%d ZONE:%d LIGHT:%d STATE:%d",
					data[2],
					data[3],
					data[4],
					(data[5]<<16)+(data[6]<<8)+data[7],
					data[8],
					data[9]
				);
				break;
			case GE_RS232_PTA_SUBCMD2_KEYFOB:
				log_msg(LOG_LEVEL_DEBUG,
					"[KEYFOB_PRESS] PN:%d AREA:%d ZONE:%d KEY:%d",
					data[2],
					data[3],
					data[4]<<8+data[5],
					data[6]
				);
				break;
		}
	}


	if(len) {
		char data_str[64*3];
		int strlen = 0;
		while(len--) {
			strlen+=snprintf(data_str+strlen,sizeof(data_str)-strlen,"%02X ",*data++);
		}
		log_msg(LOG_LEVEL_DEBUG,"[OTHER] { %s}",data_str);
	}

	return GE_RS232_STATUS_OK;
}

ge_rs232_status_t send_byte(void* context, uint8_t byte,struct ge_rs232_s* instance) {
	fputc(byte,stdout);
	fflush(stdout);
	return GE_RS232_STATUS_OK;
}


int main(int argc, const char* argv[]) {
	struct interface_context_s interface_context;

	ge_rs232_t interface = ge_rs232_init(&interface_context.interface);
	interface->received_message = (void*)&received_message;
	interface->send_byte = &send_byte;
	interface->context = (void*)&interface_context;
	ge_queue_init(&interface_context.queue, &interface_context.interface);

	if(argc>1) {
		const char* device = argv[1];
		int fd = -1;
		log_msg(LOG_LEVEL_NOTICE,"Opening \"%s\" . . .",device);
		fd = open(device,O_RDWR | O_NOCTTY | O_NDELAY);
		if(fd<0) {
			log_msg(LOG_LEVEL_CRITICAL,"Unable to open \"%s\"!",device);
			return -1;
		}
		stdin = fdopen(fd,"r");
		stdout = fdopen(fd,"w");
	} else {
		log_msg(LOG_LEVEL_WARNING,"Device not specified, using stdin/stdout.");
	}

	setvbuf(stdout, NULL, _IONBF, 0);
	setvbuf(stdin, NULL, _IONBF, 0);

	int r;
	struct termios t;
	if(tcgetattr(fileno(stdin), &t)==0) {
		cfmakeraw(&t);
		cfsetspeed(&t, 9600);
		t.c_cflag = CLOCAL | CREAD | CS8 | PARENB | PARODD;
		t.c_iflag = INPCK | IGNBRK;
		t.c_oflag = 0;
		t.c_lflag = 0;
		tcsetattr(fileno(stdin), TCSANOW, &t);
	}

	if(tcgetattr(fileno(stdout), &t)==0) {
		cfmakeraw(&t);
		cfsetspeed(&t, 9600);
		t.c_cflag = CLOCAL | CREAD | CS8 | PARENB | PARODD;
		t.c_iflag = IGNPAR | IGNBRK;
		t.c_oflag = 0;
		t.c_lflag = 0;
		tcsetattr(fileno(stdout), TCSANOW, &t);
	}

	while(!feof(stdin)) {
		ge_rs232_receive_byte(interface,fgetc(stdin));
		ge_queue_update(&interface_context.queue);
	}

	return 0;
}
