/*
 * RC-S620/S sample library for Arduino
 *
 * Copyright 2010 Sony Corporation
 */

#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <AppHardwareApi.h>		// NXP ペリフェラル API 用
#include "utils.h"				// ペリフェラル API のラッパなど
#include "serial.h"				// シリアル用
#include "sprintf.h"			// SPRINTF 用

#include "RCS620S.h"

#define echo(...) vfPrintf(&sSerStream, LB __VA_ARGS__)

/* --------------------------------
 * Constant
 * -------------------------------- */

#define RCS620S_DEFAULT_TIMEOUT  1000
static tsFILE sSerStream;          // シリアル用ストリーム


int RCS620S_initDevice(tsFILE* pSer)
{
    int ret;
    uint8_t response[RCS620S_MAX_RW_RESPONSE_LEN];
    uint16_t responseLen;

    /* RFConfiguration (various timings) */
    ret = rwCommand((unsigned char *)"\xd4\x32\x02\x00\x00\x00", 6,
                    response, &responseLen);
    if (!ret || (responseLen != 2) ||
        (memcmp(response, "\xd5\x33", 2) != 0)) {
        return 0;
    }

    /* RFConfiguration (max retries) */
    ret = rwCommand((unsigned char *)"\xd4\x32\x05\x00\x00\x00", 6,
                    response, &responseLen);
    if (!ret || (responseLen != 2) ||
        (memcmp(response, "\xd5\x33", 2) != 0)) {
        return 0;
    }

    /* RFConfiguration (additional wait time = 24ms) */
    ret = rwCommand((unsigned char *)"\xd4\x32\x81\xb7", 4,
                    response, &responseLen);
    if (!ret || (responseLen != 2) ||
        (memcmp(response, "\xd5\x33", 2) != 0)) {
        return 0;
    }

    sSerStream = *pSer;

    return 1;
}

int polling(uint16_t systemCode)
{
    int ret;
    uint8_t buf[9];
    uint8_t response[RCS620S_MAX_RW_RESPONSE_LEN];
    uint16_t responseLen;

    /* InListPassiveTarget */
    memcpy(buf, "\xd4\x4a\x01\x01\x00\xff\xff\x00\x00", 9);
    buf[5] = (uint8_t)((systemCode >> 8) & 0xff);
    buf[6] = (uint8_t)((systemCode >> 0) & 0xff);

    ret = rwCommand((unsigned char *)buf, 9, response, &responseLen);
    if (!ret || (responseLen != 22) ||
        (memcmp(response, "\xd5\x4b\x01\x01\x12\x01", 6) != 0)) {
        return 0;
    }

    memcpy(rcs_idm, response + 6, 8);
    memcpy(rcs_pmm, response + 14, 8);

    return 1;
}

int cardCommand(
    const uint8_t* command,
    uint8_t commandLen,
    uint8_t response[RCS620S_MAX_CARD_RESPONSE_LEN],
    uint8_t* responseLen)
{
    int ret;
    uint16_t commandTimeout;
    uint8_t buf[RCS620S_MAX_RW_RESPONSE_LEN];
    uint16_t len;

    if (RCS620S_DEFAULT_TIMEOUT >= (0x10000 / 2)) {
        commandTimeout = 0xffff;
    } else {
        commandTimeout = (uint16_t)(RCS620S_DEFAULT_TIMEOUT * 2);
    }

    /* CommunicateThruEX */
    buf[0] = 0xd4;
    buf[1] = 0xa0;
    buf[2] = (uint8_t)((commandTimeout >> 0) & 0xff);
    buf[3] = (uint8_t)((commandTimeout >> 8) & 0xff);
    buf[4] = (uint8_t)(commandLen + 1);
    memcpy(buf + 5, command, commandLen);

    ret = rwCommand((unsigned char *)buf, 5 + commandLen, buf, &len);
    if (!ret || (len < 4) ||
        (buf[0] != 0xd5) || (buf[1] != 0xa1) || (buf[2] != 0x00) ||
        (len != (3 + buf[3]))) {
        return 0;
    }

    *responseLen = (uint8_t)(buf[3] - 1);
    memcpy(response, buf + 4, *responseLen);

    return 1;
}

