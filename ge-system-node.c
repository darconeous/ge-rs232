

#include <smcp/assert_macros.h>
#include <stdio.h>
#include "ge-rs232.h"
#include "ge-system-node.h"
#include <string.h>
#include <stdlib.h>
#include <smcp/smcp.h>
#include <smcp/smcp-node.h>
#include <smcp/smcp-pairing.h>
#include <poll.h>
#include <termios.h>
#include <stdarg.h>
#include <fcntl.h>

#define GE_RS232_MAX_ZONES				(96)
#define GE_RS232_MAX_PARTITIONS			(6)
#define GE_RS232_MAX_SCHEDULES			(16)

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
//	static bool did_start_syslog;
//	if(!did_start_syslog) {
//		openlog("ge-rs232",LOG_PERROR|LOG_CONS,LOG_DAEMON);
//		did_start_syslog = true;
//	}
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

typedef struct {
	smcp_t smcp;
	struct smcp_async_response_s async_response;
	ge_rs232_status_t status;
	smcp_transaction_t transaction;
} panel_response_context;

static smcp_status_t
async_response_ack_handler(int statuscode, panel_response_context* context) {
    struct smcp_async_response_s* async_response = &context->async_response;
	smcp_finish_async_response(async_response);
	free(context);
	return SMCP_STATUS_OK;
}

