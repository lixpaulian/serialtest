//
//  statistics.h
//  serialtest
//
//  Copyright (c) 2017, 2018 Lix N. Paulian (lix@paulian.net)
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
//  Created on 28/07/2017 (lnp)
//

#ifndef statistics_h
#define statistics_h

#include <stdio.h>

typedef struct statistics_
{
    uint8_t last_index;
    int frames_recvd;
    int frames_lost;
    uint32_t latency_max;
    uint32_t latency_min;
    uint64_t latency_sum;
    uint32_t latency_samples;
    uint32_t rssi_sum;
    uint32_t rssi_samples;
} statistics_t;

extern statistics_t g_stats[];
extern uint32_t g_crc_error_count;
extern uint32_t g_total_recvd_frames;

void
analyzer (uint8_t *frame, size_t len);

void
clear_stats (void);

#endif /* statistics_h */
