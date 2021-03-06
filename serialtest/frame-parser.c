//
//  frame-parser.c
//  serialtest
//
//  Copyright (c) 2017 - 2021 Lix N. Paulian (lix@paulian.net)
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
#include <termios.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <IOKit/serial/ioss.h>

#include "utils.h"
#include "frame-parser.h"

#define PARSER_DEBUG 0
#define SERIAL_DEBUG 0

pthread_mutex_t send_serial_mutex = PTHREAD_MUTEX_INITIALIZER;
ipc_t ipc;

ssize_t
send_command (int fd, uint8_t *command, size_t cmd_len, size_t max_len);


//  @brief This function parses 0xf0/0xf1 (begin/end) type frames.
//  @param begin: pointer to a buffer's begin to parse; the pointer on the frame
//      begin will be returned here.
//  @param end: pointer on the buffer's end; the pointer on the frame's end will
//      be returned here.
//  @retval parsing result (see parse_result_t typedef).

parse_result_t
parse_f0_f1_frames (uint8_t **begin, uint8_t **end, int8_t *rssi)
{
    uint8_t *p = *begin;
    int result = FRAME_NOT_FOUND;
    static int8_t local_rssi = 0;
    static bool white_plus = false;
    
#if PARSER_DEBUG == 1
    fprintf (stdout, "in %ld, ", *end - *begin + 1);
#endif
    
    // find the frame's start
    while (*p != SOF_CHAR && *p != SOH_CHAR && p < *end)
    {
        p++;
    }
    
    if (*p == SOH_CHAR)
    {
        if (p + 3 < *end && *(p + 3) == SOF_CHAR )
        {
            local_rssi = *(p + 2);
            p += 3;
        }
        else
        {
            *end = p;
            result = FRAME_TRUNCATED;
        }
        white_plus = true;
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
            if (p == *end && white_plus == false)
            {
                result = FRAME_TRUNCATED;
            }
            else
            {
                // add rssi byte
                if (white_plus)
                {
                    *rssi = local_rssi;
                }
                else
                {
                    p++;
                    *rssi = *p * -1;
                }
                *end = p;
                white_plus = false;
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
print_frames (uint8_t *buff, size_t len, int8_t rssi)
{
    fprintf (stdout, "%3ld bytes, rssi %03d dBm: ", len, rssi);
    
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
    
    if ((buff[len - 2] == EOF_CHAR || buff[len - 1] == EOF_CHAR) && *buff == SOF_CHAR) // do we have a valid frame + rssi?
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
    /* note: the frame is 90 bytes long + 2 bytes CRC. This is close to the maximum permissible
     for a 3.5 ms frame at 250 Kbps OQPSK (e.g. ZigBee) */
#define LOCAL_BUFFER_SIZE 120
    static int frame_size = 22;  // default
    int fd = *(int *) p;
    uint8_t send_buffer[LOCAL_BUFFER_SIZE + 2];    // +2 for CRC
    uint8_t cc_buffer[20];
    struct timespec sts;
    bool send_periodically = false;
    bool send_one_time = false;
    frame_t *frame;
    uint8_t dest_address = 0;
#if USE_IOSSIOSPEED == false
    struct termios options;
#endif
    int count = frame_size - sizeof (frame_hdr_t);
    static int interval = 20; // ms
    uint8_t slot = 0;
    
    // set cmd/data line to data (true)
    if (cmd_data(fd, true) == false)
    {
        fprintf (stdout, "Could not switch CMD/DATA line\n");
    }
    
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
                case SEND_LOW_LATENCY_FRAMES_WITH_HEADER:
                    dest_address = ipc.address;
                    count = frame_size;
                    send_periodically = true;
                    slot = ipc.parameter0;
                    break;
                    
                case SEND_PLAIN_FRAME:
                    write (fd, ipc.text, ipc.parameter0);
                    break;
                    
                case STOP_LOW_LATENCY_FRAMES:
                    send_periodically = false;
                    break;
                    
                case INTERVAL:
                    interval = ipc.parameter0;
                    break;
                    
                case LENGTH:
                    frame_size = ipc.parameter0;
                    break;

                case SET_CHANNEL:
                    cc_buffer[0] = 0xcc;
                    cc_buffer[1] = 0x02;    // set/get channel
                    cc_buffer[2] = ipc.parameter0 & 0xff;
                    if (send_command (fd, cc_buffer, 3, sizeof(cc_buffer)) < 0)
                    {
                        perror("send command:");
                    }
                    break;
                    
                case SET_MASTER:
                    cc_buffer[0] = 0xcc;
                    cc_buffer[1] = 0x03;    // set/get channel
                    cc_buffer[2] = ipc.parameter0 & 0xff;
                    if (send_command (fd, cc_buffer, 3, sizeof(cc_buffer)) < 0)
                    {
                        perror("send command:");
                    }
                    break;
                    
                case SET_RATE:
                    cc_buffer[0] = 0xcc;
                    cc_buffer[1] = 0x66;    // set/get bit rate
                    cc_buffer[2] = ipc.parameter0 & 0xff;
                    if (send_command (fd, cc_buffer, 3, sizeof(cc_buffer)) < 0)
                    {
                        perror("send command:");
                    }
                    break;
                    
                case SET_HOP_PARAMS:
                    cc_buffer[0] = 0xcc;
                    cc_buffer[1] = 0x67;    // set/get hop parameters
                    cc_buffer[2] = ipc.parameter0 & 0xff;
                    cc_buffer[3] = (ipc.parameter0 >> 8) & 0xff;
                    cc_buffer[4] = ipc.parameter1 & 0xff;
                    cc_buffer[5] = (ipc.parameter1 >> 8) & 0xff;
                    if (send_command (fd, cc_buffer, 6, sizeof(cc_buffer)) < 0)
                    {
                        perror("send command:");
                    }
                    
                    sts.tv_nsec = 10000000; // 10 ms
                    sts.tv_sec = 0;
                    nanosleep (&sts, NULL);

                    cc_buffer[0] = 0xcc;
                    cc_buffer[1] = 0x68;    // set/get slots number
                    cc_buffer[2] = ipc.parameter2 & 0xff;
                    if (send_command (fd, cc_buffer, 3, sizeof(cc_buffer)) < 0)
                    {
                        perror("send command:");
                    }
                    break;
                    
                case SET_HOP_STRETCHING:
                    cc_buffer[0] = 0xcc;
                    cc_buffer[1] = 0x69;    // set/get hop stretching
                    cc_buffer[2] = ipc.parameter0 & 0xff;
                    cc_buffer[3] = (ipc.parameter0 >> 8) & 0xff;
                    if (send_command (fd, cc_buffer, 4, sizeof(cc_buffer)) < 0)
                    {
                        perror("send command:");
                    }
                    break;

                case SET_BAUD:
#if USE_IOSSIOSPEED == false
                    tcgetattr (fd, &options);
                    cfsetispeed (&options, ipc.parameter0);
                    cfsetospeed (&options, ipc.parameter0);
#endif

                    cc_buffer[0] = 0xcc;
                    cc_buffer[1] = 0x50;    // set baud rate
#if USE_IOSSIOSPEED == false
                    if (ipc.parameter0 == 300)
                    {
                        // handle special case 300 baud which is aliased to 288000
                        ipc.parameter0 = 288000;
                    }
#endif
                    cc_buffer[2] = ipc.parameter0 & 0xff;
                    cc_buffer[3] = (ipc.parameter0 >> 8) & 0xff;
                    cc_buffer[4] = (ipc.parameter0 >> 16) & 0xff;
                    cc_buffer[5] = (ipc.parameter0 >> 24) & 0xff;
                    if (send_command (fd, cc_buffer, 6, sizeof(cc_buffer)) < 0)
                    {
                        perror("send command:");
                    }
#if USE_IOSSIOSPEED == false
                    if (tcsetattr (fd, TCSAFLUSH, &options) < 0)
                    {
                        fprintf (stdout, "Failed to set new baudrate\n");
                    }
#else
                    speed_t speed = ipc.parameter0;
                    if (ioctl(fd, IOSSIOSPEED, &speed) == -1 )
                    {
                       fprintf (stdout, "Failed to set new baudrate\n");
                    }
#endif
                    break;
                    
                case SET_SLOT:
                    cc_buffer[0] = 0xcc;
                    cc_buffer[1] = 0x81;    // set/get slots
                    cc_buffer[2] = ipc.parameter0 & 0xff;
                    if (send_command (fd, cc_buffer, 3, sizeof(cc_buffer)) < 0)
                    {
                        perror("send command:");
                    }
                    break;
 
                case SET_BW:
                    cc_buffer[0] = 0xcc;
                    cc_buffer[1] = 0x82;    // set bandwidth
                    cc_buffer[2] = ipc.parameter0 & 0xff;
                    if (send_command (fd, cc_buffer, 3, sizeof(cc_buffer)) < 0)
                    {
                        perror("send command:");
                    }
                    break;
                    
                case SET_REGION:
                    cc_buffer[0] = 0xcc;
                    cc_buffer[1] = 0x60;    // get/set region
                    cc_buffer[2] = ipc.parameter0 & 0xff;
                    if (send_command (fd, cc_buffer, 3, sizeof(cc_buffer)) < 0)
                    {
                        perror("send command:");
                    }
                    break;

                case SET_PROTOCOL:
                    cc_buffer[0] = 0xcc;
                    cc_buffer[1] = 0x80;    // set protocol
                    cc_buffer[2] = ipc.parameter0 & 3;
                    if (send_command (fd, cc_buffer, 3, sizeof(cc_buffer)) < 0)
                    {
                        perror("send command:");
                    }
                    set_mode (ipc.parameter0);
                    break;
                    
                case GET_TRAFFIC_STATS:
                case GET_RED_TRAFFIC_STATS:
                    cc_buffer[0] = 0xcc;
                    cc_buffer[1] = (ipc.cmd == GET_TRAFFIC_STATS) ? 0x6A : 0x6B;    // get stats
                    if (send_command (fd, cc_buffer, 2, sizeof(cc_buffer)) < 0)
                    {
                        perror("send command:");
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
            
            if (!send_one_time)
            {
                frame->header.type = LOW_LATENCY;
                frame->header.dest = dest_address;
                memset (&frame->payload, 0x55, frame_size - sizeof (frame_hdr_t));
                count = frame_size;
                frame->header.len = count;
            }
            
            // compute frame's CRC
            uint16_t crc = calcCRC(0, send_buffer, count);
            send_buffer[count] = (uint8_t) crc;
            send_buffer[count + 1] = (uint8_t) (crc >> 8) & 0xFF;
            
            count += 2;
            if (send_frame (fd, send_buffer, count, get_mode (), slot) < 0)
            {
                perror("serial port write");
                break;
            }
            
            if (send_one_time)
            {
                send_one_time = false;
            }
        }
        
        sts.tv_nsec = interval * 1000000;
        sts.tv_sec = 0;
        nanosleep (&sts, NULL);
    }
    
    pthread_exit (NULL);
}


ssize_t
send_frame (int fd, uint8_t *frame, int count, op_mode_t type, uint8_t slot)
{
    uint8_t send_buffer[MAX_FRAME_LEN];
    uint8_t *p = send_buffer;
    int i;
    ssize_t result;
    
    if (type > WHITE_RADIO)
    {
        p += 3; // leave some space (3 bytes) for the header
    }
    
    if (type < ROTFUNK_PLUS)
    {
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
    }
    else
    {
        // rotfunk
        memcpy (p, frame, count);
        count += sizeof (red_header_t); // header length
    }
    
    if (type > WHITE_RADIO)
    {
        // special handling of frames with header
        send_buffer[0] = (type == WHITE_RADIO_PLUS ? SOH_CHAR : (count - sizeof (red_header_t)));
        send_buffer[1] = 1;         // frame type (TBD)
        send_buffer[2] = slot;      // slot number
    }
    
#if SERIAL_DEBUG == 1
    fprintf (stdout, "sent %d bytes, ", count);
    for (int i = 0; i < count; i++)
    {
        fprintf (stdout, "%02x ", send_buffer[i]);
    }
    fprintf (stdout, "\n");
#endif
    result = write (fd, send_buffer, count);
    tcdrain (fd);           // wait for the transmission to finish
    
    return result;
}

ssize_t
send_command (int fd, uint8_t *command, size_t cmd_len, size_t max_len)
{
    ssize_t result;
    struct timespec sts;
    
    sts.tv_nsec = 500000;   // 500 us
    sts.tv_sec = 0;
    
    cmd_data(fd, false);    // cmd active
    result = write (fd, command, cmd_len);
    tcdrain (fd);           // wait for the transmission to finish
    nanosleep (&sts, NULL); // add some more delay
    cmd_data(fd, true);     // cmd inactive
    
    return result;
}
