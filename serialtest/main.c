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
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <termios.h>
#include <fcntl.h>
#include <sys/time.h>

#include "frame-parser.h"

#define VERSION_MAJOR 0
#define VERSION_MINOR 4

#define SERIAL_DEBUG 0

typedef enum
{
    GET_DUMP_STATE,
    SET_DUMP_STATE
} dump_request_t;

static int counter = 0;

static void
quit (void);

static int
handle_serial_line (int fd, bool print);

static bool
dump_frames (dump_request_t operation, bool state);

static int
parse_line (char *line, size_t size);

static int
getver (int argc, char *argv[]);

static int
dump_rec (int argc, char *argv[]);

static int
quit_cmd (int argc, char *argv[]);

static int
help (int argc, char *argv[]);



// Main entry point.

int main (int argc, char * argv[])
{
    int ch;
    char *port = NULL;
    int fd;
    struct termios options;
    int baudRate = 115200;
    bool print = false;
    
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
                getver (0, NULL);
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
    size_t res;
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
            res = handle_serial_line (fd, dump_frames (GET_DUMP_STATE, false));
        }
        else if (FD_ISSET(fileno (stdin), &readfs))
        {
            // got a line from the user
            char *line = NULL;
            size_t size = 0;
            
            if ((res = getline (&line, &size, stdin)) > 0)
            {
                if (parse_line (line, res) < 0)
                {
                    // quit command
                    res = 1;
                }
                else
                {
                    res = 0;
                }
                free (line);
                fprintf (stdout, "> ");
                fflush (stdout);
            }
        }
    } while (res == 0);
    
    return EXIT_SUCCESS;
}

// @brief   Handle serial port input frames.
// @param   fd: serial file descriptor
// @retval  0 if successful, 1 if serial port error.

static int handle_serial_line (int fd, bool print)
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
            if (print)
            {
                print_f0_f1_frames (begin, end - begin + 1);
            }
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
            // prepare to read more data from the serial port
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

static bool
dump_frames (dump_request_t operation, bool state)
{
    static bool dump_frames_state = false;
    
    operation == SET_DUMP_STATE ? dump_frames_state = state : 0;
    return dump_frames_state;
}


#define MAX_PARAMS 16
#define OK 1
#define ERROR 0
#define QUIT -1

typedef struct
{
    char *name;
    int (*func) ();
    char *help_string;
} cmds_t;

//===============================================================================
// Commands table
//
const cmds_t rxcmds[] =
//	CMD,	function,	  help string
{
    { "ver", getver, "Returns current version" },
    { "dump", dump_rec, "Switch on/off dumping of received frames" },
    { "quit", quit_cmd, "Quit program" },
    { "exit", quit_cmd, "Exit program" },
    { "help", help, "Show this help; for individual command help, use <command> -h" },
    { NULL, NULL, 0 }
} ;

static int parse_line (char *line, size_t size)
{
    char *pbuff;
    int result;
    const cmds_t *pcmd;
    char *argv[MAX_PARAMS]; /* pointers on parameters */
    int argc; /* parameter counter */

    pbuff = line;
    *(pbuff + size - 1) = '\0'; // replace nl with null terminator
    
    while (*pbuff != ' ' && *pbuff != '\0')
        pbuff++;
    *pbuff++ = '\0'; // insert terminator
    
    result = ERROR; // init result just in case of failure
    
    /* iterate all commands in the table, one by one */
    for (pcmd = rxcmds; pcmd->name; pcmd++) // lookup in the commands table
    {
        if (!strcasecmp (line, pcmd->name)) // valid command, parse parameters, if any
        {
            for (argc = 0; argc < MAX_PARAMS; argc++)
            {
                if (!*pbuff)
                    break; 			// end of line reached
                
                if (*pbuff == '\"')
                {
                    pbuff++; 		// suck up the "
                    argv[argc] = pbuff;
                    while (*pbuff != '\"' && *pbuff != '\0')
                        pbuff++;
                    *pbuff++ = '\0';
                    if (*pbuff)		// if not end of line...
                        pbuff++;	// suck up the trailing space too
                }
                else
                {
                    argv[argc] = pbuff;
                    while (*pbuff != ' ' && *pbuff != '\0')
                        pbuff++;
                    *pbuff++ = '\0';
                }
            }
            result = (*pcmd->func) (argc, &argv[0]); // execute command
            break;
        }
    }
    return result;
}

//#pragma GCC diagnostic ignored "-Wunused-parameter"

static int
getver (int argc, char *argv[])
{
    fprintf (stdout, "Serial Test Utility, version %d.%d, compiled on %s, %s\n",
             VERSION_MAJOR, VERSION_MINOR, __DATE__, __TIME__);
    return OK;
}

static int
dump_rec (int argc, char *argv[])
{
    if (argc == 0)
    {
        fprintf (stdout, "Received frames are %sdumped to the console\n",
                 dump_frames (GET_DUMP_STATE, false) ? "" : "not ");
    }
    else if (!strcasecmp (argv[0], "-h"))
    {
        fprintf (stdout, "Usage: dump { on | off }\n");
    }
    else if (!strcasecmp (argv[0], "on"))
    {
        dump_frames (SET_DUMP_STATE, true);
    }
    else if (!strcasecmp (argv[0], "off"))
    {
        dump_frames (SET_DUMP_STATE, false);
    }
    return OK;
}

static int
quit_cmd (int argc, char *argv[])
{
    quit ();    // no return!
    return OK;
}

static int
help (int argc, char *argv[])
{
    const cmds_t *pcmd;
    
    fprintf (stdout, "Following commands are available:\r\n");
    for (pcmd = rxcmds; pcmd->name; pcmd++)
        fprintf (stdout, "  %-10s%s\r\n", pcmd->name, pcmd->help_string);
    return OK;
}


//#pragma GCC diagnostic pop


static void quit (void)
{
    fprintf (stdout, "Total frames received: %d\n", counter);
    exit (1);
}
