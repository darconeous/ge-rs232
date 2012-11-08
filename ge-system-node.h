
#ifndef __GE_SYSTEM_NODE_H__
#define __GE_SYSTEM_NODE_H__

#include <smcp/assert_macros.h>
#include "ge-rs232.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <smcp/smcp.h>
#include <smcp/smcp-node.h>
#include <smcp/smcp-pairing.h>
//#include <poll.h>
//#include <termios.h>
//#include <stdarg.h>
//#include <stdlib.h>
//#include <fcntl.h>

#define GE_RS232_MAX_ZONES				(96)
#define GE_RS232_MAX_PARTITIONS			(6)
#define GE_RS232_MAX_SCHEDULES			(16)

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
	time_t arm_date;

	uint8_t feature_state;
	uint16_t light_state;

	char label[16];
	uint8_t label_len;

	char touchpad_lcd[32];
	uint8_t touchpad_lcd_len;
};

struct ge_system_node_s {
	struct smcp_node_s node;

	struct ge_rs232_s interface;

	FILE* serial_in;
	FILE* serial_out;

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

typedef struct ge_system_node_s* ge_system_node_t;
typedef struct ge_partition_s* ge_partition_t;
typedef struct ge_zone_s* ge_zone_t;
typedef struct ge_schedule_s* ge_schedule_t;

const char* ge_text_to_ascii_one_line(const char* bytes, uint8_t len);
const char* ge_text_to_ascii(const char* bytes, uint8_t len);
struct ge_zone_s *ge_get_zone(struct ge_system_node_s *node,int zonei);

struct ge_partition_s *ge_get_partition(struct ge_system_node_s *node,int partitioni);

extern ge_system_node_t smcp_ge_system_node_init(
	ge_system_node_t self,
	smcp_node_t parent,
	const char* name
);

extern smcp_status_t smcp_ge_system_node_update_fdset(
	ge_system_node_t node,
    fd_set *read_fd_set,
    fd_set *write_fd_set,
    fd_set *exc_fd_set,
    int *max_fd,
	cms_t *timeout
);

extern smcp_status_t smcp_ge_system_node_process(ge_system_node_t node);



#endif

