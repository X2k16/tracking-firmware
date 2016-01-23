
typedef struct {
	// MAC SETTING
	uint8 u8channel;
	uint8 u8retry;
	int32 i32transmitDelayRand;

	// Parent
	uint32 u32parentAddr;
	uint32 u32parentDisconnectTime;

	// TICK COUNT
	uint8 u8tick_ms;

	// Config
	uint8 u8settingsMode;
	uint8 u8settingsLatest;
	/*
	//tsSettings sSettings;

	// Question
	//tsQuestionData sQuestionData;

	// Answer
	uint8 u8answer;

	// Button
	uint8 u8button;

	// LED
	tsBlinkLed sLedPower;
	tsBlinkLed sLedLink;
	tsBlinkLed sLedActive;

	// ADC
	tsObjData_ADC sObjADC; //!< ADC管理構造体（データ部）
	tsSnsObj sADC; //!< ADC管理構造体（制御部）
	bool_t bUpdatedAdc; //!< TRUE:ADCのアップデートが有った。アップデート検出後、FALSE に戻す事。
	uint8 u8AdcState; //!< ADCの状態 (0xFF:初期化前, 0x0:ADC開始要求, 0x1:AD中, 0x2:AD完了)

	uint16 u16volt; //供給電圧
	uint16 u16batt; //電池電圧
	*/

	// 統計情報
	uint8 u8Lqi; //最後の受信Lqi
	uint32 u32statusTransmitTime; //次のstatus送信までの残り時間

} tsAppData;

