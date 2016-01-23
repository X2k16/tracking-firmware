/*
 * Master.c
 *
 * ワイヤレスセンサーモジュールからの情報を一カ所で受信し、UARTに流すコード
 *
 */

#include <string.h>				// C 標準ライブラリ用
#include <AppHardwareApi.h>		// NXP ペリフェラル API 用
#include "utils.h"				// ペリフェラル API のラッパなど
#include "serial.h"				// シリアル用
#include "sprintf.h"			// SPRINTF 用

#define ToCoNet_USE_MOD_ENERGYSCAN
#undef ToCoNet_USE_MOD_NBSCAN // Neighbour scan module
#define ToCoNet_USE_MOD_NBSCAN_SLAVE

#include "ToCoNet.h"
#include "ToCoNet_mod_prototype.h" // ToCoNet モジュール定義

#include "../../Common/Source/packets.h"				// パケット
#include "../../Common/Source/app_event.h"

// ToCoNet 用パラメータ
#define APP_ID   0x22FF84B2
#define CHANNEL  18
#define CHANNEL_MASK        0x7FFF800 //ch11~26
#define CHANNEL_MASK_BASE	11
#define MASTER_ADDR	0x0000
//0x1FF800 //ch11 to ch20

// ポート定義
#define DO1		18
#define DO2		19
#define DO3		4
#define DO4		9 // デジタル出力 4
#define UART_BAUD 115200 // シリアルのボーレート

// デバッグメッセージ
#undef DBG
//#define DBG
#ifdef DBG
#define dbg(...) vfPrintf(&sSerStream, LB __VA_ARGS__)
#else
#define dbg(...)
#endif

#define echo(...) vfPrintf(&sSerStream, LB __VA_ARGS__)


typedef struct {
	// application state
	teState eState;

	// Random count generation
	uint32 u32randcount; // not used in this application

	// OSC calibration
	uint32 u32RcOscError;

	// frame sequence number
	uint32 u32FrameCount;

	// CCA fail count
	uint16 u16CCAfail;

	// PER
	uint16 u16PerCount;
	uint16 u16PerCountMax;
	uint16 u16PerSuccess;
	uint16 u16PerSuccessAppAck;
	uint8 bPerFinish;
	uint8 bPerAppAckMode;
	uint8 u8PerLqiLast;
	uint32 u32PerLqiSum;

	// PER MAC SETTING
	uint8 u8retry;
	uint8 u8channel;
	uint8 u8Power;

	uint8 u8payload;

	// TICK COUNT
	uint8 u8tick_ms;

	// Tx Finish Flag
	uint8 bTxBusy;

	// 送信中のシーケンス番号
	uint8 u8TxSeq;

	// Child nodes
	uint8 u8ChildFound;
	uint8 u8ChildSelect;
	tsToCoNet_NbScan_Entitiy asChilds[20];
	uint32 u32ChildAddr;
	uint8 u8ChildConfCh;

	// Energy Scan
	uint8 u8ChEnergy;
	uint8 u8ChEnergyMax;

	// Disp Lang
	uint8 u8Lang;
} tsAppData;



// 変数
static tsFILE sSerStream;          // シリアル用ストリーム
static tsSerialPortSetup sSerPort; // シリアルポートデスクリプタ
static uint32 u32Seq;              // 送信パケットのシーケンス番号
static uint8 u8channel;
static tsAppData sAppData;
static uint32 u32BeforeSeq = -1;
static uint32 sec = 0;


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
	sSerPort.u8SerialPort = E_AHI_UART_0;
	sSerPort.u8RX_FIFO_LEVEL = E_AHI_UART_FIFO_LEVEL_1;
	SERIAL_vInit(&sSerPort);

	sSerStream.bPutChar = SERIAL_bTxChar;
	sSerStream.u8Device = E_AHI_UART_0;
}

// ハードウェア初期化
static void vInitHardware()
{
	// デバッグ出力用
	vSerialInit();
	ToCoNet_vDebugInit(&sSerStream);
	ToCoNet_vDebugLevel(0);


	// 使用ポートの設定
	vPortAsOutput(DO4);
	vPortSetLo(DO4);
}

