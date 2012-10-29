#include <smcp/assert_macros.h>
#include <stdio.h>
#include "ge-rs232.h"
#include <string.h>
#include <stdlib.h>
#include <smcp/smcp.h>
#include <smcp/smcp-node.h>
#include <smcp/smcp-pairing.h>
#include <poll.h>
#include <termios.h>
#include <stdarg.h>
#include <stdlib.h>
#include <fcntl.h>

#define GE_RS232_ZONE_TYPE_HARDWIRED	(0)
#define GE_RS232_ZONE_TYPE_RF			(2)
#define GE_RS232_ZONE_TYPE_TOUCHPAD		(3)
#define GE_RS232_ZONE_TYPE_UNCONFIGURED	(255)

#define GE_RS232_MAX_ZONES				(96)
#define GE_RS232_MAX_PARTITIONS			(6)
#define GE_RS232_MAX_SCHEDULES			(16)

#define GE_RS232_ARMING_LEVEL_ZONE_TEST	(0)
#define GE_RS232_ARMING_LEVEL_OFF		(1)
#define GE_RS232_ARMING_LEVEL_STAY		(2)
#define GE_RS232_ARMING_LEVEL_AWAY		(3)
#define GE_RS232_ARMING_LEVEL_NIGHT		(4)
#define GE_RS232_ARMING_LEVEL_SILENT	(5)
#define GE_RS232_ARMING_LEVEL_PHONE_TEST (8)
#define GE_RS232_ARMING_LEVEL_SENSOR_TEST (9)

#define GE_RS232_ALARM_SOURCE_TYPE_BUS_DEVICE		(0)
#define GE_RS232_ALARM_SOURCE_TYPE_LOCAL_PHONE		(1)
#define GE_RS232_ALARM_SOURCE_TYPE_ZONE				(2)
#define GE_RS232_ALARM_SOURCE_TYPE_SYSTEM			(3)
#define GE_RS232_ALARM_SOURCE_TYPE_REMOTE_PHONE		(4)

#define GE_RS232_ALARM_GENERAL_TYPE_ALARM			(1)
#define GE_RS232_ALARM_GENERAL_TYPE_ALARM_CANCEL	(2)
#define GE_RS232_ALARM_GENERAL_TYPE_ALARM_RESTORAL	(3)
#define GE_RS232_ALARM_GENERAL_TYPE_FIRE_TROUBLE	(4)
#define GE_RS232_ALARM_GENERAL_TYPE_FIRE_TROUBLE_RESTORAL	(5)
#define GE_RS232_ALARM_GENERAL_TYPE_NONFIRE_TROUBLE	(6)
#define GE_RS232_ALARM_GENERAL_TYPE_NONFIRE_TROUBLE_RESTORAL	(7)
#define GE_RS232_ALARM_GENERAL_TYPE_BYPASS			(8)
#define GE_RS232_ALARM_GENERAL_TYPE_UNBYPASS		(9)
#define GE_RS232_ALARM_GENERAL_TYPE_OPENING			(10)
#define GE_RS232_ALARM_GENERAL_TYPE_CLOSING			(11)
#define GE_RS232_ALARM_GENERAL_TYPE_PARTITION_CONFIG_CHANGE			(12)
#define GE_RS232_ALARM_GENERAL_TYPE_PARTITION_EVENT			(13)
#define GE_RS232_ALARM_GENERAL_TYPE_PARTITION_TEST			(14)
#define GE_RS232_ALARM_GENERAL_TYPE_SYSTEM_TROUBLE			(15)
#define GE_RS232_ALARM_GENERAL_TYPE_SYSTEM_TROUBLE_RESTORAL			(16)
#define GE_RS232_ALARM_GENERAL_TYPE_SYSTEM_CONFIG_CHANGE			(17)
#define GE_RS232_ALARM_GENERAL_TYPE_SYSTEM_EVENT			(18)

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

struct ge_zone_s {
	struct smcp_variable_node_s node;

	uint8_t partition;
	uint8_t area;
	uint8_t group;
	uint16_t zone_number;
	uint8_t type;
	uint8_t status;

	time_t last_tripped;
	uint32_t trip_count;

	char label[16];
	uint8_t label_len;
};

struct ge_schedule_s {
	struct smcp_variable_node_s node;
	uint8_t partition_number;
	uint8_t area;

	uint8_t schedule_number;
	uint8_t start_hour;
	uint8_t start_minute;
	uint8_t stop_hour;
	uint8_t stop_minute;
	uint8_t weekdays;
};

struct ge_alarm_s {
	uint8_t partition;
	uint8_t area;
	uint8_t src_type;
	uint32_t src_number;

	uint8_t general_type;
	uint8_t specific_type;
	uint16_t data;
	time_t date;
};

struct ge_partition_s {
	struct smcp_variable_node_s node;

	uint8_t partition_number;

	uint8_t arming_level;
	uint16_t armed_by;

	uint8_t feature_state;
	uint16_t light_state;

	char label[16];
	uint8_t label_len;

