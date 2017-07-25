//
//  main.c
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
//  Created on 17/05/2017 (lnp)
//
//

#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <termios.h>
#include <fcntl.h>
#include <sys/time.h>

#include "frame-parser.h"

#define VERSION_MAJOR 0
#define VERSION_MINOR 3

#define SERIAL_DEBUG 0

static void quit (void);
static int handle_serial_line (int fd);
static int counter = 0;


// Main entry point.

int main (int argc, char * argv[])
{
    int ch;
    char *port = NULL;
    int fd;
    struct termios options;
    int baudRate = 115200;
    
    signal (SIGINT, (void *) quit);	/* trap ctrl-c calls here */
    
    while ((ch = getopt (argc, argv, "hvD:b:")) != -1)
    {
        switch (ch)
        {
            case 'D':
                port = optarg;
                break;
                
            case 'b':
                baudRate = atoi (optarg);
                break;
                
            case 'v':
                fprintf (stdout, "Serial Test, version %d.%d, compiled on %s, %s\n",
                        VERSION_MAJOR, VERSION_MINOR, __DATE__, __TIME__);
                exit (EXIT_SUCCESS);
                break;
                
            case 'h':
            default:
                fprintf (stdout, "Usage: sertest -D <tty>\n");
                exit (EXIT_SUCCESS);
                break;
        }
    }
    
    if (port == NULL)
    {
        fprintf (stdout, "Missing device (-D option)\n");
        exit (EXIT_FAILURE);
    }
    
    fd = open(port, O_RDWR | O_NOCTTY | O_NDELAY);
    if (fd != -1)
    {
        fcntl (fd, F_SETFL, 0);
        tcgetattr (fd, &options);
        cfsetispeed (&options, baudRate);
        cfsetospeed (&options, baudRate);
        options.c_cflag |= (CLOCAL | CREAD);
        options.c_oflag &= ~OPOST;
        options.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
        
        options.c_cc[VTIME] = 0;
        options.c_cc[VMIN] = 1;
        if (tcsetattr (fd, TCSANOW, &options) < 0)
        {
            fprintf (stdout, "Failed to set new parameters on the serial port\n");
            exit (EXIT_FAILURE);
        }
    }
    else
    {
        fprintf (stdout, "Failed to open serial device %s\n", port);
        exit (EXIT_FAILURE);
    }

    // prepare for multiple input sources
    int res;
    fd_set readfs;
    struct timeval timeout;
    int maxfd = fd + 1;
    uint8_t send_buffer[220];
    
    send_buffer[0] = 0xf0;
    send_buffer[1] = 0;
    
    do
    {
        FD_SET (fd, &readfs);
        FD_SET (fileno (stdin), &readfs);
        timeout.tv_usec = 5000;     // 5 ms
        timeout.tv_sec = 0;

        res = select (maxfd, &readfs, NULL, NULL, &timeout);
        if (res == 0)
        {
            // timeout, send a frame
            send_buffer[1] += 1;
            if (send_buffer[1] == 0xf0)
                send_buffer[1] = 0;
            send_buffer[109] = 0xf1;
            write (fd, send_buffer, 110);
        }
        else if (FD_ISSET(fd, &readfs))
        {
            // got a frame from serial
            res = handle_serial_line (fd);
        }
        else if (FD_ISSET(fileno (stdin), &readfs))
        {
            // got a char from the user, just echo it back for now
            putchar (getchar ());
            res = 0;
        }
    } while (res == 0);
    
    return EXIT_SUCCESS;
}

// @brief   Handle serial port input frames.
// @param   fd: serial file descriptor
// @retval  0 if successful, 1 if serial port error.

static int handle_serial_line (int fd)
{
    ssize_t res;
    static uint8_t buff[400];
    static ssize_t offset = 0;
    
    if ((res = read (fd, buff + offset, sizeof (buff) - offset)) > 0)
    {
#if SERIAL_DEBUG == 1
        fprintf (stdout, "\nread %ld bytes, offset %ld\n", res, offset);
        for (int i = 0; i < res; i++)
        {
            fprintf (stdout, "%02x ", (buff + offset)[i]);
        }
        fprintf (stdout, "\n");
#endif
        uint8_t *begin, *end;
        int result;
         
        begin = buff;
        end = buff + res + offset - 1;
        
        while ((result = parse_f0_f1_frames (&begin, &end)) == FRAME_OK)
        {
            print_f0_f1_frames (begin, end - begin + 1);
            counter++;
            
            if (end < buff + res + offset) // whole buffer done?
            {
                // no, we might have more frames here, or at least a truncated one
                begin = end + 1;
                end = buff + res + offset - 1;
            }
            else
            {
                offset = 0;
                break;
            }
        }
        
        if (result == FRAME_TRUNCATED)
        {
            // that means we have detected a frame begin, but not an end
#if SERIAL_DEBUG == 1
            fprintf (stdout, "-- truncated (len %ld)\n", end - begin + 1);
#endif
            memmove (buff, begin, end - begin + 1);
            offset = end - begin + 1;
        }
        else
        {
            offset = 0;
        }
        return 0;
    }
    else
    {
        perror("serial\n");
        return 1;
    }
}


static void quit (void)
{
    fprintf (stdout, "Total frames received: %d\n", counter);
    exit (1);
}
