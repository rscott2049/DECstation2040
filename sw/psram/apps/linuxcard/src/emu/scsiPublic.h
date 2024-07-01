/*
	(c) 2021 Dmitry Grinberg   https://dmitry.gr
	Non-commercial use only OR licensing@dmitry.gr
*/

#ifndef _SCSI_PUBLIC_H_
#define _SCSI_PUBLIC_H_



//COMMANDS
	//first byte is command (top 3 bits are gourp, bottom 5 is command), top 3 bits of second are LUN. last byte is control byte. 
	//	control byte: 0x01 - LINK - this is a linked command. targets not supportinglinked commands shall set check condition ilegal request
	//  control byte: 0x02 - FLAG - only valid if link bit is set. see page 48 os SCSI-I spec
	//group0 are commands that are 6 bytes long
	//group1 are commands that are 10 bytes long
	//groups2,3,4 are RFU
	//group5 are commands that are 12 bytes long, commands 0x00 and 0x0e are private use, 0x0f and 0x1f are RFU
	//goups6,7 are private use

	//commands for ALL devices
	#define SCSI_CMD_TEST_UNIT_READY				0x00	//not mandatory
	#define SCSI_CMD_REQUEST_SENSE					0x03	//always mandatory
	#define SCSI_CMD_SEND_DSIAGNOSTIC				0x10	//not mandatory
	#define SCSI_CMD_INQUIRY						0x12	//mandatory in extended level. cmd[4] is number of allocated bytes, see p. 60
	#define SCSI_CMD_COPY							0x18	//not mandatory
	#define SCSI_CMD_RECV_DIAGNOSTIC_RESULTS		0x1c	//not mandatoty
	#define SCSI_CMD_COMPARE						0x39	//not mandatory
	#define SCSI_CMD_COPY_AND_VERIFY				0x3a	//not mandatory
	
	//commands for direct access devices (p. 72+)
	#define SCSI_CMD_REZERO_UNIT					0x01	//not mandatory
	#define SCSI_CMD_FORMAT_UNIT					0x04	//always mandatory
	#define SCSI_CMD_REASSIGN_BLOCKS				0x07	//not mandatory
	#define SCSI_CMD_READ							0x08	//always mandatory
	#define SCSI_CMD_WRITE							0x0A	//always mandatory
	#define SCSI_CMD_SEEK							0x0B	//not mandatory
	#define SCSI_CMD_MODE_SELECT					0x15	//not mandatory
	#define SCSI_CMD_RESERVE						0x16	//not mandatory
	#define SCSI_CMD_RELEASE						0x17	//not mandatory
	#define SCSI_CMD_MODE_SENSE						0x1A	//not mandatory
	#define SCSI_CMD_START_STOP_UNIT				0x1B	//not mandatory
	#define SCSI_CMD_PREVENT_ALLOW_MEDIUM_REMOVAL	0x1E	//not mandatory
	#define SCSI_CMD_READ_CAPACITY					0x25	//mandatory in extended level
	#define SCSI_CMD_READ_EXTENDED					0x28	//mandatory in extended level
	#define SCSI_CMD_WRITE_EXTENDED					0x2A	//mandatory in extended level
	#define SCSI_CMD_SEEK_EXTENDED					0x2B	//not mandatory
	#define SCSI_CMD_WRITE_AND_VERIFY				0x2E	//not mandatory
	#define SCSI_CMD_VERIFY							0x2F	//not mandatory
	#define SCSI_CMD_SEARCH_DATA_HIGH				0x30	//not mandatory
	#define SCSI_CMD_SEARCH_DATA_EQUAL				0x31	//not mandatory
	#define SCSI_CMD_SEARCH_DATA_LOW				0x32	//not mandatory
	
	//commands for sequential access devices (p. 116+)
	#define SCSI_CMD_REWIND							0x01	//always mandatory
	#define SCSI_CMD_READ_BLOCK_LIMITS				0x05	//mandatory in extended level
	//SCSI_CMD_READ
	//SCSI_CMD_WRITE
	#define SCSI_CMD_TRACK_SELECT					0x0B	//not mandatory
	#define SCSI_CMD_READ_REVERSE					0x0F	//not mandatory
	#define SCSI_CMD_WRITE_FILE_MARKS				0x10	//always mandatory
	#define SCSI_CMD_SPACE							0x11	//not mandatory
	#define SCSI_CMD_SEQ_VERIFY						0x13	//not mandatory, called "VERIFY"
	#define SCSI_CMD_RECOVER_BUFFERED_DATA			0x14	//not mandatory
	//SCSI_CMD_MODE_SELECT
	//SCSI_CMD_RESERVE
	//SCSI_CMD_RELEASE
	#define SCSI_CMD_ERASE							0x19	//not mandatory
	//SCSI_CMD_MODE_SENSE
	#define SCSI_CMD_LOAD_UNLOAD					0x1B	//not mandatory
	//SCSI_CMD_PREVENT_ALLOW_MEDIUM_REMOVAL
	