// ユーザ定義のイベントハンドラ
static void vProcessEvCore(tsEvent *pEv, teEvent eEvent, uint32 u32evarg)
{
	//	static int i = 0;
	if (eEvent == E_EVENT_TICK_SECOND) {
		// DO4 の Lo / Hi をトグル
		sec++;
		if (sec > 2){
			vPortSetLo(DO4);
			sec = 0;
		}
		
	}

	switch (pEv->eState)
	{
		case E_STATE_IDLE:
			if (eEvent == E_EVENT_START_UP)
			{
				dbg("** Master init**");
				echo("{ \"status\": \"initialize\" }");
				vPortSetLo(DO4);
				//sendBroadcast();
				// 起動直後
				ToCoNet_Event_SetState(pEv, E_STATE_CHSCAN_INIT);
			}


			if (eEvent == E_EVENT_TICK_SECOND) {
				break;
			}

			break;

		case E_STATE_CHSCAN_INIT:
			if (eEvent == E_EVENT_NEW_STATE) {
				dbg("\n\rFree Channel Scanning...");
			}

			// wait a small tick
			if (ToCoNet_Event_u32TickFrNewState(pEv) > 200) { // wait to finish Energy Scan (will take around 64ms)
				ToCoNet_EnergyScan_bStart(CHANNEL_MASK, 2);
				ToCoNet_Event_SetState(pEv, E_STATE_CHSCANNING);
			}
			break;

		case E_STATE_CHSCANNING:

			if (eEvent == E_EVENT_CHSCAN_FINISH){
				//エナジースキャンの完了
				dbg("\n\rCh%d is selected.", sAppData.u8channel);
				echo("{ \"status\": \"channel selected\", \"channel\": %d }", sAppData.u8channel);

				WAIT_UART_OUTPUT(E_AHI_UART_0);
				//Ch変更
				sToCoNet_AppContext.u8Channel = sAppData.u8channel;
				ToCoNet_vRfConfig();
				ToCoNet_Event_SetState(pEv, E_STATE_IDLE);
			}

			if (ToCoNet_Event_u32TickFrNewState(pEv) > 2000) {
				dbg("timeout.", sAppData.u8channel);
				echo("{ \"status\": \"timeout\"");
				ToCoNet_Event_SetState(pEv, E_STATE_CHSCAN_INIT);
			}

			break;

	}
}

//
// 以下 ToCoNet 既定のイベントハンドラ群
//

// 割り込み発生後に随時呼び出される
void cbToCoNet_vMain(void)
{

	return;
}

// パケット受信時
void cbToCoNet_vRxEvent(tsRxDataApp *pRx) {
	dbg("packet incoming");
	if (u32BeforeSeq != pRx->u8Seq)
	{
		if (pRx->u8Cmd == Felica)
		{
			char buf[64];
			tsFelicaData data;

			memcpy((void *)&data, pRx->auData, pRx->u8Len);

			echo("{ \"macaddress\": \"%08X\", \"idm\": %d }", pRx->u32SrcAddr, data.IDm);
			WAIT_UART_OUTPUT(E_AHI_UART_0);
			u32BeforeSeq = pRx->u8Seq;
			vPortSetHi(DO4);
		}
	}

}

// パケット送信完了時
void cbToCoNet_vTxEvent(uint8 u8CbId, uint8 bStatus)
{
	dbg(">> SEND %s seq=%u", bStatus ? "OK" : "NG", u32Seq);
	//E_ORDER_KICK イベントを通知
	ToCoNet_Event_Process(E_ORDER_KICK, 0, vProcessEvCore);
	return;
}

// ネットワークイベント発生時
void cbToCoNet_vNwkEvent(teEvent eEvent, uint32 u32arg)
{

	switch (eEvent) {
		case E_EVENT_TOCONET_NWK_START:
			break;
		case E_EVENT_TOCONET_ENERGY_SCAN_COMPLETE:
			_C{
				uint8 *pu8Result = (uint8*)u32arg;
				uint8 u8ChCount = pu8Result[0];

				int i, min = 255;
				for (i = 0; i < u8ChCount; i++) {
					dbg("%d: %3d",
							i + CHANNEL_MASK_BASE,
							pu8Result[i + 1]
							);
					//ノイズの一番少ないチャンネルを選択する
					if (min > pu8Result[i + 1]){
						sAppData.u8channel = i + CHANNEL_MASK_BASE;
						min = pu8Result[i + 1];
					}
					WAIT_UART_OUTPUT(E_AHI_UART_0);
				}
				ToCoNet_Event_Process(E_EVENT_CHSCAN_FINISH, 0, vProcessEvCore);

			}
			break;
	}
	return;
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
	if (!bAfterAhiInit) {

		// 必要モジュール登録手続き
		ToCoNet_REG_MOD_ALL();
	}
	else {
		// SPRINTF 初期化
		SPRINTF_vInit128();


		// ハードウェア初期化
		vInitHardware();

		// ToCoNet パラメータ
		sToCoNet_AppContext.u32AppId = APP_ID;
		sToCoNet_AppContext.u8Channel = CHANNEL;
		sToCoNet_AppContext.bRxOnIdle = TRUE; // アイドル時にも受信
		sToCoNet_AppContext.u16ShortAddress = MASTER_ADDR;
		sToCoNet_AppContext.u8TxMacRetry = 3;
		u32Seq = 0;

		dbg("Master Init complete. MAC start.\r\n");
		dbg("APP_ID=%08X Ch=%d\r\n", sToCoNet_AppContext.u32AppId, sToCoNet_AppContext.u8Channel);
		dbg("addr: %08X", ToCoNet_u32GetSerial());

		// MAC 層開始
		ToCoNet_vMacStart();

		// ユーザ定義のイベントハンドラを登録
		ToCoNet_Event_Register_State_Machine(vProcessEvCore);



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
