

#include <stdint.h>
#include <stdbool.h>

#define GE_RS232_START_OF_MESSAGE	(0x0A)
#define GE_RS232_ACK				(0x06)
#define GE_RS232_NAK				(0x15)
#define GE_RS232_MAX_MESSAGE_SIZE	(56)

typedef int ge_rs232_status_t;

struct ge_rs232_s {
	void* context;
	bool reading_message;
	uint8_t current_byte;
	uint8_t message_len;
	uint8_t nibble_buffer;
	uint8_t last_response;
	uint8_t buffer_sum;
	uint8_t buffer[GE_RS232_MAX_MESSAGE_SIZE];
	ge_rs232_status_t (*received_message)(void* context, const uint8_t* data, uint8_t len,struct ge_rs232_s* instance);
	ge_rs232_status_t (*send_byte)(void* context, uint8_t byte,struct ge_rs232_s* instance);
};

typedef struct ge_rs232_s* ge_rs232_t;


#define GE_RS232_STATUS_OK					(0)
#define GE_RS232_STATUS_ERROR				(-1)
#define GE_RS232_STATUS_WAIT				(-2)
#define GE_RS232_STATUS_NAK					(-3)
#define GE_RS232_STATUS_TIMEOUT				(-4)
#define GE_RS232_STATUS_MESSAGE_TOO_BIG		(-5)
#define GE_RS232_STATUS_JUNK				(-6)
#define GE_RS232_STATUS_BAD_CHECKSUM		(-7)

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

extern const char* ge_rs232_text_token_lookup[256];

ge_rs232_t ge_rs232_init(ge_rs232_t interface);
ge_rs232_status_t ge_rs232_receive_byte(ge_rs232_t interface, uint8_t byte);
ge_rs232_status_t ge_rs232_ready_to_send(ge_rs232_t interface);
ge_rs232_status_t ge_rs232_send_message(ge_rs232_t interface, const uint8_t* data, uint8_t len);

