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
#define SLEEP_INTERVAL 5000
// Masterの応答がなくなってから再接続を試みるまでの時間(秒単位)
#define RECONNECT_TIME	35

// ポート定義
#define PORT_LED_1 3
#define PORT_LED_2 2
#define PORT_LED_3 1
#define PORT_LED_4 0
#define PORT_SW_1 8
#define PORT_SW_2 9
#define PORT_SW_3 10
#define PORT_FELICA 5


// デバッグメッセージ
#define DBG
#ifdef DBG
#define UART_BAUD 115200 // シリアルのボーレート
#define dbg(...) vfPrintf(&sSerStream, LB __VA_ARGS__)
#else
#define dbg(...)
#undef WAIT_UART_OUTPUT
#define WAIT_UART_OUTPUT(...) // disabled

#endif

// 変数
static tsFILE sSerStream;          // シリアル用ストリーム
static tsSerialPortSetup sSerPort; // シリアルポートデスクリプタ
static uint32 u32Seq;              // 送信パケットのシーケンス番号
static tsAppData sAppData;


#ifdef DBG
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

#endif

static void vInitPort()
{
	// 使用ポートの設定
	vPortAsOutput(PORT_LED_1);
	vPortAsOutput(PORT_LED_2);
	vPortAsOutput(PORT_LED_3);
	vPortAsOutput(PORT_LED_4);
	vPortAsOutput(PORT_FELICA);

	vPortSetLo(PORT_LED_1);
	vPortSetLo(PORT_LED_2);
	vPortSetLo(PORT_LED_3);
	vPortSetLo(PORT_LED_4);
	vPortSetLo(PORT_FELICA);

	vPortAsInput(PORT_SW_1);
	vPortAsInput(PORT_SW_2);
	vPortAsInput(PORT_SW_3);
}

// ハードウェア初期化
static void vInitHardware()
{
	// デバッグ出力用
	vSerialInit();
	ToCoNet_vDebugInit(&sSerStream);
	ToCoNet_vDebugLevel(0);

	vInitPort();
	RCS620S_initDevice(&sSerStream);

}


// Masterへの送信実行
static bool_t sendToMaster(uint32 addr, void *payload, int size, uint8 type)
{
	tsTxDataApp tsTx;

	memset(&tsTx, 0, sizeof(tsTxDataApp));

	tsTx.u32SrcAddr = ToCoNet_u32GetSerial();
	tsTx.u32DstAddr = addr;
	//tsTx.u32DstAddr = TOCONET_MAC_ADDR_BROADCAST;

	tsTx.bAckReq = TRUE;
	tsTx.u8Retry = 0x01; // 送信失敗時は1回再送
	tsTx.u8CbId = u32Seq & 0xFF;
	tsTx.u8Seq = u32Seq & 0xFF;
	tsTx.u8Cmd = Felica;


	memcpy(tsTx.auData, payload, size);
	tsTx.u8Len = size;
	dbg("u8Len: %d", tsTx.u8Len);
	u32Seq++;

	// 送信
	return ToCoNet_bMacTxReq(&tsTx);
}

//*/
static tsFelicaData getFelicaData()
{
	char idm[8];
	tsFelicaData payload;

	// get IDm
	// TODO
	memcpy(payload.IDm, idm, sizeof(idm));

	return payload;
}

