//
//  utils.c
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
//  Created on 27/07/2017 (lnp)
//

#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <termios.h>
#include <sys/ioctl.h>

#include "utils.h"


bool
dump_frames (get_set_cmd_t operation, bool state)
{
    static bool dump_frames_state = false;
    
    operation == SET_PARAMETER ? dump_frames_state = state : 0;
    return dump_frames_state;
}


uint8_t
own_address (get_set_cmd_t operation, uint8_t address)
{
    static uint8_t my_address = 10;
    
    operation == SET_PARAMETER ? my_address = address : 0;
    return my_address;
}


//  @brief Computes the CRC-16 of the input string pointed to by buff.
//  @param crc: initial crc value
//  @param buff: a pointer to the buffer containing the data to be CRC-ized
//  @param len: buffer length.
//  @retval computed CRC-16 value.

uint16_t
calcCRC (uint16_t crc, uint8_t *buff, int len)
{
    int i, j;
    
    for (i = 0; i < len; i++) /* calculate CRC */
    {
        crc ^= (uint16_t) *buff++ << 8;
        for (j = 0; j < 8; j++)
            if (crc & 0x8000)
                crc = crc << 1 ^ 0x1021;
            else
                crc <<= 1;
    }
    return crc;
}

bool
cmd_data (int fd, bool state)
{
    bool result = true;
    
    if (state)
    {
        // Assert Data Terminal Ready (DTR)
        if (ioctl(fd, TIOCCDTR) == -1)
        {
            result = false;
        }
    }
    else
    {
        // Clear Data Terminal Ready (DTR)
        if (ioctl(fd, TIOCSDTR) == -1)
        {
            result = false;
        }
    }
    return result;
}
