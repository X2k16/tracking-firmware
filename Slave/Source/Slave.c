/*
 * Slave.c
 *
 * ワイヤレスセンサーネットワークにおいて実際にセンサーと接続され、温度を測定する回路のソースコード
 *
 */

#include <string.h>				// C 標準ライブラリ用
#include <AppHardwareApi.h>		// NXP ペリフェラル API 用
#include "utils.h"				// ペリフェラル API のラッパなど
#include "serial.h"				// シリアル用
#include "sprintf.h"			// SPRINTF 用
#define vfPutc(s, c) SERIAL_bTxChar((s)->u8Device, c)

#define ToCoNet_USE_MOD_NBSCAN // Neighbour scan module
#undef ToCoNet_USE_MOD_NBSCAN_SLAVE

#include "ToCoNet.h"
#include "ToCoNet_mod_prototype.h" // ToCoNet モジュール定義

#include "type.h"
#include "../../Common/Source/app_event.h"
#include "../../Common/Source/packets.h"				// パケット

// ToCoNet 用パラメータ
#define APP_ID   0x22FF84B2
#define CHANNEL  18
#define CHANNEL_MASK        0x7FFF800 //ch11 to ch26()
#define CHANNEL_MASK_BASE	11
// スリープ時間(ms単位)
#define SLEEP_INTERVAL 0
// Masterの応答がなくなってから再接続を試みるまでの時間(秒単位)
#define RECONNECT_TIME	10

// ポート定義
#define PORT_LED_1 3
#define PORT_LED_2 2
#define PORT_LED_3 1
#define PORT_LED_4 0
#define PORT_SW_1 8
#define PORT_PWM_1 11
#define PORT_FELICA 5

#define UART_BAUD 115200 // シリアルのボーレート
#define UART_PORT E_AHI_UART_1

// デバッグメッセージ
#define DBG
#undef DBG
#ifdef DBG
#define dbg(...) vfPrintf(&sSerStream, LB __VA_ARGS__)
//#define dbg(...) {SPRINTF_vRewind(); vfPrintf(SPRINTF_Stream, LB __VA_ARGS__); sendSprintf();}
#else
#define dbg(...)
#undef WAIT_UART_OUTPUT
#define WAIT_UART_OUTPUT(...) // disabled

#endif

// プロトタイプ宣言
static bool_t sendSprintf();
static void sendHexDebug(uint8 *data, uint8 size);

// 変数
static tsFILE sSerStream;          // シリアル用ストリーム
static tsSerialPortSetup sSerPort; // シリアルポートデスクリプタ
static uint32 u32Seq;              // 送信パケットのシーケンス番号
static tsAppData sAppData;
uint32 u32BeforeSeq = 0xff;
tsFelicaResponse felicaResponse;

uint8 u8NfcInitStage = 0;
uint8 au8FelicaBuffer[128];
uint8 u8FelicaBufferIndex = 0;
uint8 au8BeforIdm[8] = {0};
uint8 u8ScanFailuer = 0;

#define SOUND_FREQ 32

#define SOUND_STARTUP 0
#define SOUND_SHUTDOWN 1
#define SOUND_CONNECT 2
#define SOUND_DISCONNECT 3
#define SOUND_ERROR 4
#define SOUND_SEARCH 5
#define SOUND_TOUCH 6
#define SOUND_SEND_ERROR 7

#define SOUND_LENGTH 15
uint16 u16SoundTimer = 0;
uint8 u8SoundSelect = 0;
const uint16 au16Sounds[8][SOUND_LENGTH] = {
	{523, 659, 783},
	{783, 659, 523},
	{523, 523, 523, 784, 784, 784},
	{784, 784, 784, 523, 523, 523},
	{523, 523, 523, 0, 523, 523, 523, 0, 523, 523, 523},
	{659, 0, 0, 659, 0 , 0},
	{784, 784, 1047, 1047},
	{466, 466, 466, 466, 466}
};

tsTimerContext sTimerPWM;