int rfOff(void)
{
    int ret;
    uint8_t response[RCS620S_MAX_RW_RESPONSE_LEN];
    uint16_t responseLen;

    /* RFConfiguration (RF field) */
    ret = rwCommand((unsigned char *)"\xd4\x32\x01\x00", 4,
                    response, &responseLen);
    if (!ret || (responseLen != 2) ||
        (memcmp(response, "\xd5\x33", 2) != 0)) {
        return 0;
    }

    return 1;
}


int rwCommand(
    unsigned char* command,
    uint16_t commandLen,
    uint8_t response[RCS620S_MAX_RW_RESPONSE_LEN],
    uint16_t* responseLen)
{
    int ret;
    unsigned char buf[9];

    flushSerial();

    uint8_t dcs = calcDCS(command, commandLen);

    /* transmit the command */
    buf[0] = 0x00;
    buf[1] = 0x00;
    buf[2] = 0xff;
    if (commandLen <= 255) {
        /* normal frame */
        buf[3] = commandLen;
        buf[4] = (uint8_t)-buf[3];
        writeSerial(buf, 5);
    } else {
        /* extended frame */
        buf[3] = 0xff;
        buf[4] = 0xff;
        buf[5] = (uint8_t)((commandLen >> 8) & 0xff);
        buf[6] = (uint8_t)((commandLen >> 0) & 0xff);
        buf[7] = (uint8_t)-(buf[5] + buf[6]);
        writeSerial(buf, 8);
    }
    writeSerial(command, commandLen);
    buf[0] = dcs;
    buf[1] = 0x00;
    writeSerial(buf, 2);

    /* receive an ACK */
    ret = readSerial(buf, 6);
    if (!ret || (memcmp(buf, "\x00\x00\xff\x00\xff\x00", 6) != 0)) {
        cancel();
        return 0;
    }

    /* receive a response */
    ret = readSerial(buf, 5);
    if (!ret) {
        cancel();
        return 0;
    } else if  (memcmp(buf, "\x00\x00\xff", 3) != 0) {
        return 0;
    }
    if ((buf[3] == 0xff) && (buf[4] == 0xff)) {
        ret = readSerial(buf + 5, 3);
        if (!ret || (((buf[5] + buf[6] + buf[7]) & 0xff) != 0)) {
            return 0;
        }
        *responseLen = (((uint16_t)buf[5] << 8) |
                        ((uint16_t)buf[6] << 0));
    } else {
        if (((buf[3] + buf[4]) & 0xff) != 0) {
            return 0;
        }
        *responseLen = buf[3];
    }
    if (*responseLen > RCS620S_MAX_RW_RESPONSE_LEN) {
        return 0;
    }

    ret = readSerial(response, *responseLen);
    if (!ret) {
        cancel();
        return 0;
    }

    dcs = calcDCS(response, *responseLen);

    ret = readSerial(buf, 2);
    if (!ret || (buf[0] != dcs) || (buf[1] != 0x00)) {
        cancel();
        return 0;
    }

    return 1;
}

void cancel(void)
{
    /* transmit an ACK */
    writeSerial((unsigned char*)"\x00\x00\xff\x00\xff\x00", 6);
    delay(1);
    flushSerial();
}

uint8_t calcDCS(
    const uint8_t* data,
    uint16_t len)
{
    uint8_t sum = 0;
    uint16_t i;

    for (i = 0; i < len; i++) {
        sum += data[i];
    }

    return (uint8_t)-(sum & 0xff);
}

void writeSerial(
    unsigned char* data,
    uint16_t len)
{
    int i;
    for (i = 0; i < len; i++)
    {
        echo("%c", *data);
        data++;
    }
}

int readSerial(
    unsigned char* data,
    uint16_t len)
{
    uint16_t nread = 0;
    //unsigned long t0 = millis();

    while (nread < len) {
        //if (checkTimeout(t0)) {
        //    return 0;
        //}

        if (SERIAL_bRxQueueEmpty(0) == FALSE) {
            data[nread] = SERIAL_i16RxChar(0);
            nread++;
        }
    }

    return 1;
}

void flushSerial(void)
{
    //TODO
    SERIAL_vFlush(0);
    //Serial.flush();
}

int checkTimeout(unsigned long t0)
{
    /*
    unsigned long t = millis();

    if ((t - t0) >= RCS620S_DEFAULT_TIMEOUT) {
        return 1;
    }
    */
    return 0;
}

void delay(int count)
{
    int i;
    for (i = 0; i < count; i++);
}
