//
//  cli.c
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

#include "utils.h"
#include "frame-parser.h"
#include "cli.h"


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


extern ipc_t ipc;
extern pthread_mutex_t send_serial_mutex;

extern void
quit (void);

static int
dump_rec (int argc, char *argv[]);

static int
send_cmd (int argc, char *argv[]);

static int
quit_cmd (int argc, char *argv[]);

static int
help (int argc, char *argv[]);



//===============================================================================
// Commands table.
//
const cmds_t rxcmds[] =
//	CMD,	function,	  help string
{
    { "ver", getver, "Returns current version" },
    { "dump", dump_rec, "Switch on/off dumping of received frames" },
    { "send", send_cmd, "Send various types of frames over the serial port" },
    { "quit", quit_cmd, "Quit program" },
    { "exit", quit_cmd, "Exit program" },
    { "help", help, "Show this help; for individual command help, use <command> -h" },
    { NULL, NULL, 0 }
} ;

// @brief Parse a command line from the user.
// @param line: a lf + '\0' terminated line.
// @param size: length of the line, including the null terminator.
// @retval OK if a valid command was found and executed properly, ERROR otherwise
//         If the command was QUIT or EXIT, then QUIT will be  returned (-1).

int
parse_line (char *line, size_t size)
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

// Get version command.
int
getver (int argc, char *argv[])
{
    fprintf (stdout, "Serial Test Utility, version %d.%d, compiled on %s, %s\n",
             VERSION_MAJOR, VERSION_MINOR, __DATE__, __TIME__);
    return OK;
}

// Enable/disable dumping the frame content to the console.
static int
dump_rec (int argc, char *argv[])
{
    if (argc == 0)
    {
        fprintf (stdout, "Received frames are %sdumped to the console\n",
                 dump_frames (GET_PARAMETER, false) ? "" : "not ");
    }
    else if (!strcasecmp (argv[0], "-h"))
    {
        fprintf (stdout, "Usage:\tdump { on | off }\n");
    }
    else if (!strcasecmp (argv[0], "on"))
    {
        dump_frames (SET_PARAMETER, true);
    }
    else if (!strcasecmp (argv[0], "off"))
    {
        dump_frames (SET_PARAMETER, false);
    }
    return OK;
}

// Command to send various types of frames over the serial port.
static int
send_cmd (int argc, char *argv[])
{
    if (argc > 2)
    {
        if (!strcasecmp (argv[0], "ll"))   // send low latency frames
        {
            pthread_mutex_lock (&send_serial_mutex);
            ipc.address = atoi (argv[1]);
            ipc.cmd = strcasecmp (argv[0], "on") ? SEND_LOW_LATENCY_FRAMES : STOP_LOW_LATENCY_FRAMES;
            pthread_mutex_unlock (&send_serial_mutex);
        }
    }
    else
    {
        fprintf (stdout, "Usage:\tsend ll dest_addr { on | off }\n");
    }
    
    return OK;
}

// Quit the program.
static int
quit_cmd (int argc, char *argv[])
{
    quit ();    // no return!
    return OK;
}

// Help command.
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