static smcp_status_t
resend_async_response(panel_response_context *context) {
    smcp_status_t ret = 0;
    struct smcp_async_response_s* async_response = &context->async_response;

	if(context->status == GE_RS232_STATUS_OK) {
        ret = smcp_outbound_begin_response(COAP_RESULT_204_CHANGED);
	} else {
        ret = smcp_outbound_begin_response(COAP_RESULT_500_INTERNAL_SERVER_ERROR);
	}

	require_noerr(ret,bail);

    ret = smcp_outbound_set_async_response(async_response);
    require_noerr(ret,bail);

	if(context->status == GE_RS232_STATUS_NAK)
		smcp_outbound_set_content_formatted("NAK");
	else if(context->status == GE_RS232_STATUS_TIMEOUT)
		smcp_outbound_set_content_formatted("ACK-TIMEOUT");

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

void got_panel_response(void* c, ge_rs232_status_t status) {
	panel_response_context* context = c;
	if(!context)
		return;
	context->status = status;
	context->transaction = NULL;
	context->transaction = smcp_transaction_init(
		context->transaction,
		0,
		(void*)&resend_async_response,
		(void*)&async_response_ack_handler,
		c
	);

	smcp_transaction_begin(
		context->smcp,
		context->transaction,
		5*1000    // Retry for five seconds.
	);
}

static panel_response_context* new_panel_response_context() {
	panel_response_context* ret = calloc(sizeof(panel_response_context),1);
	smcp_start_async_response(&ret->async_response,0);
	ret->smcp = smcp_get_current_instance();

	return ret;
}

const char* ge_text_to_ascii_one_line(const char* bytes, uint8_t len) {
	static char ret[1024];
	ret[0] = 0;
	// TODO: Optimize!
	while(len--) {
		const char* str = ge_rs232_text_token_lookup[*bytes++];
		if(str) {
			if(str[0]=='\n') {
				if(len)
					strlcat(ret,isspace(ret[strlen(ret)-1])?"| ":" | ",sizeof(ret));
			} else if(str[0]=='\b') {
				// Backspace
				if(ret[0])
					ret[strlen(ret)-1] = 0;
			} else {
				strlcat(ret,str,sizeof(ret));
			}
		} else {
			strlcat(ret,"?",sizeof(ret));
		}
	}
	// Remove trailing whitespace.
	for(len=strlen(ret);len && isspace(ret[len-1]);len--)
		ret[len-1] = 0;
	return ret;
}

const char* ge_text_to_ascii(const char* bytes, uint8_t len) {
	static char ret[1024];
	ret[0] = 0;
	// TODO: Optimize!
	while(len--) {
		const char* str = ge_rs232_text_token_lookup[*bytes++];
		if(str) {
			if(str[0]=='\b') {
				// Backspace
				if(ret[0])
					ret[strlen(ret)-1] = 0;
			} else {
				strlcat(ret,str,sizeof(ret));
			}
		} else {
			strlcat(ret,"?",sizeof(ret));
		}
	}
	// Remove trailing whitespace.
	for(len=strlen(ret);len && isspace(ret[len-1]);len--)
		ret[len-1] = 0;
	return ret;
}

static const uint8_t refresh_equipment_msg[] = { GE_RS232_ATP_EQUIP_LIST_REQUEST };
static const uint8_t dynamic_data_refresh_msg[] = { GE_RS232_ATP_DYNAMIC_DATA_REFRESH };

ge_rs232_status_t refresh_equipment_list(ge_queue_t	qinterface,
	void (*finished)(void* context,ge_rs232_status_t status),
	void* context
) {
	return ge_queue_message(qinterface,refresh_equipment_msg,sizeof(refresh_equipment_msg),finished,context);
}

ge_rs232_status_t dynamic_data_refresh(ge_queue_t qinterface,
	void (*finished)(void* context,ge_rs232_status_t status),
	void* context
) {
	return ge_queue_message(qinterface,dynamic_data_refresh_msg,sizeof(dynamic_data_refresh_msg),finished,context);
}


ge_rs232_status_t send_keypress(ge_queue_t qinterface,uint8_t partition, uint8_t area, char* keys,
	void (*finished)(void* context,ge_rs232_status_t status),
	void* context
) {
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
	return ge_queue_message(qinterface,msg,len,finished,context);
}

	enum {
		PATH_ARM_LEVEL=0,
		PATH_ARMED_BY,
		PATH_ARM_DATE,
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
//		PATH_ENTRY_DELAY_REMAINING,
//		PATH_EXIT_DELAY_REMAINING,
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

const char* ge_user_to_cstr(char* dest, int user) {
	static char static_string[128];
	if(!dest)
		dest = static_string;

	if(user>=230 && user<=237) {
		snprintf(dest,sizeof(static_string),"P%d-MASTER-CODE");
	} else if(user>=238 && user<=245) {
		snprintf(dest,sizeof(static_string),"P%d-DURESS-CODE");
	} else if(user==246) {
		strncpy(dest,"SYSTEM-CODE",sizeof(static_string));
	} else if(user==247) {
		strncpy(dest,"INSTALLER",sizeof(static_string));
	} else if(user==248) {
		strncpy(dest,"DEALER",sizeof(static_string));
	} else if(user==249) {
		strncpy(dest,"AVM-CODE",sizeof(static_string));
	} else if(user==250) {
		strncpy(dest,"QUICK-ARM",sizeof(static_string));
	} else if(user==251) {
		strncpy(dest,"KEY-SWITCH",sizeof(static_string));
	} else if(user==252) {
		strncpy(dest,"SYSTEM",sizeof(static_string));
	} else if(user==255) {
		strncpy(dest,"AUTOMATION",sizeof(static_string));
	} else if(user==65535) {
		strncpy(dest,"SYSTEM/KEY-SWITCH",sizeof(static_string));
	} else {
		snprintf(dest,sizeof(static_string),"USER-%d",user);
	}
	return dest;
}


static smcp_status_t
partition_node_var_func(
	struct ge_partition_s *node,
	uint8_t action,
	uint8_t path,
	char* value
) {
	smcp_status_t ret = 0;

	if(path>=PATH_COUNT) {
		ret = SMCP_STATUS_NOT_FOUND;
	} else if(action==SMCP_VAR_GET_KEY) {
		static const char* path_names[] = {
			"arm-level",
			"armed-by",
			"arm-date",
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
//			"entry-delay-remaining",
//			"exit-delay-remaining",
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
			ge_user_to_cstr(value,node->armed_by);
		} else {
			ret = SMCP_STATUS_NOT_ALLOWED;
		}
	} else if(action==SMCP_VAR_GET_MAX_AGE) {
		int v = 0;
		switch(path) {
			case PATH_TOUCHPAD_TEXT:
				v = (node->arming_level<=1)?60:240;
				break;
			case PATH_ARM_LEVEL:
			case PATH_ARMED_BY:
			case PATH_ARM_DATE:
			case PATH_FS_CHIME:
			case PATH_FS_ENERGY_SAVER:
			case PATH_FS_NO_DELAY:
			case PATH_FS_LATCHKEY:
			case PATH_FS_SILENT_ARMING:
			case PATH_FS_QUICK_ARM:
			case PATH_LIGHT_ALL:
			case PATH_LIGHT_1:
			case PATH_LIGHT_2:
			case PATH_LIGHT_3:
			case PATH_LIGHT_4:
			case PATH_LIGHT_5:
			case PATH_LIGHT_6:
			case PATH_LIGHT_7:
			case PATH_LIGHT_8:
			case PATH_LIGHT_9:
				v = 3600;
				break;
			default:
				ret = SMCP_STATUS_NOT_ALLOWED;
				break;
		}
		if(v) {
			sprintf(value,"%d",v);
		} else {
			ret = SMCP_STATUS_NOT_ALLOWED;
		}
	} else if(action==SMCP_VAR_GET_OBSERVABLE) {
		switch(path) {
			case PATH_TOUCHPAD_TEXT:
			case PATH_ARM_LEVEL:
			case PATH_ARMED_BY:
			case PATH_ARM_DATE:
			case PATH_FS_CHIME:
			case PATH_FS_ENERGY_SAVER:
			case PATH_FS_NO_DELAY:
			case PATH_FS_LATCHKEY:
			case PATH_FS_SILENT_ARMING:
			case PATH_FS_QUICK_ARM:
			case PATH_LIGHT_ALL:
			case PATH_LIGHT_1:
			case PATH_LIGHT_2:
			case PATH_LIGHT_3:
			case PATH_LIGHT_4:
			case PATH_LIGHT_5:
			case PATH_LIGHT_6:
			case PATH_LIGHT_7:
			case PATH_LIGHT_8:
			case PATH_LIGHT_9:
				ret = SMCP_STATUS_OK;
				break;
			default:
				ret = SMCP_STATUS_NOT_ALLOWED;
				break;
		}
	} else if(action==SMCP_VAR_GET_VALUE) {
		if(path==PATH_TOUCHPAD_TEXT) {
			// Just send the ascii for now.
			int i = 0;
			value[0]=0;
			strcpy(value,ge_text_to_ascii(node->touchpad_lcd,node->touchpad_lcd_len));

/*
			for(;i<node->touchpad_lcd_len;i++) {
				const char* str = ge_rs232_text_token_lookup[node->touchpad_lcd[i]];

				if(str) {
					strlcat(value,str,SMCP_VARIABLE_MAX_VALUE_LENGTH);
				}
			}
*/
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
			else if(path==PATH_ARM_DATE)
				v = node->arm_date;
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
			struct ge_system_node_s* system_state=(struct ge_system_node_s*)node->node.node.parent;
			if((node->arming_level)==atoi(value)) {
				// arm level already set!
				ret = SMCP_STATUS_OK;
			} else {
				switch(atoi(value)) {
					case 1:
						send_keypress(&system_state->qinterface,node->partition_number,0,"5[20]",&got_panel_response,new_panel_response_context());
						ret = SMCP_STATUS_ASYNC_RESPONSE;
						break;
					case 2:
						send_keypress(&system_state->qinterface,node->partition_number,0,"5[28]",&got_panel_response,new_panel_response_context());
						ret = SMCP_STATUS_ASYNC_RESPONSE;
						break;
					case 3:
						send_keypress(&system_state->qinterface,node->partition_number,0,"5[27]",&got_panel_response,new_panel_response_context());
						ret = SMCP_STATUS_ASYNC_RESPONSE;
						break;
					default:
						log_msg(LOG_LEVEL_WARNING,"Bad arming level \"%s\"",value);
						return SMCP_STATUS_FAILURE;
				}
//				ret = smcp_start_async_response(&system_state->async_response,0);
//				require_noerr(ret,bail);
//				system_state->interface.response_context = system_state;
//				system_state->interface.got_response=(void*)&got_panel_response;
//				ret = SMCP_STATUS_ASYNC_RESPONSE;
//			} else {
//				smcp_outbound_drop();
//				log_msg(LOG_LEVEL_WARNING,"Too busy to set arming level. Dropping packet.");
//				ret = SMCP_STATUS_FAILURE;
			}
		} else if(path>=PATH_LIGHT_ALL && path<=PATH_LIGHT_9) {
			struct ge_system_node_s* system_state=(struct ge_system_node_s*)node->node.node.parent;
			char cmd[] = { '[','1','1'-!!atoi(value),']','0'+path-PATH_LIGHT_ALL,0};
			if(!!(node->light_state & (1<<(path-PATH_LIGHT_ALL)))==atoi(value)) {
				// Light already set!
				ret = SMCP_STATUS_OK;
			} else if(0== send_keypress(&system_state->qinterface,node->partition_number,0,cmd,&got_panel_response,new_panel_response_context())) {
//				ret = smcp_start_async_response(&system_state->async_response,0);
//				require_noerr(ret,bail);
//				system_state->interface.response_context = system_state;
//				system_state->interface.got_response=(void*)&got_panel_response;
				ret = SMCP_STATUS_ASYNC_RESPONSE;
			} else {
				log_msg(LOG_LEVEL_WARNING,"Too busy to change light. Dropping packet.");
				smcp_outbound_drop();
				ret = SMCP_STATUS_FAILURE;
			}
		} else if(path==PATH_FS_CHIME) {
			struct ge_system_node_s* system_state=(struct ge_system_node_s*)node->node.node.parent;
			if((node->feature_state & (1<<0))==atoi(value)) {
				// Chime already set!
				ret = SMCP_STATUS_OK;
			} else if(0== send_keypress(&system_state->qinterface,node->partition_number,0,"71",&got_panel_response,new_panel_response_context())) {
//				ret = smcp_start_async_response(&system_state->async_response,0);
//				require_noerr(ret,bail);
//				system_state->interface.response_context = system_state;
//				system_state->interface.got_response=(void*)&got_panel_response;
				ret = SMCP_STATUS_ASYNC_RESPONSE;
			} else {
				log_msg(LOG_LEVEL_WARNING,"Too busy to set chime. Dropping packet.");
				smcp_outbound_drop();
				ret = SMCP_STATUS_FAILURE;
			}
		} else if(path==PATH_REFRESH_EQUIPMENT) {
			struct ge_system_node_s* system_state=(struct ge_system_node_s*)node->node.node.parent;
			if(GE_RS232_STATUS_OK!=ge_queue_message(&system_state->qinterface,refresh_equipment_msg,sizeof(refresh_equipment_msg),&got_panel_response,new_panel_response_context()))
				ret = SMCP_STATUS_FAILURE;
			else
				ret = SMCP_STATUS_ASYNC_RESPONSE;
//			if(!system_state->interface.got_response) {
//				ret = smcp_start_async_response(&system_state->async_response,0);
//				require_noerr(ret,bail);
//				system_state->interface.response_context = system_state;
//				system_state->interface.got_response=(void*)&got_panel_response;
				ret = SMCP_STATUS_ASYNC_RESPONSE;
//			}
		} else if(path==PATH_KEYPRESS) {
			struct ge_system_node_s* system_state=(struct ge_system_node_s*)node->node.node.parent;
			if(0 == send_keypress(&system_state->qinterface,node->partition_number,0,value,&got_panel_response,new_panel_response_context())) {
//				ret = smcp_start_async_response(&system_state->async_response,0);
//				require_noerr(ret,bail);
//				system_state->interface.response_context = system_state;
//				system_state->interface.got_response=(void*)&got_panel_response;
				ret = SMCP_STATUS_ASYNC_RESPONSE;
			} else {
				log_msg(LOG_LEVEL_WARNING,"Too busy to send keypresses. Dropping packet.");
				smcp_outbound_drop();
				ret = SMCP_STATUS_FAILURE;
			}
		} else if(path==PATH_DDR) {
			struct ge_system_node_s* system_state=(struct ge_system_node_s*)node->node.node.parent;
			if(GE_RS232_STATUS_OK!=ge_queue_message(&system_state->qinterface,dynamic_data_refresh_msg,sizeof(dynamic_data_refresh_msg),&got_panel_response,new_panel_response_context()))
				ret = SMCP_STATUS_FAILURE;
			else
				ret = SMCP_STATUS_ASYNC_RESPONSE;

//			if(!system_state->interface.got_response) {
//				dynamic_data_refresh(&system_state->interface);
//				ret = smcp_start_async_response(&system_state->async_response,0);
//				require_noerr(ret,bail);
//				system_state->interface.response_context = system_state;
//				system_state->interface.got_response=(void*)&got_panel_response;
//				ret = SMCP_STATUS_ASYNC_RESPONSE;
//			} else {
//				log_msg(LOG_LEVEL_WARNING,"Too busy to send dynamic data refresh. Dropping packet.");
//				smcp_outbound_drop();
//				ret = SMCP_STATUS_FAILURE;
//			}
		} else {
			ret = SMCP_STATUS_NOT_ALLOWED;
		}
	} else {
		ret = SMCP_STATUS_NOT_IMPLEMENTED;
	}
bail:
	return ret;
}

	enum {
		PATH_PARTITION=0,
		PATH_AREA,
		PATH_GROUP,
		PATH_TYPE,
		PATH_TEXT,
		PATH_LAST_TRIPPED,
		PATH_STATUS_TRIPPED,
		PATH_STATUS_FAULT,
		PATH_STATUS_ALARM,
		PATH_STATUS_TROUBLE,
		PATH_STATUS_BYPASS,

		PATH_ZONE_COUNT,
	};

static smcp_status_t
zone_node_var_func(
	struct ge_zone_s *node,
	uint8_t action,
	uint8_t path,
	char* value
) {
	smcp_status_t ret = 0;

	if(path>=PATH_ZONE_COUNT) {
		ret = SMCP_STATUS_NOT_FOUND;
	} else if(action==SMCP_VAR_GET_KEY) {
		static const char* path_names[] = {
			"pn",
			"an",
			"gn",
			"zt",
			"text",
			"last-tripped",
			"zs.tripped",
			"zs.fault",
			"zs.alarm",
			"zs.trouble",
			"zs.bypass",
		};
		strcpy(value,path_names[path]);
	} else if(action==SMCP_VAR_GET_MAX_AGE) {
		int v = 0;
		switch(path) {
			case PATH_PARTITION:
			case PATH_AREA:
			case PATH_GROUP:
			case PATH_TYPE:
			case PATH_TEXT:
				v = 3600;
				break;
			case PATH_LAST_TRIPPED:
			case PATH_STATUS_TRIPPED:
			case PATH_STATUS_FAULT:
			case PATH_STATUS_ALARM:
			case PATH_STATUS_TROUBLE:
			case PATH_STATUS_BYPASS:
				v = 60*5;
				break;
			default:
				ret = SMCP_STATUS_NOT_ALLOWED;
				break;
		}
		if(v) {
			sprintf(value,"%d",v);
		} else {
			ret = SMCP_STATUS_NOT_ALLOWED;
		}
	} else if(action==SMCP_VAR_GET_OBSERVABLE) {
		switch(path) {
			case PATH_LAST_TRIPPED:
			case PATH_STATUS_TRIPPED:
			case PATH_STATUS_FAULT:
			case PATH_STATUS_ALARM:
			case PATH_STATUS_TROUBLE:
			case PATH_STATUS_BYPASS:
				ret = SMCP_STATUS_OK;
				break;
			default:
				ret = SMCP_STATUS_NOT_ALLOWED;
				break;
		}
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
			else if(path==PATH_LAST_TRIPPED)
				v = node->last_tripped;
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
ge_get_zone(struct ge_system_node_s *node,int zonei) {
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
ge_get_partition(struct ge_system_node_s *node,int partitioni) {
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

bool
should_bypass_gate(void) {
	struct tm* local;
	time_t t;

	time(&t);

	local = localtime(&t);

//	return 1;
	return local->tm_wday==3
		&& local->tm_hour>=7
		&& local->tm_hour<=12+5;
}

static time_t next_lawn_care_hack_check;

void
lawn_care_hack_check(struct ge_system_node_s *self) {
	struct ge_partition_s* partition = ge_get_partition(self,1);
	struct ge_zone_s* zone = ge_get_zone(self,18);

	ge_rs232_status_t status = ge_rs232_ready_to_send(&self->interface);
	if(status == GE_RS232_STATUS_WAIT) {
		next_lawn_care_hack_check = time(NULL)+2;
		return;
	}
	static bool did_run;

	if(!did_run && partition->arming_level==2 || partition->arming_level==3) {
		bool gate_is_bypassed = !!(zone->status&GE_RS232_ZONE_STATUS_BYPASSED);
		if(gate_is_bypassed^should_bypass_gate()) {
			const char* code = self->system_code;
			char bypass_command[40];
			if(code[0]) {
//				did_run = 1;
				snprintf(bypass_command,sizeof(bypass_command),"#%s18",code);
				//log_msg(LOG_LEVEL_ERROR,"LAWN HACK KEYPRESS: %s",bypass_command);
				//zone->status|=GE_RS232_ZONE_STATUS_BYPASSED;
				send_keypress(&self->qinterface,partition->partition_number,0,bypass_command,NULL,NULL);
			}
		}
	} else if(partition->arming_level==1) {
		did_run = 0;
	}

	next_lawn_care_hack_check = time(NULL)+60;
}

ge_rs232_status_t
received_message(struct ge_system_node_s *node, const uint8_t* data, uint8_t len,struct ge_rs232_s* interface) {

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
		ge_rs232_status_t status = dynamic_data_refresh(&node->qinterface,NULL,NULL);
		len=0;
		return 0;
	} else if(data[0]==GE_RS232_PTA_ZONE_STATUS) {
		int zonei = (data[3]<<8)+data[4];

		struct ge_zone_s* zone = ge_get_zone(node,zonei);

		if(zone) {
			if((zone->status^data[5])&GE_RS232_ZONE_STATUS_TRIPPED) {
				if((data[5]&GE_RS232_ZONE_STATUS_TRIPPED)) {
					zone->last_tripped = time(NULL);
					smcp_variable_node_did_change(&zone->node,PATH_LAST_TRIPPED,NULL);
				}

				smcp_variable_node_did_change(&zone->node,PATH_STATUS_TRIPPED,(data[5]&GE_RS232_ZONE_STATUS_TRIPPED)?"v=1":"v=0");
			}
			if((zone->status^data[5])&GE_RS232_ZONE_STATUS_FAULT)
				smcp_variable_node_did_change(&zone->node,PATH_STATUS_FAULT,(data[5]&GE_RS232_ZONE_STATUS_FAULT)?"v=1":"v=0");
			if((zone->status^data[5])&GE_RS232_ZONE_STATUS_TROUBLE)
				smcp_variable_node_did_change(&zone->node,PATH_STATUS_TROUBLE,(data[5]&GE_RS232_ZONE_STATUS_TROUBLE)?"v=1":"v=0");
			if((zone->status^data[5])&GE_RS232_ZONE_STATUS_ALARM)
				smcp_variable_node_did_change(&zone->node,PATH_STATUS_ALARM,(data[5]&GE_RS232_ZONE_STATUS_ALARM)?"v=1":"v=0");
			if((zone->status^data[5])&GE_RS232_ZONE_STATUS_BYPASSED)
				smcp_variable_node_did_change(&zone->node,PATH_STATUS_BYPASS,(data[5]&GE_RS232_ZONE_STATUS_BYPASSED)?"v=1":"v=0");
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

			//if((zone->status^data[7])&GE_RS232_ZONE_STATUS_TRIPPED)
			//	smcp_variable_node_did_change(&zone->node,PATH_STATUS_TRIPPED,NULL);
			if((zone->status^data[7])&GE_RS232_ZONE_STATUS_FAULT)
				smcp_variable_node_did_change(&zone->node,PATH_STATUS_FAULT,(data[7]&GE_RS232_ZONE_STATUS_FAULT)?"v=1":"v=0");
			if((zone->status^data[7])&GE_RS232_ZONE_STATUS_TROUBLE)
				smcp_variable_node_did_change(&zone->node,PATH_STATUS_TROUBLE,(data[7]&GE_RS232_ZONE_STATUS_TROUBLE)?"v=1":"v=0");
			if((zone->status^data[7])&GE_RS232_ZONE_STATUS_ALARM)
				smcp_variable_node_did_change(&zone->node,PATH_STATUS_ALARM,(data[7]&GE_RS232_ZONE_STATUS_ALARM)?"v=1":"v=0");
			if((zone->status^data[7])&GE_RS232_ZONE_STATUS_BYPASSED)
				smcp_variable_node_did_change(&zone->node,PATH_STATUS_BYPASS,(data[7]&GE_RS232_ZONE_STATUS_BYPASSED)?"v=1":"v=0");
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
			"?",
			data[7]&GE_RS232_ZONE_STATUS_FAULT?"F":"-",
			data[7]&GE_RS232_ZONE_STATUS_ALARM?"A":"-",
			data[7]&GE_RS232_ZONE_STATUS_TROUBLE?"R":"-",
			data[7]&GE_RS232_ZONE_STATUS_BYPASSED?"B":"-",
			ge_text_to_ascii_one_line(data+8,len-8)

		);
		return 0;
		len = 0;
	} else if(data[0]==GE_RS232_PTA_EQUIP_LIST_USER_DATA) {
		uint16_t user = (data[1]<<8)+data[2];
		char code_backing[5];
		char* code = code_backing;

		if(user == 246) {
			code = node->system_code;
		} else if(user == 247) {
			code = node->installer_code;
		} else if(user>=230 && user<=237) {
			// Partition master code
		} else if(user>=238 && user<=245) {
			// Partition duress code
		}

		if(code) {
			code[0] = (data[4]>>4)+'0';
			code[1] = (data[4]&0xF)+'0';
			code[2] = (data[5]>>4)+'0';
			code[3] = (data[5]&0xF)+'0';
			code[4] = 0;

			// Check for the zero code, which is invalid.
			if(strcmp(code,"0000")==0)
				code[0] = 0;

#if DEBUG
			log_msg(LOG_LEVEL_DEBUG,
				"[EQUIP_LIST_USER_DATA] USER:\"%s\"(%d) CODE=\"%s\"",
				ge_user_to_cstr(NULL,user),
				user,
				code
			);
#endif
		}
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
		ge_rs232_status_t status = dynamic_data_refresh(&node->qinterface,NULL,NULL);
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
					char arm_level_changed[] = { 'v','=','0'+data[6],0 };
					partition->arming_level = data[6];
					partition->armed_by = (data[4]<<8)+(data[5]);
					partition->arm_date = time(NULL);
					smcp_variable_node_did_change(&partition->node,PATH_ARM_LEVEL,arm_level_changed);
					smcp_variable_node_did_change(&partition->node,PATH_ARM_DATE,NULL);
					smcp_variable_node_did_change(&partition->node,PATH_ARMED_BY,NULL);
					if(partition->arming_level == 1) {
						lawn_care_hack_check(node);
					} else {
						next_lawn_care_hack_check = time(NULL)+60;
					}
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
				int partitioni = data[2];
				struct ge_partition_s* partition = ge_get_partition(node,partitioni);
				if(partition) {
					bool new_value = !!(data[4]&(1<<7));
					if(data[4]&(1<<6)) {
						partition->exit_delay_active = new_value;
					} else {
						partition->entry_delay_active = new_value;
					}
				}
				lawn_care_hack_check(node);
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
					smcp_variable_node_did_change(&partition->node,PATH_TOUCHPAD_TEXT,NULL);

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
					if((partition->feature_state^data[4])&(1<<0))
						smcp_variable_node_did_change(&partition->node,PATH_FS_CHIME,(data[4]&(1<<0))?"v=1":"v=0");
					if((partition->feature_state^data[4])&(1<<1))
						smcp_variable_node_did_change(&partition->node,PATH_FS_ENERGY_SAVER,(data[4]&(1<<1))?"v=1":"v=0");
					if((partition->feature_state^data[4])&(1<<2))
						smcp_variable_node_did_change(&partition->node,PATH_FS_NO_DELAY,(data[4]&(1<<2))?"v=1":"v=0");
					if((partition->feature_state^data[4])&(1<<3))
						smcp_variable_node_did_change(&partition->node,PATH_FS_LATCHKEY,(data[4]&(1<<3))?"v=1":"v=0");
					if((partition->feature_state^data[4])&(1<<4))
						smcp_variable_node_did_change(&partition->node,PATH_FS_SILENT_ARMING,(data[4]&(1<<4))?"v=1":"v=0");
					if((partition->feature_state^data[4])&(1<<5))
						smcp_variable_node_did_change(&partition->node,PATH_FS_QUICK_ARM,(data[4]&(1<<5))?"v=1":"v=0");
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
						smcp_variable_node_did_change(&partition->node,PATH_LIGHT_ALL,(data[4]&(1<<0))?"v=1":"v=0");
					if((partition->light_state^data[4])&(1<<1))
						smcp_variable_node_did_change(&partition->node,PATH_LIGHT_1,(data[4]&(1<<1))?"v=1":"v=0");
					if((partition->light_state^data[4])&(1<<2))
						smcp_variable_node_did_change(&partition->node,PATH_LIGHT_2,(data[4]&(1<<2))?"v=1":"v=0");
					if((partition->light_state^data[4])&(1<<3))
						smcp_variable_node_did_change(&partition->node,PATH_LIGHT_3,(data[4]&(1<<3))?"v=1":"v=0");
					if((partition->light_state^data[4])&(1<<4))
						smcp_variable_node_did_change(&partition->node,PATH_LIGHT_4,(data[4]&(1<<4))?"v=1":"v=0");
					if((partition->light_state^data[4])&(1<<5))
						smcp_variable_node_did_change(&partition->node,PATH_LIGHT_5,(data[4]&(1<<5))?"v=1":"v=0");
					if((partition->light_state^data[4])&(1<<6))
						smcp_variable_node_did_change(&partition->node,PATH_LIGHT_6,(data[4]&(1<<6))?"v=1":"v=0");
					if((partition->light_state^data[4])&(1<<7))
						smcp_variable_node_did_change(&partition->node,PATH_LIGHT_7,(data[4]&(1<<7))?"v=1":"v=0");
					if(((partition->light_state>>8)^data[5])&(1<<0))
						smcp_variable_node_did_change(&partition->node,PATH_LIGHT_8,(data[4]&(1<<8))?"v=1":"v=0");
					if(((partition->light_state>>8)^data[5])&(1<<1))
						smcp_variable_node_did_change(&partition->node,PATH_LIGHT_9,(data[4]&(1<<9))?"v=1":"v=0");
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
	ge_system_node_t self = (void*)context;
	fputc(byte,self->serial_out);
	fflush(self->serial_out);
	return GE_RS232_STATUS_OK;
}

smcp_status_t
ge_system_request_handler(
	struct ge_system_node_s* node,
	smcp_method_t	method
) {
	return SMCP_STATUS_OK;
}










void
ge_system_node_dealloc(ge_system_node_t x) {
	free(x);
}

ge_system_node_t
ge_system_node_alloc() {
	ge_system_node_t ret =
	    (ge_system_node_t)calloc(sizeof(struct ge_system_node_s), 1);

	ret->node.finalize = (void (*)(smcp_node_t)) &ge_system_node_dealloc;
	return ret;
}

static smcp_status_t
reset_serial(ge_system_node_t self, const char* device) {
	ge_rs232_t interface = &self->interface;
	int fd = -1;
	log_msg(LOG_LEVEL_NOTICE,"Opening \"%s\" . . .",device);
	fd = open(device,O_RDWR | O_NOCTTY | O_NDELAY);
	if(fd<0) {
		log_msg(LOG_LEVEL_CRITICAL,"Unable to open \"%s\"!",device);
		return -1;
	}
	self->serial_in = fdopen(fd,"r");
	self->serial_out = fdopen(fd,"w");
	setvbuf(self->serial_out, NULL, _IONBF, 0);
	setvbuf(self->serial_in, NULL, _IONBF, 0);

	int r;
	struct termios t;
	if(tcgetattr(fileno(self->serial_in), &t)==0) {
		cfmakeraw(&t);
		cfsetspeed(&t, 9600);
		t.c_cflag = CLOCAL | CREAD | CS8 | PARENB | PARODD;
		t.c_iflag = INPCK | IGNBRK;
		t.c_oflag = 0;
		t.c_lflag = 0;
		tcsetattr(fileno(self->serial_in), TCSANOW, &t);
	}
	if(tcgetattr(fileno(self->serial_out), &t)==0) {
		cfmakeraw(&t);
		cfsetspeed(&t, 9600);
		t.c_cflag = CLOCAL | CREAD | CS8 | PARENB | PARODD;
		t.c_iflag = IGNPAR | IGNBRK;
		t.c_oflag = 0;
		t.c_lflag = 0;
		tcsetattr(fileno(self->serial_out), TCSANOW, &t);
	}
	return 0;
}

ge_system_node_t
smcp_ge_system_node_init(
	ge_system_node_t self,
	smcp_node_t parent,
	const char* name
) {
	require(self || (self = ge_system_node_alloc()), bail);

	require(smcp_node_init(
		&self->node,
		(void*)parent,
		name
	), bail);

	ge_rs232_t interface = ge_rs232_init(&self->interface);
	struct ge_queue_s *qinterface = ge_queue_init(&self->qinterface,interface);
	interface->received_message = (void*)&received_message;
	interface->send_byte = &send_byte;
	interface->context = (void*)self;

	if(SMCP_STATUS_OK!=reset_serial(self,"/dev/ttyUSB0")) {
		if(SMCP_STATUS_OK!=reset_serial(self,"/dev/ttyUSB1")) {
			smcp_node_delete(&self->node);
			self = NULL;
			sleep(1);
			goto bail;
		}
	}

	// Make sure we at least have the first partition set up.
	ge_get_partition(self,1);

	dynamic_data_refresh(qinterface,NULL,NULL);
	refresh_equipment_list(qinterface,NULL,NULL);

bail:
	return self;
}

smcp_status_t
smcp_ge_system_node_update_fdset(
	ge_system_node_t self,
    fd_set *read_fd_set,
    fd_set *write_fd_set,
    fd_set *exc_fd_set,
    int *max_fd,
	cms_t *timeout
) {
	int fd = fileno(self->serial_in);

	//log_msg(LOG_LEVEL_CRITICAL,">>> Updated FDSET, fd=%d",fd);

	if(max_fd && *max_fd<fd)
		*max_fd = fd;

	if(read_fd_set && fd >= 0)
		FD_SET(fd,read_fd_set);

	if(exc_fd_set && fd >= 0)
		FD_SET(fd,exc_fd_set);

	if(timeout && *timeout/1000>(next_lawn_care_hack_check-time(NULL)))
		*timeout = MAX(60,next_lawn_care_hack_check-time(NULL))*1000;

	return 0;
}

smcp_status_t
smcp_ge_system_node_process(ge_system_node_t self) {
	smcp_status_t status = 0;

	struct pollfd polltable[] = {
		{ fileno(self->serial_in), POLLIN | POLLHUP, 0 },
	};

	if(feof(self->serial_in) || ferror(self->serial_in)) {
		status = reset_serial(self,"/dev/ttyUSB0");
		if(SMCP_STATUS_OK!=status) {
			status = reset_serial(self,"/dev/ttyUSB1");
		}
		if(SMCP_STATUS_OK!=status) {
			goto bail;
		}
	}

	if(poll(polltable, 1, 10) > 0) {
		ge_rs232_status_t status;
		char byte = fgetc(self->serial_in);

		if(byte==GE_RS232_NAK) {
			log_msg(LOG_LEVEL_WARNING,"GOT NAK");
		}

		if(byte==GE_RS232_ACK) {
			log_msg(LOG_LEVEL_DEBUG,"GOT ACK");
		}
		status = ge_rs232_receive_byte(&self->interface,byte);
	}

	ge_queue_update(&self->qinterface);

	if(
		ge_rs232_ready_to_send(&self->interface)==GE_RS232_STATUS_TIMEOUT
	) {
		if(self->interface.output_attempt_count<3) {
			ge_rs232_resend_last_message(&self->interface);
		} else if(self->interface.got_response) {
			self->interface.got_response(self->interface.response_context,&self->interface,false);
		}
	}

	if(time(NULL)>next_lawn_care_hack_check)
		lawn_care_hack_check(self);

bail:
	return status;
}



#if 0
int main(int argc, const char* argv[]) {
	smcp_t smcp = smcp_create(0);

	struct ge_system_node_s system_state_node = { };

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

#endif