// デバッグ出力用に UART を初期化
static void vSerialInit() {
	static uint8 au8SerialTxBuffer[96];
	static uint8 au8SerialRxBuffer[32];

	sSerPort.pu8SerialRxQueueBuffer = au8SerialRxBuffer;
	sSerPort.pu8SerialTxQueueBuffer = au8SerialTxBuffer;
	sSerPort.u32BaudRate = UART_BAUD;
	sSerPort.u16AHI_UART_RTS_LOW = 0xffff;
	sSerPort.u16AHI_UART_RTS_HIGH = 0xffff;
	sSerPort.u16SerialRxQueueSize = sizeof(au8SerialRxBuffer);
	sSerPort.u16SerialTxQueueSize = sizeof(au8SerialTxBuffer);
	sSerPort.u8SerialPort = UART_PORT;
	sSerPort.u8RX_FIFO_LEVEL = E_AHI_UART_FIFO_LEVEL_1;
	SERIAL_vInit(&sSerPort);

	sSerStream.bPutChar = SERIAL_bTxChar;
	sSerStream.u8Device = UART_PORT;
}


static void vInitPort()
{
	// 使用ポートの設定
	vPortAsOutput(PORT_LED_1);
	vPortAsOutput(PORT_LED_2);
	vPortAsOutput(PORT_LED_3);
	vPortAsOutput(PORT_LED_4);
	vPortAsOutput(PORT_FELICA);


	vPortSetHi(PORT_LED_1);
	vPortSetHi(PORT_LED_2);
	vPortSetHi(PORT_LED_3);
	vPortSetHi(PORT_LED_4);

	vWait(0x3ffff);

	vPortSetLo(PORT_LED_1);
	vPortSetLo(PORT_LED_2);
	vPortSetLo(PORT_LED_3);
	vPortSetLo(PORT_LED_4);
	vPortSetLo(PORT_FELICA);

	vPortAsInput(PORT_SW_1);
}


static void vSetPWM(uint16 value)
{
	if(value == 0){
		// Off
		sTimerPWM.u16duty = 0;
	}else{
		// On
		sTimerPWM.u16duty = 128;
		sTimerPWM.u16Hz = value;
	}

	//vTimerConfig(&sTimerPWM);
	vTimerChangeHz(&sTimerPWM);
}

static void vPlaySound(uint8 sound){
	u8SoundSelect = sound;
	u16SoundTimer = 0;
	vSetPWM(au16Sounds[sound][0]);
}

static void vInitPWM()
{
	memset(&sTimerPWM, 0, sizeof(tsTimerContext));
	vPortAsOutput(PORT_PWM_1);

	// PWM
	uint16 u16PWM_Hz = 1500; // PWM周波数
  uint8 u8PWM_prescale = 0; // prescaleの設定

  if (u16PWM_Hz < 10) u8PWM_prescale = 9;
  else if (u16PWM_Hz < 100) u8PWM_prescale = 6;
  else if (u16PWM_Hz < 1000) u8PWM_prescale = 3;
  else if (u16PWM_Hz < 2000) u8PWM_prescale = 1;
  else u8PWM_prescale = 0;

	sTimerPWM.u16Hz = u16PWM_Hz;
	sTimerPWM.u8PreScale = u8PWM_prescale;
	sTimerPWM.u16duty = 0;
	sTimerPWM.bPWMout = TRUE;
  sTimerPWM.u8Device = E_AHI_DEVICE_TIMER1;
	vTimerConfig(&sTimerPWM);
	vTimerStart(&sTimerPWM);
}

// ハードウェア初期化
static void vInitHardware()
{
	// デバッグ出力用
	vSerialInit();
	ToCoNet_vDebugInit(&sSerStream);
	ToCoNet_vDebugLevel(0);
	vInitPWM();
	vInitPort();
}


static void writeSerial(uint8 *buf, uint16 size){
	uint16 i;
	for(i=0; i<size; i++){
		vPutChar(&sSerStream, buf[i]);
	}
	//sendHexDebug(buf, size);
}

static uint8 calcDCS(uint8 *data, uint16 len)
{
  uint8 sum = 0;
	uint16 i;

  for (i = 0; i < len; i++) {
      sum += data[i];
  }
  return (uint8)-(sum & 0xff);
}

static void sendFelicaReset()
{
	uint8 buf[6] = {0x00, 0x00, 0xff, 0x00, 0xff, 0x00};
	writeSerial(buf, 6);
	WAIT_UART_OUTPUT(UART_PORT);
}

