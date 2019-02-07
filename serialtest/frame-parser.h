//
//  frame-parser.h
//  serialtest
//
//  Copyright (c) 2017 - 2019 Lix N. Paulian (lix@paulian.net)
//
//  Permission is hereby granted, free of charge, to any person obtaining a copy
//  of this software and associated documentation files (the "Software"), to deal
//  in the Software without restriction, including without limitation the rights
//  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
//  copies of the Software, and to permit persons to whom the Software is
//  furnished to do so, subject to the following conditions:
//
//  The above copyright notice and this permission notice shall be included in all
//  copies or substantial portions of the Software.
//
//  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
//  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
//  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
//  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
//  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
//  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
//  SOFTWARE.
//
//  Created on 20/07/2017 (lnp)
//

#ifndef frame_parser_h
#define frame_parser_h

#include <stdio.h>
#include <pthread.h>

#include "utils.h"

#define USE_IOSSIOSPEED true

#define SOF_CHAR 0xf0   // start of frame
#define EOF_CHAR 0xf1   // end of frame
#define ESCAPE_CHAR 0xf2
#define SOH_CHAR 0xf3   // start of header
#define MAX_FRAME_LEN 240

typedef enum
{
    FRAME_NOT_FOUND = -1,
    FRAME_OK = 0,
    FRAME_TRUNCATED,
} parse_result_t;

typedef struct __attribute__ ((packed))
{
    uint8_t len;
    uint8_t dest;
    uint8_t src;
    uint8_t index;
    uint8_t type;
    uint32_t timestamp;
} frame_hdr_t;

typedef struct
{
    frame_hdr_t header;
    uint8_t payload[];  // length is variable
} frame_t;

// interprocess communication structure
typedef struct
{
    int cmd;
    uint8_t address;
    uint32_t parameter0;
    uint32_t parameter1;
    uint32_t parameter2;
} ipc_t;

typedef enum
{
    NOP,
    SEND_LOW_LATENCY_FRAMES,
    SEND_LOW_LATENCY_FRAMES_WITH_HEADER,
    STOP_LOW_LATENCY_FRAMES,
    INTERVAL,
    LENGTH,
    SEND_FILE,
    SET_CHANNEL,
    SET_MASTER,
    SET_RATE,
    SET_REGION,
    SET_HOP_PARAMS,
    SET_HOP_STRETCHING,
    SET_BAUD,
    SET_SLOT,
    SET_BW,
    SET_PROTOCOL,
    GET_TRAFFIC_STATS,
    GET_RED_TRAFFIC_STATS,
} serial_cmds_t;

typedef enum
{
    LOW_LATENCY,
    FILE_XFER,
    SET_RADIO_CHANNEL,
    SET_RADIO_RATE,
    HIGHEST_CMD
} radio_cmds_t;

typedef enum
{
    MOD_OQPSK_100K = 0,
    MOD_OQPSK_250K,
    MOD_GFSK_1M,
    MOD_GFSK_2M
} rate_t;


parse_result_t
parse_f0_f1_frames (uint8_t **buff, uint8_t **end, int8_t *rssi);

void
print_frames (uint8_t *buff, size_t len, int8_t rssi);

void *
send_frames (void *p);

ssize_t
send_frame (int fd, uint8_t *frame, int count, op_mode_t type, uint8_t slot);

int
extract_f0_f1_frame (uint8_t *buff, size_t len);


#endif /* frame_parser_h */
