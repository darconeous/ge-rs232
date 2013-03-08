
#ifndef __GE_RS232_H__
#define __GE_RS232_H__

#include <stdint.h>
#include <stdbool.h>
#include <time.h>

#define GE_RS232_START_OF_MESSAGE	(0x0A)	// ASCII Line Feed
#define GE_RS232_ACK				(0x06)	// ASCII ACK
#define GE_RS232_NAK				(0x15)	// ASCII NAK

#define GE_RS232_MAX_MESSAGE_SIZE	(56)

#ifndef GE_QUEUE_MAX_MESSAGES
#define GE_QUEUE_MAX_MESSAGES		(8)
#endif


#define GE_RS232_STATUS_OK					(0)
#define GE_RS232_STATUS_ERROR				(-1)
#define GE_RS232_STATUS_WAIT				(-2)
#define GE_RS232_STATUS_NAK					(-3)
#define GE_RS232_STATUS_TIMEOUT				(-4)
#define GE_RS232_STATUS_MESSAGE_TOO_BIG		(-5)
#define GE_RS232_STATUS_JUNK				(-6)
#define GE_RS232_STATUS_BAD_CHECKSUM		(-7)
#define GE_RS232_STATUS_MESSAGE_TOO_SMALL	(-8)
#define GE_RS232_STATUS_QUEUE_FULL			(-9)

#define GE_RS232_ZONE_STATUS_TRIPPED		(1<<0)
#define GE_RS232_ZONE_STATUS_FAULT			(1<<1)
#define GE_RS232_ZONE_STATUS_ALARM			(1<<2)
#define GE_RS232_ZONE_STATUS_TROUBLE		(1<<3)
#define GE_RS232_ZONE_STATUS_BYPASSED		(1<<4)

#define GE_RS232_KEYPRESS_ARM_ARAY_NO_DELAY		(0x2B)


#define GE_RS232_PTA_PANEL_TYPE				(0x01)
#define GE_RS232_PTA_AUTOMATION_EVENT_LOST	(0x02)
#define GE_RS232_PTA_EQUIP_LIST_ZONE_DATA	(0x03)
#define GE_RS232_PTA_EQUIP_LIST_PARTITION_DATA	(0x04)
#define GE_RS232_PTA_EQUIP_LIST_SUPERBUS_DEV_DATA	(0x05)
#define GE_RS232_PTA_EQUIP_LIST_SUPERBUS_CAP_DATA	(0x06)
#define GE_RS232_PTA_EQUIP_LIST_OUTPUT_DATA	(0x07)
#define GE_RS232_PTA_EQUIP_LIST_USER_DATA	(0x09)
#define GE_RS232_PTA_EQUIP_LIST_SCHEDULE_DATA	(0x0A)
#define GE_RS232_PTA_EQUIP_LIST_SCHEDULED_EVENT_DATA	(0x0B)
#define GE_RS232_PTA_EQUIP_LIST_LIGHT_TO_SENSOR_DATA	(0x0C)
#define GE_RS232_PTA_EQUIP_LIST_COMPLETE	(0x08)
#define GE_RS232_PTA_CLEAR_AUTOMATION_DYNAMIC_IMAGE	(0x20)
#define GE_RS232_PTA_ZONE_STATUS			(0x21)
#define GE_RS232_PTA_SUBCMD					(0x22)
#	define GE_RS232_PTA_SUBCMD_LEVEL		(0x01)
#	define GE_RS232_PTA_SUBCMD_ALARM_TROUBLE	(0x02)
#	define GE_RS232_PTA_SUBCMD_ENTRY_EXIT_DELAY	(0x03)
#	define GE_RS232_PTA_SUBCMD_SIREN_SETUP	(0x04)
#	define GE_RS232_PTA_SUBCMD_SIREN_SYNC	(0x05)
#	define GE_RS232_PTA_SUBCMD_SIREN_GO		(0x06)
#	define GE_RS232_PTA_SUBCMD_TOUCHPAD_DISPLAY		(0x09)
#	define GE_RS232_PTA_SUBCMD_SIREN_STOP	(0x0B)
#	define GE_RS232_PTA_SUBCMD_FEATURE_STATE	(0x0C)
#	define GE_RS232_PTA_SUBCMD_TEMPERATURE	(0x0D)
#	define GE_RS232_PTA_SUBCMD_TIME_AND_DATE	(0x0E)
#define GE_RS232_PTA_SUBCMD2					(0x23)
#	define GE_RS232_PTA_SUBCMD2_LIGHTS_STATE	(0x01)
#	define GE_RS232_PTA_SUBCMD2_USER_LIGHTS	(0x02)
#	define GE_RS232_PTA_SUBCMD2_KEYFOB		(0x03)