static void sendFelicaCommand(uint8 *command, uint16 commandLen){
	uint8 buf[9];

	uint8 dcs = calcDCS(command, commandLen);

	/* transmit the command */
	buf[0] = 0x00;
	buf[1] = 0x00;
	buf[2] = 0xff;
	if (commandLen <= 255) {
			/* normal frame */
			buf[3] = commandLen;
			buf[4] = (uint8)-buf[3];
			writeSerial(buf, 5);
	} else {
			/* extended frame */
			buf[3] = 0xff;
			buf[4] = 0xff;
			buf[5] = (uint8)((commandLen >> 8) & 0xff);
			buf[6] = (uint8)((commandLen >> 0) & 0xff);
			buf[7] = (uint8)-(buf[5] + buf[6]);
			writeSerial(buf, 8);
	}
	writeSerial(command, commandLen);
	buf[0] = dcs;
	buf[1] = 0x00;
	writeSerial(buf, 2);
	WAIT_UART_OUTPUT(UART_PORT);
}



static bool_t sendDebugMessage(char *message){
	tsTxDataApp tsTx;
	uint8 size;

	if(sAppData.u32parentAddr == 0){
		return;
	}

	memset(&tsTx, 0, sizeof(tsTxDataApp));

	tsTx.u32SrcAddr = ToCoNet_u32GetSerial();
	tsTx.u32DstAddr = sAppData.u32parentAddr;

	tsTx.bAckReq = TRUE;
	tsTx.u8Retry = 0x01; // 送信失敗時は1回再送
	tsTx.u8CbId = u32Seq & 0xFF;
	tsTx.u8Seq = u32Seq & 0xFF;
	tsTx.u8Cmd = PACKET_CMD_DEBUG;

	size = strlen(message);
	if(size > 98){
		size = 98;
		message[97] = 0;
	}

	memcpy(tsTx.auData, message, size);
	tsTx.u8Len = size;
	dbg("u8Len: %d", tsTx.u8Len);
	u32Seq++;

	// 送信
	return ToCoNet_bMacTxReq(&tsTx);
}




// Masterへの送信実行
static bool_t sendIdm(uint8 *idm)
{
	tsTxDataApp tsTx;

	memset(&tsTx, 0, sizeof(tsTxDataApp));

	tsTx.u32SrcAddr = ToCoNet_u32GetSerial();
	tsTx.u32DstAddr = sAppData.u32parentAddr;
	//tsTx.u32DstAddr = TOCONET_MAC_ADDR_BROADCAST;

	tsTx.bAckReq = TRUE;
	tsTx.u8Retry = 0x03; // 送信失敗時は3回再送
	tsTx.u8CbId = u32Seq & 0xFF;
	tsTx.u8Seq = u32Seq & 0xFF;
	tsTx.u8Cmd = PACKET_CMD_FELICA;

	memcpy(tsTx.auData, idm, 8);
	tsTx.u8Len = 8;
	u32Seq++;

	// 送信
	vPortSetHi(PORT_LED_2);
	return ToCoNet_bMacTxReq(&tsTx);
}