	char touchpad_lcd[32];
	uint8_t touchpad_lcd_len;
};

struct ge_system_state_s {
	struct smcp_node_s node;

	struct ge_rs232_s interface;

	struct ge_zone_s zone[GE_RS232_MAX_ZONES];
	struct ge_partition_s partition[GE_RS232_MAX_PARTITIONS];
	struct ge_schedule_s schedules[GE_RS232_MAX_SCHEDULES];

	uint8_t zone_count;

	uint8_t panel_type;
	uint16_t hardware_rev;
	uint16_t software_rev;
	uint32_t serial;

	struct smcp_async_response_s async_response;
};

static void
async_response_ack_handler(int statuscode, struct ge_system_state_s* self) {
    struct smcp_async_response_s* async_response = &self->async_response;
	smcp_finish_async_response(async_response);
}

static smcp_status_t
resend_async_response(struct ge_system_state_s* self) {
    smcp_status_t ret = 0;
    struct smcp_async_response_s* async_response = &self->async_response;

	if(self->interface.last_response == GE_RS232_ACK)
        ret = smcp_outbound_begin_response(COAP_RESULT_204_CHANGED);
	else
        ret = smcp_outbound_begin_response(COAP_RESULT_500_INTERNAL_SERVER_ERROR);
	require_noerr(ret,bail);

    ret = smcp_outbound_set_async_response(async_response);
    require_noerr(ret,bail);

	if(self->interface.last_response == GE_RS232_NAK)
		smcp_outbound_set_content_formatted("NAK");
	else if(self->interface.last_response == 0)
		smcp_outbound_set_content_formatted("ACK-TIMEOUT");
	else if(self->interface.last_response != GE_RS232_ACK)
		smcp_outbound_set_content_formatted("WEIRD-LAST_RESPONSE '%u'",(int)self->interface.last_response);

    ret = smcp_outbound_send();
    require_noerr(ret,bail);

    if(ret) {
        assert_printf(
            "smcp_outbound_send() returned error %d(%s).\n",
            ret,
            smcp_status_to_cstr(ret)
        );
        goto bail;
    }

bail:
    return ret;
}

void got_panel_response(struct ge_system_state_s* self,struct ge_rs232_s* instance, bool didAck) {
	instance->got_response = NULL;
	smcp_t smcp_instance = (smcp_t)smcp_node_get_root(&self->node);

	smcp_begin_transaction(
		smcp_instance,
		smcp_get_next_tid(smcp_instance,NULL),
		5*1000,    // Retry for five seconds.
		0, // Flags
		(void*)&resend_async_response,
		(void*)&async_response_ack_handler,
		(void*)self
	);

}

const char* ge_text_to_ascii_one_line(const char* bytes, uint8_t len) {
	static char ret[1024];
	ret[0] = 0;
	while(len--) {
		const char* str = ge_rs232_text_token_lookup[*bytes++];
		if(str) {
			if(str[0]=='\n') {
				if(len)
					strlcat(ret," | ",sizeof(ret));
			} else {
				strlcat(ret,str,sizeof(ret));
			}
		} else {
			strlcat(ret,"?",sizeof(ret));
		}
	}
	return ret;
}

const char* ge_text_to_ascii(const char* bytes, uint8_t len) {
	static char ret[1024];
	ret[0] = 0;
	while(len--) {
		const char* str = ge_rs232_text_token_lookup[*bytes++];
		if(str) {
			strlcat(ret,str,sizeof(ret));
		} else {
			strlcat(ret,"?",sizeof(ret));
		}
	}
	return ret;
}

ge_rs232_status_t refresh_equipment_list(ge_rs232_t interface) {
	uint8_t msg[] = {
		GE_RS232_ATP_EQUIP_LIST_REQUEST,
	};
	return ge_rs232_send_message(interface,msg,sizeof(msg));
}

ge_rs232_status_t dynamic_data_refresh(ge_rs232_t interface) {
	uint8_t msg[] = {
		GE_RS232_ATP_DYNAMIC_DATA_REFRESH,
	};
	return ge_rs232_send_message(interface,msg,sizeof(msg));
}


ge_rs232_status_t send_keypress(ge_rs232_t interface,uint8_t partition, uint8_t area, char* keys) {
	uint8_t msg[50] = {
		GE_RS232_ATP_KEYPRESS,
		partition,	// Partition
		area,	// Area
	};
	uint8_t len = 3;
	for(;*keys;keys++) {
		uint8_t code = 255;
		switch(*keys) {
			case '0':
			case '1':
			case '2':
			case '3':
			case '4':
			case '5':
			case '6':
			case '7':
			case '8':
			case '9':
				code = *keys - '0';
				break;
			case '*':code = 0x0A;break;
			case '#':code = 0x0B;break;
			case 'A':case 'a':code = 0x2C;break;
			case 'B':case 'b':code = 0x30;break;
			case 'C':case 'c':code = 0x2D;break;
			case 'D':case 'd':code = 0x33;break;
			case 'E':case 'e':code = 0x2E;break;
			case 'F':case 'f':code = 0x36;break;
			case '[':
				if(keys[1]!=0) {
					keys++;
					code = strtol(keys,&keys,16);
					if(*keys!=']') {
						log_msg(LOG_LEVEL_WARNING,"send_keypress: '[' without ']'");
						return -1;
					}
				}
				break;
			default:
				log_msg(LOG_LEVEL_WARNING,"send_keypress: bad key code %d '%c'",*keys,*keys);
				return -1;
				break;
		}
		if(code==255)
			continue;
		msg[len++] = code;
	}
	return ge_rs232_send_message(interface,msg,len);
}

