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
#include <string.h>
#include <sys/time.h>

#include "utils.h"
#include "frame-parser.h"

#define PARSER_DEBUG 0
#define SERIAL_DEBUG 0

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
    while (*p != SOF_CHAR && p < *end)
    {
        p++;
    }
    
    if (*p == SOF_CHAR) // start of frame
    {
        *begin = p; // store the frame start
        if (p < *end)
            p++;   // and get over it
        
        while (*p != EOF_CHAR && p < *end)
        {
            if (*p == SOF_CHAR)
            {
                *begin = p;  // new frame start, reset search
            }
            p++;
        }
        
        if (*p == EOF_CHAR) // end of frame
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
print_f0_f1_frames (uint8_t *buff, size_t len)
{
    if (buff[len - 2] == EOF_CHAR)      // do we have a valid rssi?
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

//  @brief Extract the useful data from an 0xf0/0xf1 frame.
//  @param buff: pointer on a 0xf0/0xf1 frame
//  @param len: length of the frame
//  @retval The number of bytes the resulting frame has; the "cleaned" frame
//      is returned at the "buff" pointer

int
extract_f0_f1_frame (uint8_t *buff, size_t len)
{
    uint8_t *p = buff;
    uint8_t *q = buff;
    int count = 0;
    
    if (buff[len - 2] == EOF_CHAR && *buff == SOF_CHAR) // do we have a valid frame + rssi?
    {
        p++;    // skip the start of frame (0xf0)
        while (*p != EOF_CHAR)
        {
            if (*p == ESCAPE_CHAR)
            {
                p++;
                if (*p == 0)
                    *q = SOF_CHAR;
                else if (*p == 1)
                    *q = EOF_CHAR;
                else if (*p == 2)
                    *q = ESCAPE_CHAR;
                else
                    break;  // error! malformed frame
                q++;
                p++;
            }
            else
            {
                *q++ = *p++;
            }
        }
        if (*p++ != EOF_CHAR)
        {
            count = -1;
        }
        else
        {
            *q = *p;    // copy rssi at the end of the frame
            count = (int) (q - buff) + 1;
        }
    }
    else
    {
        count = -1; // invalid frame
    }
    
    return count;
}

// @brief Send frames over the serial port.
// @param p: void pointer, contains the file descriptor of the serial port.
// @retval a null pointer.

void *
send_frames (void *p)
{
#define LOCAL_BUFFER_SIZE 22
    int fd = *(int *) p;
    uint8_t send_buffer[LOCAL_BUFFER_SIZE + 2];    // +2 for CRC
    uint8_t cc_buffer[20];
    struct timespec sts;
    bool send_periodically = false;
    bool send_one_time = false;
    frame_t *frame;
    uint8_t dest_address = 0;
    int count = LOCAL_BUFFER_SIZE - sizeof (frame_hdr_t);
    
    frame = (frame_t *) &send_buffer;
    frame->header.index = 0;
    frame->header.src = own_address(GET_PARAMETER, 0);
    
    while (true)
    {
        pthread_mutex_lock (&send_serial_mutex);
        if (ipc.cmd)
        {
            switch (ipc.cmd)
            {
                case SEND_LOW_LATENCY_FRAMES:
                dest_address = ipc.address;
                count = LOCAL_BUFFER_SIZE;
                send_periodically = true;
                break;
                
                case STOP_LOW_LATENCY_FRAMES:
                send_periodically = false;
                break;
                
                case SET_CHANNEL:
                frame->header.dest = ipc.address;
                frame->header.type = SET_RADIO_CHANNEL;
                frame->payload[0] = (uint8_t) ipc.parameter;
                count = 1;
                send_one_time = true;
                break;
                
                case SET_RATE:
                frame->header.dest = ipc.address;
                frame->header.type = SET_RADIO_RATE;
                frame->payload[0] = (uint8_t) ipc.parameter;
                count = 1;
                send_one_time = true;
                break;
                
                case SET_HOP_PARAMS:
                cc_buffer[0] = 0xcc;
                cc_buffer[1] = 0x67;    // set/get hop parameters
                cc_buffer[2] = ipc.parameter & 0xff;
                cc_buffer[3] = (ipc.parameter >> 8) & 0xff;
                cc_buffer[4] = ipc.parameter1 & 0xff;
                cc_buffer[5] = (ipc.parameter1 >> 8) & 0xff;
                if (write (fd, cc_buffer, 6) < 0)
                {
                    perror("serial port write");
                    break;
                }
                break;
                
                default:
                // unknown command
                break;
            }
            ipc.cmd = NOP;
        }
        pthread_mutex_unlock (&send_serial_mutex);
        
        if (send_periodically || send_one_time)
        {
            frame->header.index += 1;
            
            struct timespec tp;
            clock_gettime (CLOCK_MONOTONIC, &tp);
            frame->header.timestamp = (uint32_t) (tp.tv_nsec / 1000);
            
            frame->header.magic = MAGIC;
            
            if (!send_one_time)
            {
                frame->header.type = LOW_LATENCY;
                frame->header.dest = dest_address;
                memset (&frame->payload, 0x55, LOCAL_BUFFER_SIZE - sizeof (frame_hdr_t));
                count = LOCAL_BUFFER_SIZE - sizeof (frame_hdr_t);
            }
            
            // compute frame's CRC
            uint16_t crc = calcCRC(0, send_buffer, count + sizeof (frame_hdr_t));
            send_buffer[count + sizeof (frame_hdr_t)] = (uint8_t) crc;
            send_buffer[count + sizeof (frame_hdr_t) + 1] = (uint8_t) (crc >> 8) & 0xFF;
            
            count += 2;
            if (send_f0_f1_frame (fd, send_buffer, count + sizeof (frame_hdr_t)) < 0)
            {
                perror("serial port write");
                break;
            }
            
            if (send_one_time)
            {
                send_one_time = false;
            }
        }
        
        sts.tv_nsec = 8000000 + own_address(GET_PARAMETER, 0) * 1000000; // ~10 ms
        sts.tv_sec = 0;
        nanosleep (&sts, NULL);
    }
    
    pthread_exit (NULL);
}


ssize_t
send_f0_f1_frame (int fd, uint8_t *frame, int count)
{
    uint8_t send_buffer[MAX_FRAME_LEN];
    uint8_t *p = send_buffer;
    int i;
    
    *p++ = SOF_CHAR;
    
    for (i = 0; i < count; i++)
    {
        if (p < send_buffer + MAX_FRAME_LEN - 1)
        {
            switch (frame[i])
            {
                case SOF_CHAR:
                    *p++ = ESCAPE_CHAR;
                    *p++ = 0;
                    break;
                    
                case EOF_CHAR:
                    *p++ = ESCAPE_CHAR;
                    *p++ = 1;
                    break;
                    
                case ESCAPE_CHAR:
                    *p++ = ESCAPE_CHAR;
                    *p++ = 2;
                    break;
                    
                default:
                    *p++ = frame[i];
            }
        }
    }
    *p++ = EOF_CHAR;
    count = (int) (p - send_buffer);
    
#if SERIAL_DEBUG == 1
    fprintf (stdout, "sent %d bytes, ", count);
    for (int i = 0; i < count; i++)
    {
        fprintf (stdout, "%02x ", send_buffer[i]);
    }
    fprintf (stdout, "\n");
#endif
    return write (fd, send_buffer, count);
}