// ユーザ定義のイベントハンドラ
static void vProcessEvCore(tsEvent *pEv, teEvent eEvent, uint32 u32evarg)
{

	if (eEvent == E_EVENT_TICK_SECOND) {
		sAppData.u32parentDisconnectTime++;

		if (sAppData.u32parentDisconnectTime > RECONNECT_TIME)
		{
			dbg("master disconnected.");
			vPlaySound(SOUND_DISCONNECT);
			sAppData.u32parentDisconnectTime = 0;
			sAppData.u32parentAddr = 0;
			ToCoNet_Event_SetState(pEv, E_STATE_CHSCAN_INIT);
		}
	}

	if (eEvent == E_EVENT_TICK_TIMER) {
		sAppData.u8tick_ms += 4;

		// サウンドの再生
		u16SoundTimer += 4;
		uint16 soundIndex = u16SoundTimer/SOUND_FREQ;
		if(soundIndex<SOUND_LENGTH){
			vSetPWM(au16Sounds[u8SoundSelect][soundIndex]);
		}else{
			u16SoundTimer = SOUND_FREQ * (SOUND_LENGTH+1);
		}
	}

	// ステート処理
	switch (pEv->eState) {
		// アイドル状態
		case E_STATE_IDLE:
			dbg("E_STATE_IDLE");

			if (eEvent == E_EVENT_START_UP){

				if (u32evarg & EVARG_START_UP_WAKEUP_RAMHOLD_MASK) {
				}
				sAppData.u32parentDisconnectTime = 0;
				vPlaySound(SOUND_STARTUP);
			}else if(ToCoNet_Event_u32TickFrNewState(pEv) > 1000) {
				// 空きチャンネルスキャンに入る
				u8ScanFailuer = 0;
				sAppData.u32parentDisconnectTime = 0;
				ToCoNet_Event_SetState(pEv, E_STATE_CHSCAN_INIT);
			}
			break;

		case E_STATE_CHSCAN_INIT:
			if (eEvent == E_EVENT_NEW_STATE) {
				dbg("E_EVENT_NEW_STATE");
				vPlaySound(SOUND_SEARCH);
				if(u8ScanFailuer > 5){
					ToCoNet_Event_SetState(pEv, E_STATE_APP_SHUTDOWN);
				}
				vPortSetLo(PORT_LED_3);
				vPortSetHi(PORT_FELICA);
			}

			//dbg("wait a small tick");
			if (ToCoNet_Event_u32TickFrNewState(pEv) > 200) {
				ToCoNet_vRfConfig();
				vPortSetHi(PORT_LED_4);
				dbg("master scan...");
				ToCoNet_NbScan_bStart(CHANNEL_MASK, 128);
				ToCoNet_Event_SetState(pEv, E_STATE_CHSCANNING);
			}
			break;
		case E_STATE_CHSCANNING:
			sAppData.u32parentDisconnectTime = 0;
			if (eEvent == E_EVENT_CHSCAN_FINISH)
			{
				dbg("CHSCAN finish. Ch%d selected.", sAppData.u8channel);
				vPortSetHi(PORT_LED_3);
				vPortSetLo(PORT_LED_4);
				vPlaySound(SOUND_CONNECT);
				//Ch変更
				sToCoNet_AppContext.u8Channel = sAppData.u8channel;
				ToCoNet_vRfConfig();

				sendDebugMessage("Hello!");

				ToCoNet_Event_SetState(pEv, E_STATE_NFC_INIT);
			}
			if (eEvent == E_EVENT_CHSCAN_FAIL)
			{
				dbg("CHSCAN failed.");
				vPortSetLo(PORT_LED_4);
				u8ScanFailuer++;
				ToCoNet_Event_SetState(pEv, E_STATE_CHSCAN_INIT);
			}

			//タイムアウト
			if (ToCoNet_Event_u32TickFrNewState(pEv) > 2500) {
				dbg("CHSCAN timeout.");
				vPortSetLo(PORT_LED_4);
				ToCoNet_Event_SetState(pEv, E_STATE_CHSCAN_INIT);
			}

			WAIT_UART_OUTPUT(UART_PORT);

			break;

		case E_STATE_NFC_RESET:

			if (eEvent == E_EVENT_NEW_STATE) {
				sendDebugMessage("NFC Reset");
				vPortSetLo(PORT_FELICA);
				vPlaySound(SOUND_ERROR);
			}else if(eEvent == E_EVENT_TICK_TIMER){
				if (ToCoNet_Event_u32TickFrNewState(pEv) > 4000){
					ToCoNet_Event_SetState(pEv, E_STATE_NFC_INIT);
				}else if (ToCoNet_Event_u32TickFrNewState(pEv) > 2500){
					sendFelicaReset();
					sendFelicaReset();
					sendFelicaReset();
				}else if (ToCoNet_Event_u32TickFrNewState(pEv) > 500){
					vPortSetHi(PORT_FELICA);
				}
			}
			break;


		case E_STATE_NFC_INIT:
			if (eEvent == E_EVENT_NEW_STATE) {
				sendDebugMessage("NFC Init");
				sAppData.u8tick_ms = 0;
				u8NfcInitStage = 1;
				sendFelicaCommand((uint8*)"\xd4\x18\x01", 3);
			}else if(eEvent == E_EVENT_NFC_RESPONSE){
				sAppData.u8tick_ms = 0;
				if(u8NfcInitStage == 1)
					sendFelicaCommand((uint8*)"\xd4\x32\x02\x00\x00\x00", 6);
				else if(u8NfcInitStage == 2)
					sendFelicaCommand((uint8*)"\xd4\x32\x05\x00\x00\x00", 6);
				else if(u8NfcInitStage == 3)
					sendFelicaCommand((uint8*)"\xd4\x32\x81\xb7", 4);
				else{
					sAppData.u8tick_ms = 0;
					ToCoNet_Event_SetState(pEv, E_STATE_POLLING);
				}
				u8NfcInitStage++;
			}
			if(eEvent == E_EVENT_TICK_TIMER && sAppData.u8tick_ms > 200){
				ToCoNet_Event_SetState(pEv, E_STATE_NFC_RESET);
			}

			break;

		case E_STATE_POLLING:
			if (eEvent == E_EVENT_NEW_STATE || eEvent == E_EVENT_NFC_RESPONSE) {
				sendFelicaCommand((uint8*)"\xd4\x4a\x01\x01\x00\xff\xff\x00\x00", 9);
			}

			if(eEvent == E_EVENT_NFC_RESPONSE){
				sAppData.u8tick_ms = 0;
				if(felicaResponse.length==22){
					vPortSetHi(PORT_LED_1);
					if(memcmp(au8BeforIdm, felicaResponse.data+6, 8) != 0){
						vPlaySound(SOUND_TOUCH);
						sendIdm(felicaResponse.data+6);
						memcpy(au8BeforIdm, felicaResponse.data+6, 8);
					}
				}else{
					vPortSetLo(PORT_LED_1);
					memset(au8BeforIdm, 0, 8);
				}
			}else if(eEvent == E_EVENT_TICK_TIMER && sAppData.u8tick_ms > 200){
				ToCoNet_Event_SetState(pEv, E_STATE_NFC_RESET);
			}

			break;

		case E_STATE_APP_SHUTDOWN:
			if (eEvent == E_EVENT_NEW_STATE){
				vPlaySound(SOUND_SHUTDOWN);
			}else if(ToCoNet_Event_u32TickFrNewState(pEv) > 200) {
				ToCoNet_Event_SetState(pEv, E_STATE_APP_SLEEP);
			}
			break;

		case E_STATE_APP_SLEEP:
			dbg("E_STATE_APP_SLEEP");
			if (eEvent == E_EVENT_NEW_STATE) {
				dbg("Sleeping...\r\n");
				vPortSetLo(PORT_LED_1);
				vPortSetLo(PORT_LED_2);
				vPortSetLo(PORT_LED_3);
				vPortSetLo(PORT_LED_4);
				vPortSetLo(PORT_FELICA);
				sAppData.u32parentAddr = 0;
				WAIT_UART_OUTPUT(UART_PORT);
				vAHI_DioWakeEnable(1<<PORT_SW_1, 0);
				ToCoNet_vSleep(E_AHI_WAKE_TIMER_0, SLEEP_INTERVAL, FALSE, TRUE);
			}
			break;

		default:
			break;
	}

}

