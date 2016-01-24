
typedef struct {
	// MAC SETTING
	uint8 u8channel;
	uint8 u8retry;

	// Parent
	uint32 u32parentAddr;
	uint32 u32parentDisconnectTime;

	// TICK COUNT
	uint8 u8tick_ms;

} tsAppData;


typedef struct {
	uint16 length;
	uint8 data[128];
} tsFelicaResponse;
