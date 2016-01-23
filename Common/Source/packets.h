#ifndef PACKETS_H_
#define PACKETS_H_

#include "ToCoNet_packets.h"

typedef enum
{
	PACKET_CMD_DEBUG = TOCONET_PACKET_CMD_APP_USER,
	PACKET_CMD_KEEP_ALIVE,
	PACKET_CMD_FELICA
} tePacketCmdApp;

typedef struct
{
	char IDm[8];
} tsFelicaData;

#endif /* PACKETS_H_ */
