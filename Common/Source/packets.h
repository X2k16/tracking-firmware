#ifndef PACKETS_H_
#define PACKETS_H_

#include "ToCoNet_packets.h"

typedef enum
{
	PACKET_CMD_QUESTION = TOCONET_PACKET_CMD_APP_USER,
	PACKET_CMD_POWER,
	PACKET_CMD_ANSWER,
	PACKET_CMD_REQUEST,
	PACKET_CMD_RESPONSE,
	PACKET_CMD_STATUS
} tePacketCmdApp;

typedef enum
{
	PACKET_CBID_QUESTION = 0x80,
	PACKET_CBID_POWER,
	PACKET_CBID_ANSWER,
	PACKET_CBID_REQUEST,
	PACKET_CBID_RESPONSE,
	PACKET_CBID_STATUS
} tePacketCbid;

typedef struct
{
	char IDm[8];
} tsFelicaData;

enum PacketType
{
	Data, Cmd, Felica
};

#endif /* PACKETS_H_ */