//STATUSSES
	#define SCSI_STATUS_GOOD						0x00
	#define SCSI_STATUS_CHECK_CONDITION				0x02
	#define SCSI_STATUS_CONDITION_MET				0x04
	#define SCSI_STATUS_BUSY						0x08
	#define SCSI_STATUS_INTERMEDIATE				0x10
	#define SCSI_STATUS_INTERMEDIATE_CONDITION_MET	0x14
	#define SCSI_STATUS_RESERVATION_CONFLICT		0x18
	#define SCSI_STATUS_COMMAND_TERMINATED			0x22
	#define SCSI_STATUS_QUEUE_FULL					0x28
	

//MESSAGES
	
	//extended message { u8 0x01, u8 len (0 = 256), u8[]}	//first byte is cmd itself
	
	//1-byte messages from target
	#define SCSI_MSG_CMD_COMPLETED					0x00	//mandatory always (command completed and status has been sent back to initiator)
	#define SCSI_MSG_SAVE_DATA_POINTER				0x02	//not mandatory
	#define SCSI_MSG_RESTORE_POINTERS				0x03	//not mandatory
	#define SCSI_MSG_DISCONNECT						0x04	//mandatory in extended level
	#define SCSI_MSG_LINKED_CMD_COMPLETED			0x0A	//not mandatory
	#define SCSI_MSG_LINKED_CMD_COMPLETED_W_FLAG	0x0B	//not mandatory
	
	//1-byte messages from initiator
	#define SCSI_MSG_INITIATOD_DETECTED_ERR			0x05	//not mandatory
	#define SCSI_MSG_ABORT							0x06	//not mandatory
	#define SCSI_MSG_NOP							0x08	//not mandatory
	#define SCSI_MSG_MESSAGE_PAR_ERROR				0x09	//not mandatory
	#define SCSI_MSG_RESET_BUS_DEV					0x0c	//not mandatory
	
	//1-byte messages from either
	#define SCSI_MSG_MESSAGE_REJECTED				0x07	//mandatory always
	#define SCSI_MSG_MESSAGE_PAR_ERROR				0x09	//not mandatory
	
	//supportsReconnect only valid from initiator
	#define SCSI_MSG_IDENTIFY(supportsReconnect, targetLun)	(0x80 + ((supportsReconnect) ? 0x40 : 0x00) + ((targetLun) & 7))
	#define SCSI_MSG_IS_IDENTIFY(val)				(!!((val) & 0x80))
	#define SCSI_IDENT_SUPPORTS_RECONNECT(val)		(!!((val) & 0x40))
	#define SCSI_IDENT_LUN(val)						((val) & 0x07)
	
	
	//MSG_IDENTIFY is 0x80..0x87 and 0cc0..0xc7
	
	//multibyte messages from either
	#define SCSI_MSG_MULTIBYTE_START				0x01
	#define SCSI_MSG_MODIFY_DATA_POINTER			0x00	//not mandatory, 7 bytes total (0x01, 0x05, 0x00, ?, ?, ?, ?), param is BE s32 to add to data pointer
	#define SCSI_MSG_EXTENDED_IDENTIFY				0x02	//not mandatory, 4 bytes total (0x01, 0x02, 0x02, ?), byte is sub-LUN
	#define SCSI_MSG_OFFSET_INTRLCK_DATA_XFER_REQ	0x01	//not mandatory, 5 bytes total (0x01, 0x03, 0x01, ?, ?)


//SENSE KEYS (p166 in SCSI-II spec)  https://www.ibm.com/docs/en/ts3500-tape-library?topic=information-sense-key-illegal-request
	#define SCSI_SENSE_KEY_NONE						0x00
	#define SCSI_SENSE_KEY_RECOVERED_ERROR			0x01
	#define SCSI_SENSE_KEY_NOT_READY				0x02
	#define SCSI_SENSE_KEY_MEDIUM_ERROR				0x03
	#define SCSI_SENSE_KEY_HARDWARE_ERROR			0x04
	#define SCSI_SENSE_KEY_ILLEGAL_REQUEST			0x05
	#define SCSI_SENSE_KEY_UNIT_ATTENTION			0x06
	#define SCSI_SENSE_KEY_DATA_PROTECT				0x07
	#define SCSI_SENSE_KEY_BLANK_CHECK				0x08
	#define SCSI_SENSE_KEY_COPY_ABORTED				0x09
	#define SCSI_SENSE_KEY_ABORTED_COMMAND			0x0b
	#define SCSI_SENSE_KEY_EQUAL					0x0c
	#define SCSI_SENSE_KEY_VOLUME_OVERFLOW			0x0d
	#define SCSI_SENSE_KEY_MISCOMPARE				0x0e
	
//ASC and ASCQ values (hi and lo respectively)
	#define SCSI_ASC_Q_INVALID_FIELD_IN_CDB			0x2400
	#define SCSI_ASC_Q_LUN_NOT_SUPPORTED			0x2500
	#define SCSI_ASC_Q_INCOMPAT_MEDIUM_INSTALLED	0x3000
	#define SCSI_ASC_Q_INTERNAL_TARGET_ERROR		0x4400


#endif