static void sendHexDebug(uint8 *data, uint8 size){
	uint8 debug[40] = {0};
	uint8 i;
	uint8 c;
	for(i=0; i<size; i++){
		c = data[i];
		debug[2*i] = ((c>>4)>9 ? ('A'-10) : '0')  + (c>>4);
		debug[2*i+1] = ((c&0x0f)>9 ? ('A'-10)  : '0') + (c&0x0f);
	}
	sendDebugMessage(debug);

}



void vHandleSerialInput(){
	uint8 i;
	if (!SERIAL_bRxQueueEmpty(sSerPort.u8SerialPort)) {
		// FIFOキューから１バイトずつ取り出して処理する。

		int16 i16Char;
		uint8 u8Char;
		i16Char = SERIAL_i16RxChar(sSerPort.u8SerialPort);
		u8Char = (uint8)i16Char;

		au8FelicaBuffer[u8FelicaBufferIndex] = u8Char;

		if(
				(u8FelicaBufferIndex == 0 && u8Char != 0x00) ||
				(u8FelicaBufferIndex == 1 && u8Char != 0x00) ||
				(u8FelicaBufferIndex == 2 && u8Char != 0xff)
			){
			u8FelicaBufferIndex = 0;
		}else{
			u8FelicaBufferIndex++;
		}

		if(u8FelicaBufferIndex == 6){

			if(au8FelicaBuffer[3] == 0x00 && au8FelicaBuffer[4] == 0xff && au8FelicaBuffer[5] == 0x00){
				//ACK
				u8FelicaBufferIndex = 0;
				//sendDebugMessage("ACK");
				ToCoNet_Event_Process(E_EVENT_NFC_ACK, 0, vProcessEvCore);
			}else if(((au8FelicaBuffer[3] + au8FelicaBuffer[4]) & 0xff) != 0){
				// エラー
				u8FelicaBufferIndex = 0;
				//sendDebugMessage("LE");
			}else{
				felicaResponse.length = au8FelicaBuffer[3];
			}
		}


		if(u8FelicaBufferIndex == (5 + felicaResponse.length + 2)){
			//sendHexDebug(au8FelicaBuffer,u8FelicaBufferIndex);

			for(i=0; i<felicaResponse.length; i++){
				felicaResponse.data[i] = au8FelicaBuffer[5+i];
			}
			uint8 dcs = calcDCS(felicaResponse.data, felicaResponse.length);
			if(dcs == au8FelicaBuffer[5+felicaResponse.length] && au8FelicaBuffer[5+felicaResponse.length+1] == 0x00){
				// DCS/フッタ一致
				//sendHexDebug(felicaResponse.data, felicaResponse.length);
				ToCoNet_Event_Process(E_EVENT_NFC_RESPONSE, 0, vProcessEvCore);
			}else{
				//sendDebugMessage("DCS M");
			}
			u8FelicaBufferIndex = 0;
		}



  }
}


