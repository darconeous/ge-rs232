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
					if(*keys!=']')
						return -1;
				}
				break;
			default:
				return -1;
				break;
		}
		if(code==255)
			continue;
		msg[len++] = code;
	}
	return ge_rs232_send_message(interface,msg,len);
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

static smcp_status_t
partition_node_var_func(
	struct ge_partition_s *node,
	uint8_t action,
	uint8_t path,
	char* value
) {
/*
	uint8_t partition;

	uint8_t arming_level;

	uint16_t armed_by;
	uint8_t feature_state;
	uint16_t light_state;

	char label[16];
	uint8_t label_len;

	char touchpad_lcd[32];
	uint8_t touchpad_lcd_len;
*/


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
		PATH_TEXT,
		PATH_TOUCHPAD_TEXT,
		PATH_KEYPRESS,
		PATH_REFRESH_EQUIPMENT,
		PATH_DDR,
//		PATH_LIGHT_1,
//		PATH_LIGHT_2,
//		PATH_LIGHT_3,
//		PATH_LIGHT_4,
//		PATH_LIGHT_5,
//		PATH_LIGHT_6,
//		PATH_LIGHT_7,
//		PATH_LIGHT_8,
//		PATH_LIGHT_9,

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
			"text",
			"touchpad-text",
			"keypress",
			"refresh-equipment",
			"ddr",
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
		} else if(path==PATH_TOUCHPAD_TEXT) {
			// Just send the ascii for now.
			int i = 0;
			value[0]=0;
			for(;i<node->touchpad_lcd_len;i++) {
				const char* str = ge_rs232_text_token_lookup[node->touchpad_lcd[i]];

				if(str) {
					strlcat(value,str,SMCP_VARIABLE_MAX_VALUE_LENGTH);
				}
			}
		} else {
			int v = 0;

			if(path==PATH_ARM_LEVEL)
				v = node->arming_level;
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
					case 1: send_keypress(&system_state->interface,node->partition_number,0,"[20]");break;
					case 2: send_keypress(&system_state->interface,node->partition_number,0,"[28]");break;
					case 3: send_keypress(&system_state->interface,node->partition_number,0,"[27]");break;
					default: return SMCP_STATUS_FAILURE;
				}
				ret = smcp_start_async_response(&system_state->async_response);
				require_noerr(ret,bail);
				system_state->interface.got_response=(void*)&got_panel_response;
				ret = SMCP_STATUS_ASYNC_RESPONSE;
			} else {
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
				ret = smcp_start_async_response(&system_state->async_response);
				require_noerr(ret,bail);
				system_state->interface.got_response=(void*)&got_panel_response;
				ret = SMCP_STATUS_ASYNC_RESPONSE;
			} else {
				ret = SMCP_STATUS_FAILURE;
			}
		} else if(path==PATH_REFRESH_EQUIPMENT) {
			struct ge_system_state_s* system_state=(struct ge_system_state_s*)node->node.node.parent;
			if(!system_state->interface.got_response) {
				refresh_equipment_list(&system_state->interface);
				ret = smcp_start_async_response(&system_state->async_response);
				require_noerr(ret,bail);
				system_state->interface.got_response=(void*)&got_panel_response;
				ret = SMCP_STATUS_ASYNC_RESPONSE;
			}
		} else if(path==PATH_KEYPRESS) {
			struct ge_system_state_s* system_state=(struct ge_system_state_s*)node->node.node.parent;
			if(!system_state->interface.got_response
				&& (0 == send_keypress(&system_state->interface,node->partition_number,0,value))
			) {
				ret = smcp_start_async_response(&system_state->async_response);
				require_noerr(ret,bail);
				system_state->interface.got_response=(void*)&got_panel_response;
				ret = SMCP_STATUS_ASYNC_RESPONSE;
			} else {
				ret = SMCP_STATUS_FAILURE;
			}
		} else if(path==PATH_DDR) {
			struct ge_system_state_s* system_state=(struct ge_system_state_s*)node->node.node.parent;
			if(!system_state->interface.got_response) {
				dynamic_data_refresh(&system_state->interface);
				ret = smcp_start_async_response(&system_state->async_response);
				require_noerr(ret,bail);
				system_state->interface.got_response=(void*)&got_panel_response;
				ret = SMCP_STATUS_ASYNC_RESPONSE;
			} else {
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
			"zs.trip",
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
		int zonei = (data[3]<<8)+data[4];

		struct ge_zone_s* zone = ge_get_zone(node,zonei);

		if(zone) {
			if((zone->status^data[5])&GE_RS232_ZONE_STATUS_TRIPPED)
				smcp_trigger_event_with_node(
					smcp_node_get_root((smcp_node_t)node),
					&zone->node.node,
					"zs.tripped"
				);
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
		}

		fprintf(stderr," PN:%d AREA:%d ZONE:%d STATUS:",data[1],data[2],zonei);
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
		fprintf(stderr,"[EQUIP_LIST_ZONE_DATA]");
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

		fprintf(stderr," PN:%d AREA:%d ZONE:%d TYPE:%d GROUP:%d STATUS:",data[1],data[2],zonei,data[6],data[3]);
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
		ge_rs232_status_t status = ge_rs232_ready_to_send(interface);
		if(status != GE_RS232_STATUS_WAIT) {
			ge_rs232_status_t status = dynamic_data_refresh(interface);
			fprintf(stderr," refresh send status = %d",status);
		} else {
			fprintf(stderr," UNABLE TO SEND REFRESH: NOT READY");
		}

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
				{
				int partitioni = data[2];
				struct ge_partition_s* partition = ge_get_partition(node,partitioni);
				if(partition) {
					partition->arming_level = data[6];
					partition->armed_by = (data[4]<<8)+(data[5]);
				}
				}
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
				//if(data[2]!=1)
				//	return GE_RS232_STATUS_OK;

				fprintf(stderr,"[TOUCHPAD_DISPLAY]");
				fprintf(stderr," PN:%d AN:%d",data[2],data[3]);
				fprintf(stderr," MT:%d MSG:\"",data[4]);

				{
				int partitioni = data[2];
				struct ge_partition_s* partition = ge_get_partition(node,partitioni);
				if(partition) {
					smcp_trigger_event_with_node(
						smcp_node_get_root((smcp_node_t)node),
						&partition->node.node,
						"touchpad_text"
					);

					len-=5;
					data+=5;

					partition->touchpad_lcd_len = len;
					memcpy(partition->touchpad_lcd,data,len);

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
				}
				}
				break;
			case GE_RS232_PTA_SUBCMD_SIREN_STOP:
//				fprintf(stderr,"[SIREN_STOP]");
				len=0;
				break;
			case GE_RS232_PTA_SUBCMD_FEATURE_STATE:
				{
				int partitioni = data[2];
				struct ge_partition_s* partition = ge_get_partition(node,partitioni);
				if(partition) {
					partition->feature_state = data[4];
				}
				}
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

	fprintf(stderr,"Listening on port %d.\n",smcp_get_port(smcp));

	ge_rs232_t interface = ge_rs232_init(&system_state_node.interface);
	interface->received_message = (void*)&received_message;
	interface->send_byte = &send_byte;
	interface->context = (void*)&system_state_node;

	smcp_pairing_init(smcp_get_root_node(smcp),".pairing");

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
				fprintf(stderr,"GOT NAK\n");
			}

			if(byte==GE_RS232_ACK) {
				fprintf(stderr,"GOT ACK\n");
			}
			status = ge_rs232_receive_byte(interface,byte);

			if(status==GE_RS232_STATUS_NAK)
				fprintf(stderr,"N\n");
			else if(status==GE_RS232_STATUS_JUNK)
				fprintf(stderr,"<%02X>",byte);
			else if(status)
				fprintf(stderr,"[%d]",status);
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