static smcp_status_t
partition_node_var_func(
	struct ge_partition_s *node,
	uint8_t action,
	uint8_t path,
	char* value
) {
	smcp_status_t ret = 0;
	enum {
		PATH_ARM_LEVEL=0,
		PATH_ARMED_BY,
		PATH_FS_CHIME,
		PATH_FS_ENERGY_SAVER,
		PATH_FS_NO_DELAY,
		PATH_FS_LATCHKEY,
		PATH_FS_SILENT_ARMING,
		PATH_FS_QUICK_ARM,
//		PATH_TEXT,
		PATH_TOUCHPAD_TEXT,
		PATH_KEYPRESS,
		PATH_REFRESH_EQUIPMENT,
		PATH_DDR,
		PATH_LIGHT_ALL,
		PATH_LIGHT_1,
		PATH_LIGHT_2,
		PATH_LIGHT_3,
		PATH_LIGHT_4,
		PATH_LIGHT_5,
		PATH_LIGHT_6,
		PATH_LIGHT_7,
		PATH_LIGHT_8,
		PATH_LIGHT_9,

		PATH_COUNT,
	};

	if(path>=PATH_COUNT) {
		ret = SMCP_STATUS_NOT_FOUND;
	} else if(action==SMCP_VAR_GET_KEY) {
		static const char* path_names[] = {
			"arm-level",
			"armed-by",
			"chime",
			"energy-saver",
			"no-delay",
			"latchkey",
			"silent-arming",
			"quick-arm",
//			"text",
			"touchpad-text",
			"keypress",
			"refresh-equipment",
			"ddr",
			"light-all",
			"light-1",
			"light-2",
			"light-3",
			"light-4",
			"light-5",
			"light-6",
			"light-7",
			"light-8",
			"light-9",
		};
		strcpy(value,path_names[path]);
	} else if(action==SMCP_VAR_GET_LF_TITLE) {
		if(path==PATH_ARM_LEVEL) {
			const char *arm_level[] = {
				[0]="ZONE-TEST",
				[1]="DISARMED",
				[2]="STAY",
				[3]="AWAY",
				[4]="NIGHT",
				[5]="SILENT",
			};
			if(node->arming_level<sizeof(arm_level)/sizeof(*arm_level)-1) {
				strncpy(value,arm_level[node->arming_level],SMCP_VARIABLE_MAX_VALUE_LENGTH);
			} else {
				ret = SMCP_STATUS_NOT_ALLOWED;
			}
		} else if(path==PATH_ARMED_BY) {
			if(node->armed_by>=230 && node->armed_by<=237) {
				strncpy(value,"PARTITION-MASTER-CODE",SMCP_VARIABLE_MAX_VALUE_LENGTH);
			} else if(node->armed_by>=238 && node->armed_by<=245) {
				strncpy(value,"DURESS-CODE",SMCP_VARIABLE_MAX_VALUE_LENGTH);
			} else if(node->armed_by==246) {
				strncpy(value,"SYSTEM-CODE",SMCP_VARIABLE_MAX_VALUE_LENGTH);
			} else if(node->armed_by==247) {
				strncpy(value,"INSTALLER",SMCP_VARIABLE_MAX_VALUE_LENGTH);
			} else if(node->armed_by==248) {
				strncpy(value,"DEALER",SMCP_VARIABLE_MAX_VALUE_LENGTH);
			} else if(node->armed_by==249) {
				strncpy(value,"AVM-CODE",SMCP_VARIABLE_MAX_VALUE_LENGTH);
			} else if(node->armed_by==250) {
				strncpy(value,"QUICK-ARM",SMCP_VARIABLE_MAX_VALUE_LENGTH);
			} else if(node->armed_by==251) {
				strncpy(value,"KEY-SWITCH",SMCP_VARIABLE_MAX_VALUE_LENGTH);
			} else if(node->armed_by==252) {
				strncpy(value,"SYSTEM",SMCP_VARIABLE_MAX_VALUE_LENGTH);
			} else if(node->armed_by==255) {
				strncpy(value,"AUTOMATION",SMCP_VARIABLE_MAX_VALUE_LENGTH);
			} else if(node->armed_by==65535) {
				strncpy(value,"SYSTEM/KEY-SWITCH",SMCP_VARIABLE_MAX_VALUE_LENGTH);
			} else {
				snprintf(value,SMCP_VARIABLE_MAX_VALUE_LENGTH,"USER-%d",node->armed_by);
			}
		} else {
			ret = SMCP_STATUS_NOT_ALLOWED;
		}
	} else if(action==SMCP_VAR_GET_VALUE) {
		if(path==PATH_TOUCHPAD_TEXT) {
			// Just send the ascii for now.
			int i = 0;
			value[0]=0;
			for(;i<node->touchpad_lcd_len;i++) {
				const char* str = ge_rs232_text_token_lookup[node->touchpad_lcd[i]];

				if(str) {
					strlcat(value,str,SMCP_VARIABLE_MAX_VALUE_LENGTH);
				}
			}
/*
		} else if(path==PATH_TEXT) {
			// Just send the ascii for now.
			int i = 0;
			value[0]=0;
			for(;i<node->label_len;i++) {
				const char* str = ge_rs232_text_token_lookup[node->label[i]];

				if(str) {
					strlcat(value,str,SMCP_VARIABLE_MAX_VALUE_LENGTH);
				}
			}
*/
		} else {
			int v = 0;

			if(path==PATH_ARM_LEVEL)
				v = node->arming_level;
			else if(path>=PATH_LIGHT_ALL && path<=PATH_LIGHT_9)
				v = !!(node->light_state & (1<<(path-PATH_LIGHT_ALL)));
			else if(path==PATH_ARMED_BY)
				v = node->armed_by;
			else if(path==PATH_FS_CHIME)
				v = !!(node->feature_state & (1<<0));
			else if(path==PATH_FS_ENERGY_SAVER)
				v = !!(node->feature_state & (1<<1));
			else if(path==PATH_FS_NO_DELAY)
				v = !!(node->feature_state & (1<<2));
			else if(path==PATH_FS_LATCHKEY)
				v = !!(node->feature_state & (1<<3));
			else if(path==PATH_FS_SILENT_ARMING)
				v = !!(node->feature_state & (1<<4));
			else if(path==PATH_FS_QUICK_ARM)
				v = !!(node->feature_state & (1<<5));
			else
				ret = SMCP_STATUS_NOT_ALLOWED;
			sprintf(value,"%d",v);
		}
	} else if(action==SMCP_VAR_SET_VALUE) {
		if(path==PATH_ARM_LEVEL) {
			struct ge_system_state_s* system_state=(struct ge_system_state_s*)node->node.node.parent;
			if((node->arming_level)==atoi(value)) {
				// arm level already set!
				ret = SMCP_STATUS_OK;
			} else if(!system_state->interface.got_response) {
				switch(atoi(value)) {
					case 1: send_keypress(&system_state->interface,node->partition_number,0,"5[20]");break;
					case 2: send_keypress(&system_state->interface,node->partition_number,0,"5[28]");break;
					case 3: send_keypress(&system_state->interface,node->partition_number,0,"5[27]");break;
					default:
						log_msg(LOG_LEVEL_WARNING,"Bad arming level \"%s\"",value);
						return SMCP_STATUS_FAILURE;
				}
				ret = smcp_start_async_response(&system_state->async_response,0);
				require_noerr(ret,bail);
				system_state->interface.got_response=(void*)&got_panel_response;
				ret = SMCP_STATUS_ASYNC_RESPONSE;
			} else {
				smcp_outbound_drop();
				log_msg(LOG_LEVEL_WARNING,"Too busy to set arming level. Dropping packet.");
				ret = SMCP_STATUS_FAILURE;
			}
		} else if(path>=PATH_LIGHT_ALL && path<=PATH_LIGHT_9) {
			struct ge_system_state_s* system_state=(struct ge_system_state_s*)node->node.node.parent;
			char cmd[] = { '[','1','1'-!!atoi(value),']','0'+path-PATH_LIGHT_ALL,0};
			if(!!(node->light_state & (1<<(path-PATH_LIGHT_ALL)))==atoi(value)) {
				// Light already set!
				ret = SMCP_STATUS_OK;
			} else if(!system_state->interface.got_response
				&& (0== send_keypress(&system_state->interface,node->partition_number,0,cmd))
			) {
				ret = smcp_start_async_response(&system_state->async_response,0);
				require_noerr(ret,bail);
				system_state->interface.got_response=(void*)&got_panel_response;
				ret = SMCP_STATUS_ASYNC_RESPONSE;
			} else {
				log_msg(LOG_LEVEL_WARNING,"Too busy to change light. Dropping packet.");
				smcp_outbound_drop();
				ret = SMCP_STATUS_FAILURE;
			}
		} else if(path==PATH_FS_CHIME) {
			struct ge_system_state_s* system_state=(struct ge_system_state_s*)node->node.node.parent;
			if((node->feature_state & (1<<0))==atoi(value)) {
				// Chime already set!
				ret = SMCP_STATUS_OK;
			} else if(!system_state->interface.got_response
				&& (0== send_keypress(&system_state->interface,node->partition_number,0,"71"))
			) {
				ret = smcp_start_async_response(&system_state->async_response,0);
				require_noerr(ret,bail);
				system_state->interface.got_response=(void*)&got_panel_response;
				ret = SMCP_STATUS_ASYNC_RESPONSE;
			} else {
				log_msg(LOG_LEVEL_WARNING,"Too busy to set chime. Dropping packet.");
				smcp_outbound_drop();
				ret = SMCP_STATUS_FAILURE;
			}
		} else if(path==PATH_REFRESH_EQUIPMENT) {
			struct ge_system_state_s* system_state=(struct ge_system_state_s*)node->node.node.parent;
			if(!system_state->interface.got_response) {
				refresh_equipment_list(&system_state->interface);
				ret = smcp_start_async_response(&system_state->async_response,0);
				require_noerr(ret,bail);
				system_state->interface.got_response=(void*)&got_panel_response;
				ret = SMCP_STATUS_ASYNC_RESPONSE;
			}
		} else if(path==PATH_KEYPRESS) {
			struct ge_system_state_s* system_state=(struct ge_system_state_s*)node->node.node.parent;
			if(!system_state->interface.got_response
				&& (0 == send_keypress(&system_state->interface,node->partition_number,0,value))
			) {
				ret = smcp_start_async_response(&system_state->async_response,0);
				require_noerr(ret,bail);
				system_state->interface.got_response=(void*)&got_panel_response;
				ret = SMCP_STATUS_ASYNC_RESPONSE;
			} else {
				log_msg(LOG_LEVEL_WARNING,"Too busy to send keypresses. Dropping packet.");
				smcp_outbound_drop();
				ret = SMCP_STATUS_FAILURE;
			}
		} else if(path==PATH_DDR) {
			struct ge_system_state_s* system_state=(struct ge_system_state_s*)node->node.node.parent;
			if(!system_state->interface.got_response) {
				dynamic_data_refresh(&system_state->interface);
				ret = smcp_start_async_response(&system_state->async_response,0);
				require_noerr(ret,bail);
				system_state->interface.got_response=(void*)&got_panel_response;
				ret = SMCP_STATUS_ASYNC_RESPONSE;
			} else {
				log_msg(LOG_LEVEL_WARNING,"Too busy to send dynamic data refresh. Dropping packet.");
				smcp_outbound_drop();
				ret = SMCP_STATUS_FAILURE;
			}
		} else {
			ret = SMCP_STATUS_NOT_ALLOWED;
		}
	} else {
		ret = SMCP_STATUS_NOT_IMPLEMENTED;
	}
bail:
	return ret;
}