//
// 以下 ToCoNet 既定のイベントハンドラ群
//

// 割り込み発生後に随時呼び出される
void cbToCoNet_vMain(void)
{
	vHandleSerialInput();
}

// パケット受信時
void cbToCoNet_vRxEvent(tsRxDataApp *pRx)
{
#ifdef DBG
	uint8 *p = pRx->auData;
	dbg("\n\r[PKT Ad:%04x,Cmd:%02x,Ln:%03d,Seq:%03d,Lq:%03d,Tms:%05d ",
		pRx->u32SrcAddr,
		pRx->u8Cmd,
		pRx->u8Len,// Actual payload byte is +4: the network layer uses additional 4 bytes.
		pRx->u8Seq,
		pRx->u8Lqi,
		pRx->u32Tick & 0xFFFF);
	// ToDo: Ping応答
#endif

	if (u32BeforeSeq != pRx->u8Seq)
	{
		if (pRx->u8Cmd == PACKET_CMD_KEEP_ALIVE)
		{
			sAppData.u32parentDisconnectTime = 0;
			dbg("Keep-Alive was received.");
		}

		u32BeforeSeq = pRx->u8Seq;
	}


	return;
}

// パケット送信完了時
void cbToCoNet_vTxEvent(uint8 u8CbId, uint8 bStatus) {
	dbg("\n\r[TX CbID:%02x Status:%s]", u8CbId, bStatus ? "OK" : "Err");
	if (bStatus)
	{

		vPortSetLo(PORT_LED_2);
		vPortSetLo(PORT_LED_4);
		ToCoNet_Event_Process(E_EVENT_TRANSMIT_FINISH, 0, vProcessEvCore);
	}
	else
	{
		vPlaySound(SOUND_SEND_ERROR);
		vPortSetHi(PORT_LED_4);
		ToCoNet_Event_Process(E_EVENT_TRANSMIT_FAIL, 0, vProcessEvCore);
	}
	return;
}