#define GE_RS232_ATP_EQUIP_LIST_REQUEST		(0x02)
#define GE_RS232_ATP_DYNAMIC_DATA_REFRESH	(0x20)
#define GE_RS232_ATP_KEYPRESS				(0x40)

#define GE_RS232_ATP_RESERVED1				(0x60)
#define GE_RS232_ATP_RESERVED2				(0x99)
#define GE_RS232_ATP_RESERVED3				(0x98)

#define GE_RS232_ZONE_TYPE_HARDWIRED	(0)
#define GE_RS232_ZONE_TYPE_RF			(2)
#define GE_RS232_ZONE_TYPE_TOUCHPAD		(3)
#define GE_RS232_ZONE_TYPE_UNCONFIGURED	(255)

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

#pragma mark - Primary interface

typedef int ge_rs232_status_t;

struct ge_rs232_s {
	void* context;
	bool reading_message;
	uint8_t current_byte;
	uint8_t message_len;
	uint8_t nibble_buffer;
	uint8_t last_response;
	uint8_t buffer_sum;
	time_t last_sent;
	uint8_t buffer[GE_RS232_MAX_MESSAGE_SIZE];
	uint8_t output_buffer[GE_RS232_MAX_MESSAGE_SIZE];
	uint8_t output_buffer_len;
	uint8_t output_attempt_count;
	ge_rs232_status_t (*received_message)(void* context, const uint8_t* data, uint8_t len,struct ge_rs232_s* instance);
	ge_rs232_status_t (*send_byte)(void* context, uint8_t byte,struct ge_rs232_s* instance);
	void* response_context;
	void (*got_response)(void* context,struct ge_rs232_s* instance, bool didAck);
};

typedef struct ge_rs232_s* ge_rs232_t;

ge_rs232_t ge_rs232_init(ge_rs232_t interface);
ge_rs232_status_t ge_rs232_receive_byte(ge_rs232_t interface, uint8_t byte);
ge_rs232_status_t ge_rs232_ready_to_send(ge_rs232_t interface);
ge_rs232_status_t ge_rs232_send_message(ge_rs232_t interface, const uint8_t* data, uint8_t len);
ge_rs232_status_t ge_rs232_resend_last_message(ge_rs232_t self);

#pragma mark - Queue Interface

struct ge_message_s {
	uint8_t msg[GE_RS232_MAX_MESSAGE_SIZE];
	uint8_t msg_len;
	uint8_t attempts;
	void* context;
	void (*finished)(void* context,ge_rs232_status_t status);
};

struct ge_queue_s {
	ge_rs232_t interface;
	struct ge_message_s queue[GE_QUEUE_MAX_MESSAGES];
	uint8_t head, tail;
};
typedef struct ge_queue_s *ge_queue_t;

ge_queue_t ge_queue_init(ge_queue_t qinterface,ge_rs232_t interface);

ge_rs232_status_t ge_queue_update(ge_queue_t qinterface);

ge_rs232_status_t ge_queue_message(
	ge_queue_t qinterface,
	const uint8_t* data,
	uint8_t len,
	void (*finished)(void* context,ge_rs232_status_t status),
	void* context
);

#pragma mark - Text conversion

extern const char* ge_rs232_text_token_lookup[256];

const char* ge_text_to_ascii_one_line(const uint8_t * bytes, uint8_t len);
const char* ge_text_to_ascii(const uint8_t * bytes, uint8_t len);


#endif