// ユーザ定義のイベントハンドラ
static void vProcessEvCore(tsEvent *pEv, teEvent eEvent, uint32 u32evarg)
{

	if (eEvent == E_EVENT_TICK_SECOND) {
		sAppData.u32parentDisconnectTime++;

		if (sAppData.u32parentDisconnectTime > RECONNECT_TIME)
		{
			dbg("master disconnected.");
			sAppData.u32parentDisconnectTime = 0;
			sAppData.u32parentAddr = 0;
			ToCoNet_Event_SetState(pEv, E_STATE_CHSCAN_INIT);
		}
	}

	// ステート処理
	switch (pEv->eState) {
		// アイドル状態
		case E_STATE_IDLE:
			dbg("E_STATE_IDLE");

			if (u32evarg & EVARG_START_UP_WAKEUP_RAMHOLD_MASK) {
				sAppData.u32parentDisconnectTime = 0;
				// 空きチャンネルスキャンに入る
				ToCoNet_Event_SetState(pEv, E_STATE_CHSCAN_INIT);
			}
			else {
				dbg("sleep");
				vPortSetLo(PORT_LED_3);
				sAppData.u32parentAddr = 0;
				ToCoNet_Event_SetState(pEv, E_STATE_APP_SLEEP);
			}
			break;

		case E_STATE_CHSCAN_INIT:
			if (sAppData.u32parentAddr == 0)
			{
				if (eEvent == E_EVENT_NEW_STATE) {
					dbg("E_EVENT_NEW_STATE");
				}

				//dbg("wait a small tick");
				if (ToCoNet_Event_u32TickFrNewState(pEv) > 200) {
					//ToCoNet_vRfConfig();
					vPortSetHi(PORT_LED_4);
					dbg("master scan...");
					ToCoNet_NbScan_bStart(CHANNEL_MASK, 128);
					ToCoNet_Event_SetState(pEv, E_STATE_CHSCANNING);
				}
			}
			else{
				// Chスキャン済み
				ToCoNet_Event_SetState(pEv, E_STATE_MEASURING);
			}
			break;
		case E_STATE_CHSCANNING:
			sAppData.u32parentDisconnectTime = 0;
			if (eEvent == E_EVENT_CHSCAN_FINISH)
			{
				dbg("CHSCAN finish. Ch%d selected.", sAppData.u8channel);
				vPortSetHi(PORT_LED_3);
				vPortSetLo(PORT_LED_4);

				//Ch変更
				sToCoNet_AppContext.u8Channel = sAppData.u8channel;
				ToCoNet_vRfConfig();
				ToCoNet_Event_SetState(pEv, E_STATE_MEASURING);
			}
			if (eEvent == E_EVENT_CHSCAN_FAIL)
			{
				dbg("CHSCAN failed.");
				vPortSetLo(PORT_LED_4);
				ToCoNet_Event_SetState(pEv, E_STATE_CHSCAN_INIT);
			}

			//タイムアウト
			if (ToCoNet_Event_u32TickFrNewState(pEv) > 2500) {
				dbg("CHSCAN timeout.");
				vPortSetLo(PORT_LED_4);
				ToCoNet_Event_SetState(pEv, E_STATE_CHSCAN_INIT);
			}

			WAIT_UART_OUTPUT(E_AHI_UART_0);

			break;
		case E_STATE_TRANSMITTING:
		    // TODO
			break;

		case E_STATE_APP_SLEEP:
			dbg("E_STATE_APP_SLEEP");
			if (eEvent == E_EVENT_NEW_STATE) {
				dbg("Sleeping...\r\n");
				WAIT_UART_OUTPUT(E_AHI_UART_0);
				ToCoNet_vSleep(E_AHI_WAKE_TIMER_0, SLEEP_INTERVAL, FALSE, FALSE);
			}
			break;

		default:
			break;
	}

}


//
// 以下 ToCoNet 既定のイベントハンドラ群
//

// 割り込み発生後に随時呼び出される
void cbToCoNet_vMain(void)
{
	if (ToCoNet_Event_eGetState(vProcessEvCore) == E_STATE_MEASURING)
	{
		// TODO: read card IDm
		ToCoNet_Event_Process(E_EVENT_MEASURE_FINISH, 0, vProcessEvCore);
	}


	return;
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
	return;
}

// パケット送信完了時
void cbToCoNet_vTxEvent(uint8 u8CbId, uint8 bStatus) {
	dbg("\n\r[TX CbID:%02x Status:%s]", u8CbId, bStatus ? "OK" : "Err");
	if (bStatus)
	{
		ToCoNet_Event_Process(E_EVENT_TRANSMIT_FINISH, 0, vProcessEvCore);
	}
	else
	{
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
				tsToCoNet_NbScan_Entitiy nbNodes[4];
				tsToCoNet_NbScan_Result *pNbsc = (tsToCoNet_NbScan_Result *)u32arg;
				dbg("%d", u32arg);
				uint8 i, nbFound = 0;

				if (pNbsc->u8scanMode & TOCONET_NBSCAN_NORMAL_MASK) {
					dbg("Mode: Normal Scan");
					memset(nbNodes, 0, sizeof(nbNodes));
					dbg("nodes: %d", pNbsc->u8found);
					// 全チャネルスキャン結果
					for (i = 0; i < pNbsc->u8found && i < 4; i++) {
						tsToCoNet_NbScan_Entitiy *pEnt = &pNbsc->sScanResult[pNbsc->u8IdxLqiSort[i]];
						if (pEnt->bFound) {
							nbFound++;
							nbNodes[i] = *pEnt;
							dbg("%d Ch:%d Addr:%08x", i, pEnt->u8ch, pEnt->u32addr);
						}
						WAIT_UART_OUTPUT(E_AHI_UART_0);
					}

					//検索結果あり
					if (nbFound > 0) {
						sAppData.u8channel = nbNodes[0].u8ch;
						sAppData.u32parentAddr = nbNodes[0].u32addr;
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

#ifdef DBG
		SPRINTF_vInit128();
#endif

		// clear application context
		memset(&sAppData, 0x00, sizeof(sAppData));
		sAppData.u8channel = 15;
		sAppData.u8retry = 1;
		sAppData.u32parentAddr = 0x0;


		// ユーザ定義のイベントハンドラを登録
		ToCoNet_Event_Register_State_Machine(vProcessEvCore);

		// ハードウェア初期化
#ifdef DBG
		vInitHardware();
#endif

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
#ifdef DBG
		vInitHardware();
#endif
		ToCoNet_vMacStart();
	}
	return;
}