// ネットワークイベント発生時
void cbToCoNet_vNwkEvent(teEvent eEvent, uint32 u32arg) {
	switch (eEvent) {

		//case E_EVENT_TOCONET_NWK_START:
		//	break;
		case E_EVENT_TOCONET_NWK_SCAN_COMPLETE:
			_C{
				tsToCoNet_NbScan_Entitiy *nbNode = NULL;
				tsToCoNet_NbScan_Result *pNbsc = (tsToCoNet_NbScan_Result *)u32arg;
				dbg("%d", u32arg);
				uint8 i, u8lqi=0;

				if (pNbsc->u8scanMode & TOCONET_NBSCAN_NORMAL_MASK) {
					dbg("Mode: Normal Scan");
					dbg("nodes: %d", pNbsc->u8found);
					// 全チャネルスキャン結果
					for (i = 0; i < pNbsc->u8found && i < 10; i++) {
						tsToCoNet_NbScan_Entitiy *pEnt = &pNbsc->sScanResult[pNbsc->u8IdxLqiSort[i]];
						if (pEnt->bFound) {
							dbg("%d Ch:%d Addr:%08x LQI:%d", i, pEnt->u8ch, pEnt->u32addr, pEnt->u8lqi);
							if(u8lqi < pEnt->u8lqi){
								nbNode = pEnt;
							}
						}
						WAIT_UART_OUTPUT(UART_PORT);
					}

					//検索結果あり
					if (nbNode != NULL) {
						sAppData.u8channel = nbNode->u8ch;
						sAppData.u32parentAddr = nbNode->u32addr;
						ToCoNet_Event_Process(E_EVENT_CHSCAN_FINISH, 0, vProcessEvCore);
					}
					else {
						ToCoNet_Event_Process(E_EVENT_CHSCAN_FAIL, 0, vProcessEvCore);

					}

				}/* else if (pNbsc->u8scanMode & TOCONET_NBSCAN_QUICK_EXTADDR_MASK) {
				 vfPrintf(&sSerStream, "\n\rTOCONET_NBSCAN_QUICK_EXTADDR_MASK");
				 }*/

			}
			break;
		default:
			break;
	}
}

// ハードウェア割り込み発生後（遅延呼び出し）
void cbToCoNet_vHwEvent(uint32 u32DeviceId, uint32 u32ItemBitmap)
{
	return;
}

// ハードウェア割り込み発生時
uint8 cbToCoNet_u8HwInt(uint32 u32DeviceId, uint32 u32ItemBitmap)
{
	return FALSE;
}

// コールドスタート時
void cbAppColdStart(bool_t bAfterAhiInit)
{
	// TWE-EH-S 制御
	vInitPort();

	if (!bAfterAhiInit) {
		// before AHI init, very first of code.

		// 必要モジュール登録手続き
		ToCoNet_REG_MOD_ALL();
	} else {
		// disable brown out detect
		vAHI_BrownOutConfigure(0, //0:2.0V 1:2.3V
			FALSE, FALSE, FALSE, FALSE);
		// SPRINTF 初期化

		SPRINTF_vInit128();

		// clear application context
		memset(&sAppData, 0x00, sizeof(sAppData));
		sAppData.u8channel = 15;
		sAppData.u8retry = 1;
		sAppData.u32parentAddr = 0x0;


		// ユーザ定義のイベントハンドラを登録
		ToCoNet_Event_Register_State_Machine(vProcessEvCore);

		// ハードウェア初期化
		vInitHardware();

		// ToCoNet configuration
		sToCoNet_AppContext.u32AppId = APP_ID;
		sToCoNet_AppContext.u8Channel = sAppData.u8channel;
		sToCoNet_AppContext.u8TxMacRetry = sAppData.u8retry;
		sToCoNet_AppContext.u8CPUClk = 0; // 4MHz
		sToCoNet_AppContext.bRxOnIdle = TRUE;

		dbg("slave init.");
		dbg("APP_ID=%08X Ch=%d\r\n", sToCoNet_AppContext.u32AppId, sToCoNet_AppContext.u8Channel);
		dbg("addr: %08X", ToCoNet_u32GetSerial());

		/*
		// ToCoNet パラメータ
		sToCoNet_AppContext.u32AppId = APP_ID;
		sToCoNet_AppContext.u8Channel = CHANNEL;
		sToCoNet_AppContext.u16TickHz = 1000; // 1[ms]ごとにTick
		sToCoNet_AppContext.u32ChMask = CHANNEL_MASK;
		sToCoNet_AppContext.bRxOnIdle = TRUE; // アイドル時にも受信
		u32Seq = 0;
		*/
		// MAC 層開始
		ToCoNet_vMacStart();

	}
}

// ウォームスタート時
void cbAppWarmStart(bool_t bAfterAhiInit)
{

	if (!bAfterAhiInit) {
	}
	else {
		vInitHardware();
		ToCoNet_vMacStart();
	}
	return;
}