static smcp_status_t
zone_node_var_func(
	struct ge_zone_s *node,
	uint8_t action,
	uint8_t path,
	char* value
) {
	smcp_status_t ret = 0;
	enum {
		PATH_PARTITION=0,
		PATH_AREA,
		PATH_GROUP,
		PATH_TYPE,
		PATH_TEXT,
		PATH_STATUS_TRIPPED,
		PATH_STATUS_FAULT,
		PATH_STATUS_ALARM,
		PATH_STATUS_TROUBLE,
		PATH_STATUS_BYPASS,

		PATH_COUNT,
	};

	if(path>=PATH_COUNT) {
		ret = SMCP_STATUS_NOT_FOUND;
	} else if(action==SMCP_VAR_GET_KEY) {
		static const char* path_names[] = {
			"pn",
			"an",
			"gn",
			"zt",
			"text",
			"zs.tripped",
			"zs.fault",
			"zs.alarm",
			"zs.trouble",
			"zs.bypass",
		};
		strcpy(value,path_names[path]);
	} else if(action==SMCP_VAR_GET_VALUE) {
		if(path==PATH_TEXT) {
			// Just send the ascii for now.
			int i = 0;
			value[0]=0;
			for(;i<node->label_len;i++) {
				const char* str = ge_rs232_text_token_lookup[node->label[i]];

				if(str) {
					strlcat(value,str,SMCP_VARIABLE_MAX_VALUE_LENGTH);
				}
			}
		} else {
			int v = 0;
			if(path==PATH_PARTITION)
				v = node->partition;
			else if(path==PATH_AREA)
				v = node->area;
			else if(path==PATH_GROUP)
				v = node->group;
			else if(path==PATH_TYPE)
				v = node->type;
			else if(path==PATH_STATUS_TRIPPED)
				v = !!(node->status & GE_RS232_ZONE_STATUS_TRIPPED);
			else if(path==PATH_STATUS_TROUBLE)
				v = !!(node->status & GE_RS232_ZONE_STATUS_TROUBLE);
			else if(path==PATH_STATUS_FAULT)
				v = !!(node->status & GE_RS232_ZONE_STATUS_FAULT);
			else if(path==PATH_STATUS_BYPASS)
				v = !!(node->status & GE_RS232_ZONE_STATUS_BYPASSED);
			else if(path==PATH_STATUS_ALARM)
				v = !!(node->status & GE_RS232_ZONE_STATUS_ALARM);
			sprintf(value,"%d",v);
		}
	} else if(action==SMCP_VAR_SET_VALUE) {
		ret = SMCP_STATUS_NOT_ALLOWED;
	} else {
		ret = SMCP_STATUS_NOT_IMPLEMENTED;
	}

	return ret;
}

