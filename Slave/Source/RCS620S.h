/*
 * RC-S620/S sample library for Arduino
 *
 * Copyright 2010 Sony Corporation
 */

#include <inttypes.h>

#ifndef RCS620S_H_
#define RCS620S_H_

/* --------------------------------
 * Constant
 * -------------------------------- */

#define RCS620S_MAX_CARD_RESPONSE_LEN    254
#define RCS620S_MAX_RW_RESPONSE_LEN      265

/* --------------------------------
 * Class Declaration
 * -------------------------------- */


    int RCS620S_initDevice(tsFILE* pSer);
    int polling(uint16_t systemCode);//  = 0xffff);
    int cardCommand(
        const uint8_t* command,
        uint8_t commandLen,
        uint8_t response[RCS620S_MAX_CARD_RESPONSE_LEN],
        uint8_t* responseLen);
    int rfOff(void);

    int push(
        const uint8_t* data,
        uint8_t dataLen);
    int rwCommand(
        unsigned char* command,
        uint16_t commandLen,
        uint8_t response[RCS620S_MAX_RW_RESPONSE_LEN],
        uint16_t* responseLen);
    void cancel(void);
    uint8_t calcDCS(
        const uint8_t* data,
        uint16_t len);

    void writeSerial(
        unsigned char* data,
        uint16_t len);
    int readSerial(
        unsigned char* data,
        uint16_t len);
    void flushSerial(void);

    int checkTimeout(unsigned long t0);
    void delay(int count);

//    unsigned long timeout;

uint8_t rcs_idm[8];
uint8_t rcs_pmm[8];

#endif /* !RCS620S_H_ */
