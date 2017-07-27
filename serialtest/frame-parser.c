//
//  frame-parser.c
//  serialtest
//
//  Copyright (c) 2017 Lix N. Paulian (lix@paulian.net)
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

#include <unistd.h>
#include <stdbool.h>

#include "utils.h"
#include "frame-parser.h"

#define PARSER_DEBUG 0

pthread_mutex_t send_serial_mutex = PTHREAD_MUTEX_INITIALIZER;
ipc_t ipc;


//  @brief This function parses 0xf0/0xf1 (begin/end) type frames.
//  @param begin: pointer to a buffer's begin to parse; the pointer on the frame
//      begin will be returned here.
//  @param end: pointer on the buffer's end; the pointer on the frame's end will
//      be returned here.
//  @retval parsing result (see parse_result_t typedef).

parse_result_t
parse_f0_f1_frames (uint8_t **begin, uint8_t **end)
{
    uint8_t *p = *begin;
    int result = FRAME_NOT_FOUND;
    
#if PARSER_DEBUG == 1
    fprintf (stdout, "in %ld, ", *end - *begin + 1);
#endif
    
    // find the frame's start
    while (*p != 0xf0 && p < *end)
    {
        p++;
    }
    
    if (*p == 0xf0) // start of frame
    {
        *begin = p; // store the frame start
        if (p < *end)
            p++;   // and get over it
        
        while (*p != 0xf1 && p < *end)
        {
            if (*p == 0xf0)
            {
                *begin = p;  // new frame start, reset search
            }
            p++;
        }
        
        if (*p == 0xf1) // end of frame
        {
            if (p == *end)
            {
                result = FRAME_TRUNCATED;
            }
            else
            {
                p++;    // add rssi byte
                *end = p;
                result = FRAME_OK;
            }
        }
        else
        {
            *end = p;
            result = FRAME_TRUNCATED;
        }
    }
 
#if PARSER_DEBUG == 1
    fprintf (stdout, "out %ld\n", *end - *begin + 1);
#endif
    
    return result;
}

//  @brief Print an 0xf0/0xf1 frame to the console.
//  @param buff: buffer containing the frame.
//  @param len: length of the frame.

void
print_f0_f1_frames (uint8_t *buff, ssize_t len)
{
    if (buff[len - 2] == 0xf1)      // do we have a valid rssi?
    {
        fprintf (stdout, "%3ld bytes, rssi %03d dBm: ", len, buff[len - 1] * -1);
    }
    else
    {
        fprintf (stdout, "%3ld bytes, rssi unknown: ", len);
    }
    
    for (int i = 0; i < len; i++)
    {
        fprintf (stdout, "%02x ", buff[i]);
    }
    fprintf (stdout, "\n");
}

// @brief Send frames over the serial port.
// @param p: void pointer, contains the file descriptor of the serial port.
// @retval a null pointer.

void *
send_frames (void *p)
{
    int fd = *(int *) p;
    uint8_t send_buffer[20];
    struct timespec sts;
    bool send_low_latency = false;
    frame_hdr_t *hdr;
    
    send_buffer[0] = 0xf0;
    send_buffer[19] = 0xf1;
    hdr = (frame_hdr_t *) &send_buffer[1];
    
    while (true)
    {
        pthread_mutex_lock (&send_serial_mutex);
        if (ipc.cmd)
        {
            switch (ipc.cmd)
            {
                case SEND_LOW_LATENCY_FRAMES:
                    send_low_latency = true;
                    hdr->dest = ipc.address;
                    hdr->src = own_address(GET_PARAMETER, 0);
                    hdr->index = 0;
                    break;
                    
                case STOP_LOW_LATENCY_FRAMES:
                    send_low_latency = false;
                    break;
                    
                default:
                    // unknown command
                    break;
            }
            ipc.cmd = NOP;
        }
        pthread_mutex_unlock (&send_serial_mutex);
        
        if (send_low_latency)
        {
            hdr->index += 1;
            if (hdr->index == 0xf0)
            {
                hdr->index = 0;
            }
            
            if (write (fd, send_buffer, sizeof(send_buffer)) < 0)
            {
                perror("serial port write");
                break;
            }
        }
        
        sts.tv_nsec = 10000000; // 10 ms
        sts.tv_sec = 0;
        nanosleep (&sts, NULL);
    }
    
    printf ("Send thread exiting now\n");
    
    pthread_exit (NULL);
}