struct ge_zone_s *
ge_get_zone(struct ge_system_state_s *node,int zonei) {
	struct ge_zone_s *zone = NULL;
	if(zonei && (zonei<=GE_RS232_MAX_ZONES)) {
		zone = &node->zone[zonei-1];
		if(!zone->node.node.parent) {
			// Bring this zone to life.
			char* label = NULL;
			asprintf(&label,"zone-%d",zonei);
			smcp_variable_node_init(&zone->node,&node->node,label);
			zone->node.func = (smcp_variable_node_func)&zone_node_var_func;
			zone->zone_number = zonei;
		}
	}
	return zone;
}

struct ge_partition_s *
ge_get_partition(struct ge_system_state_s *node,int partitioni) {
	struct ge_partition_s *partition = NULL;
	if(partitioni && (partitioni<=GE_RS232_MAX_PARTITIONS)) {
		partition = &node->partition[partitioni-1];
		if(!partition->node.node.parent) {
			// Bring this partition to life.
			char* label = NULL;
			asprintf(&label,"p-%d",partitioni);
			smcp_variable_node_init(&partition->node,&node->node,label);
			partition->node.func = (smcp_variable_node_func)&partition_node_var_func;
			partition->partition_number = partitioni;
		}
	}
	return partition;
}



ge_rs232_status_t
received_message(struct ge_system_state_s *node, const uint8_t* data, uint8_t len,struct ge_rs232_s* interface) {

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
		//return GE_RS232_STATUS_OK;
	}

	if(last_msg_len == len && 0==memcmp(data,last_msg,len)) {
		return GE_RS232_STATUS_OK;
	}
	memcpy(last_msg,data,len);
	last_msg_len = len;

	if(data[0]==GE_RS232_PTA_AUTOMATION_EVENT_LOST) {
		log_msg(LOG_LEVEL_NOTICE,"[AUTOMATION_EVENT_LOST]");
		ge_rs232_status_t status = ge_rs232_ready_to_send(interface);
		if(status != GE_RS232_STATUS_WAIT) {
			ge_rs232_status_t status = dynamic_data_refresh(interface);
		} else {
			log_msg(LOG_LEVEL_ERROR,"UNABLE TO SEND REFRESH: NOT READY");
		}
		len=0;
		return 0;
	} else if(data[0]==GE_RS232_PTA_ZONE_STATUS) {
		int zonei = (data[3]<<8)+data[4];

		struct ge_zone_s* zone = ge_get_zone(node,zonei);

		if(zone) {
			if((zone->status^data[5])&GE_RS232_ZONE_STATUS_TRIPPED) {
				smcp_trigger_event_with_node(
					smcp_node_get_root((smcp_node_t)node),
					&zone->node.node,
					"zs.tripped"
				);
				smcp_trigger_event_with_node(
					smcp_node_get_root((smcp_node_t)node),
					&zone->node.node,
					(data[5]&GE_RS232_ZONE_STATUS_TRIPPED)?"zs.tripped!v=1":"zs.tripped!v=0"
				);
			}
			if((zone->status^data[5])&GE_RS232_ZONE_STATUS_FAULT)
				smcp_trigger_event_with_node(
					smcp_node_get_root((smcp_node_t)node),
					&zone->node.node,
					"zs.fault"
				);
			if((zone->status^data[5])&GE_RS232_ZONE_STATUS_TROUBLE)
				smcp_trigger_event_with_node(
					smcp_node_get_root((smcp_node_t)node),
					&zone->node.node,
					"zs.trouble"
				);
			if((zone->status^data[5])&GE_RS232_ZONE_STATUS_ALARM)
				smcp_trigger_event_with_node(
					smcp_node_get_root((smcp_node_t)node),
					&zone->node.node,
					"zs.alarm"
				);
			if((zone->status^data[5])&GE_RS232_ZONE_STATUS_BYPASSED)
				smcp_trigger_event_with_node(
					smcp_node_get_root((smcp_node_t)node),
					&zone->node.node,
					"zs.bypass"
				);
			zone->partition = data[1];
			zone->area = data[2];
			zone->status = data[5];
			log_msg(LOG_LEVEL_NOTICE,"[ZONE_STATUS] ZONE:%02d STATUS:%s%s%s%s%s TEXT:\"%s\"",
				zonei,
				data[5]&GE_RS232_ZONE_STATUS_TRIPPED?"T":"-",
				data[5]&GE_RS232_ZONE_STATUS_FAULT?"F":"-",
				data[5]&GE_RS232_ZONE_STATUS_ALARM?"A":"-",
				data[5]&GE_RS232_ZONE_STATUS_TROUBLE?"R":"-",
				data[5]&GE_RS232_ZONE_STATUS_BYPASSED?"B":"-",
				ge_text_to_ascii_one_line(zone->label,zone->label_len)
			);
		}

		return 0;
		len = 0;
	} else if(data[0]==GE_RS232_PTA_EQUIP_LIST_ZONE_DATA) {
		uint16_t zonei = (data[4]<<8)+data[5];
		struct ge_zone_s* zone = ge_get_zone(node,zonei);

		if(zone) {

			if((zone->status^data[7])&GE_RS232_ZONE_STATUS_TRIPPED)
				smcp_trigger_event_with_node(
					smcp_node_get_root((smcp_node_t)node),
					&zone->node.node,
					"zs.tripped"
				);
			if((zone->status^data[7])&GE_RS232_ZONE_STATUS_FAULT)
				smcp_trigger_event_with_node(
					smcp_node_get_root((smcp_node_t)node),
					&zone->node.node,
					"zs.fault"
				);
			if((zone->status^data[7])&GE_RS232_ZONE_STATUS_TROUBLE)
				smcp_trigger_event_with_node(
					smcp_node_get_root((smcp_node_t)node),
					&zone->node.node,
					"zs.trouble"
				);
			if((zone->status^data[7])&GE_RS232_ZONE_STATUS_ALARM)
				smcp_trigger_event_with_node(
					smcp_node_get_root((smcp_node_t)node),
					&zone->node.node,
					"zs.alarm"
				);
			if((zone->status^data[7])&GE_RS232_ZONE_STATUS_BYPASSED)
				smcp_trigger_event_with_node(
					smcp_node_get_root((smcp_node_t)node),
					&zone->node.node,
					"zs.bypass"
				);
			zone->partition = data[1];
			zone->area = data[2];
			zone->group = data[3];
			zone->zone_number = zonei;
			zone->type = data[6];

			zone->status = data[7];

			zone->label_len = len-8;
			memcpy(zone->label,data+8,len-8);
		}

		log_msg(LOG_LEVEL_NOTICE,"[EQUIP_LIST_ZONE_INFO] ZONE:%d PN:%d AREA:%d TYPE:%d GROUP:%d STATUS:%s%s%s%s%s TEXT:\"%s\"",
			zonei,
			data[1],
			data[2],
			data[6],
			data[3],
			data[7]&GE_RS232_ZONE_STATUS_TRIPPED?"T":"-",
			data[7]&GE_RS232_ZONE_STATUS_FAULT?"F":"-",
			data[7]&GE_RS232_ZONE_STATUS_ALARM?"A":"-",
			data[7]&GE_RS232_ZONE_STATUS_TROUBLE?"R":"-",
			data[7]&GE_RS232_ZONE_STATUS_BYPASSED?"B":"-",
			ge_text_to_ascii_one_line(data+8,len-8)

		);
		return 0;
		len = 0;
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
		ge_rs232_status_t status = ge_rs232_ready_to_send(interface);
		if(status != GE_RS232_STATUS_WAIT) {
			ge_rs232_status_t status = dynamic_data_refresh(interface);
		} else {
			log_msg(LOG_LEVEL_WARNING,"Unable to send refresh, not ready.");
		}
		return 0;

	} else if(data[0]==GE_RS232_PTA_PANEL_TYPE) {
		log_msg(LOG_LEVEL_INFO,
			"[PANEL_TYPE]"
		);
		return 0;
	} else if(data[0]==GE_RS232_PTA_SUBCMD) {
		char *str = NULL;
		switch(data[1]) {
			case GE_RS232_PTA_SUBCMD_LEVEL:
				{
				int partitioni = data[2];
				struct ge_partition_s* partition = ge_get_partition(node,partitioni);
				if(partition) {
					partition->arming_level = data[6];
					partition->armed_by = (data[4]<<8)+(data[5]);
				}
				}
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
				return 0;
//				fprintf(stderr,"[SIREN_SYNC]");
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

				{
				int partitioni = data[2];
				struct ge_partition_s* partition = ge_get_partition(node,partitioni);
				if(partition) {
					smcp_trigger_event_with_node(
						smcp_node_get_root((smcp_node_t)node),
						&partition->node.node,
						"touchpad-text"
					);

					if(len-5!=partition->touchpad_lcd_len
						|| 0!=memcmp(data+5,partition->touchpad_lcd,len-5)
					) {
						log_msg((data[2]==1)?LOG_LEVEL_INFO:LOG_LEVEL_DEBUG,
							"[TOUCHPAD_DISPLAY] PN:%d AREA:%d MT:%d MSG:\"%s\"",
							data[2],
							data[3],
							data[4],
							ge_text_to_ascii_one_line(data+5,len-5)
						);
					}

					partition->touchpad_lcd_len = len-5;
					memcpy(partition->touchpad_lcd,data+5,len-5);
				}
				}
				return 0;
				break;
			case GE_RS232_PTA_SUBCMD_SIREN_STOP:
				log_msg(LOG_LEVEL_DEBUG,
					"[SIREN_STOP]"
				);
//				fprintf(stderr,"[SIREN_STOP]");
				len=0;
				return 0;
				break;
			case GE_RS232_PTA_SUBCMD_FEATURE_STATE:
				{
				int partitioni = data[2];
				struct ge_partition_s* partition = ge_get_partition(node,partitioni);
				if(partition) {
					partition->feature_state = data[4];
				}
				log_msg(LOG_LEVEL_DEBUG,
					"[FEATURE_STATE] PN:%d",
					partitioni
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
				int partitioni = data[2];
				struct ge_partition_s* partition = ge_get_partition(node,partitioni);
				if(partition) {
					if((partition->light_state^data[4])&(1<<0))
						smcp_trigger_event_with_node(
							smcp_node_get_root((smcp_node_t)node),
							&partition->node.node,
							"light-all"
						);
					if((partition->light_state^data[4])&(1<<1))
						smcp_trigger_event_with_node(
							smcp_node_get_root((smcp_node_t)node),
							&partition->node.node,
							"light-1"
						);
					if((partition->light_state^data[4])&(1<<2))
						smcp_trigger_event_with_node(
							smcp_node_get_root((smcp_node_t)node),
							&partition->node.node,
							"light-2"
						);
					if((partition->light_state^data[4])&(1<<3))
						smcp_trigger_event_with_node(
							smcp_node_get_root((smcp_node_t)node),
							&partition->node.node,
							"light-3"
						);
					if((partition->light_state^data[4])&(1<<4))
						smcp_trigger_event_with_node(
							smcp_node_get_root((smcp_node_t)node),
							&partition->node.node,
							"light-4"
						);
					if((partition->light_state^data[4])&(1<<5))
						smcp_trigger_event_with_node(
							smcp_node_get_root((smcp_node_t)node),
							&partition->node.node,
							"light-5"
						);
					if((partition->light_state^data[4])&(1<<6))
						smcp_trigger_event_with_node(
							smcp_node_get_root((smcp_node_t)node),
							&partition->node.node,
							"light-6"
						);
					if((partition->light_state^data[4])&(1<<7))
						smcp_trigger_event_with_node(
							smcp_node_get_root((smcp_node_t)node),
							&partition->node.node,
							"light-7"
						);
					if(((partition->light_state>>8)^data[5])&(1<<0))
						smcp_trigger_event_with_node(
							smcp_node_get_root((smcp_node_t)node),
							&partition->node.node,
							"light-8"
						);
					if(((partition->light_state>>8)^data[5])&(1<<1))
						smcp_trigger_event_with_node(
							smcp_node_get_root((smcp_node_t)node),
							&partition->node.node,
							"light-9"
						);
					partition->light_state = data[4]+(data[5]<<8);
				}
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

smcp_status_t
ge_system_request_handler(
	struct ge_system_state_s* node,
	smcp_method_t	method
) {
	return SMCP_STATUS_OK;
}



int main(int argc, const char* argv[]) {
	smcp_t smcp = smcp_create(0);

	struct ge_system_state_s system_state_node = { };

	smcp_node_init(&system_state_node.node, smcp_get_root_node(smcp), "security");

	log_msg(LOG_LEVEL_NOTICE,"Listening on port %d.",smcp_get_port(smcp));

	ge_rs232_t interface = ge_rs232_init(&system_state_node.interface);
	interface->received_message = (void*)&received_message;
	interface->send_byte = &send_byte;
	interface->context = (void*)&system_state_node;

	smcp_pairing_init(smcp_get_root_node(smcp),SMCP_PAIRING_DEFAULT_ROOT_PATH);

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

	/*
	smcp_pair_with_uri(
		smcp,
		"security/zone-5/zs.tripped",
		//"coap://localhost/security/p-1/keypress?v=[10]0",
		"/security/p-1/keypress?v=[10]0",
		SMCP_PARING_FLAG_RELIABILITY_PART,
		NULL
	);
	*/
	smcp_set_proxy_url(smcp,"coap://bellatrix.orion.deepdarc.com/proxy");

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

	// Make sure we at least have the first partition set up.
	ge_get_partition(&system_state_node,1);

	refresh_equipment_list(interface);

	while(!feof(stdin)) {
		struct pollfd polltable[2] = {
			{ fileno(stdin), POLLIN | POLLHUP, 0 },
			{ smcp_get_fd(smcp), POLLIN | POLLHUP, 0 },
		};

		if(poll(polltable, 2, smcp_get_timeout(smcp)) < 0)
			break;

		// GE serial input.
		if(polltable[0].revents) {
			ge_rs232_status_t status;
			char byte = fgetc(stdin);

			if(byte==GE_RS232_NAK) {
				log_msg(LOG_LEVEL_WARNING,"GOT NAK");
			}

			if(byte==GE_RS232_ACK) {
				log_msg(LOG_LEVEL_DEBUG,"GOT ACK");
			}
			status = ge_rs232_receive_byte(interface,byte);

		}
		if(
			ge_rs232_ready_to_send(interface)==GE_RS232_STATUS_TIMEOUT
		) {
			if(interface->output_attempt_count<3) {
				ge_rs232_resend_last_message(interface);
			} else if(interface->got_response) {
				interface->got_response(interface->context,interface,false);
			}
		}

		smcp_process(smcp, 0);
	}

	return 0;
}
