//
//  statistics.c
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

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <sys/time.h>

#include "statistics.h"
#include "frame-parser.h"
#include "utils.h"


statistics_t g_stats[255];
uint32_t g_crc_error_count;
uint32_t g_total_recvd_frames;

void
analyzer (uint8_t *data, size_t len)
{
    frame_t *frame = (frame_t *) data;
    struct timespec tp;
    int lost_frames;
    
    if (frame->header.dest == BCAST_ADDRESS ||
         frame->header.dest == own_address (GET_PARAMETER, 0))
    {
        clock_gettime (CLOCK_MONOTONIC, &tp);
        uint32_t latency = (uint32_t) (tp.tv_nsec / 1000);
        
        g_stats[frame->header.src].frames_recvd++;
        
        if (latency > frame->header.timestamp)
        {
            latency -= frame->header.timestamp;
        }
        else
        {
            latency = latency + 1000000 - frame->header.timestamp;
        }
        
        // store latency values
        g_stats[frame->header.src].latency_sum += latency;
        g_stats[frame->header.src].latency_samples++;
        
        if (latency > g_stats[frame->header.src].latency_max)
        {
            g_stats[frame->header.src].latency_max = latency;
        }
        if (latency < g_stats[frame->header.src].latency_min)
        {
            g_stats[frame->header.src].latency_min = latency;
        }
        
        // identify lost frames
        if (g_stats[frame->header.src].frames_recvd != 0)
        {
            if (g_stats[frame->header.src].last_index < frame->header.index)
            {
                lost_frames = frame->header.index - g_stats[frame->header.src].last_index - 1;
            }
            else
            {
                lost_frames = 255 + frame->header.index - g_stats[frame->header.src].last_index;
            }
            g_stats[frame->header.src].frames_lost += lost_frames;
        }
        g_stats[frame->header.src].last_index = frame->header.index;
        
        // store rssi value
        if ( g_stats[frame->header.src].rssi_samples > 10)
        {
            g_stats[frame->header.src].rssi_sum = data[len - 1];
            g_stats[frame->header.src].rssi_samples = 1;
        }
        else
        {
            g_stats[frame->header.src].rssi_sum += data[len - 1];
            g_stats[frame->header.src].rssi_samples++;
        }
        
        // handle specific frames
        switch (frame->header.type)
        {
            case LOW_LATENCY:
                break;
                
            case FILE_XFER:
                break;
        }
    }
}

//  @brief  Clear the statistic data
void
clear_stats (void)
{
    statistics_t *ps;
    int i;
    
    for (i = 0, ps = g_stats; i < 255; i++, ps++)
    {
        ps->frames_lost = 0;
        ps->frames_recvd = 0;
        ps->latency_max = 0;
        ps->latency_min = 100000;   // initial value 100 ms
        ps->latency_samples = 0;
        ps->latency_sum = 0;
        ps->rssi_samples = 0;
        ps->rssi_sum = 0;
    }
    g_crc_error_count = 0;
    g_total_recvd_frames = 0;
}
