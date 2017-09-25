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
#include "statistics.h"
#include "cli.h"


#define MAX_PARAMS 16
#define OK 1
#define ERROR 0
#define QUIT -1

typedef struct
{
    char *name;
    int (*func) (int, char**);
    char *help_string;
} cmds_t;


extern ipc_t ipc;
extern pthread_mutex_t send_serial_mutex;

extern void
quit (void);

static int
dump_rec (int argc, char *argv[]);

static int
stats_cmd (int argc, char *argv[]);

static int
send_cmd (int argc, char *argv[]);

static int
set_cmd (int argc, char *argv[]);

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
    { "set", set_cmd, "Set various parameters" },
    { "stat", stats_cmd, "Show/clear statistics" },
    { "quit", quit_cmd, "Quit program" },
    { "exit", quit_cmd, "Exit program" },
    { "help", help, "Show this help; for individual command help, use <command> -h" },
    { NULL, NULL, NULL }
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
    if (pcmd->name == NULL && size > 1)
    {
        fprintf (stdout, "Unknown command\n");
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
        pthread_mutex_lock (&send_serial_mutex);
        if (!strcasecmp (argv[0], "ll"))   // send low latency frames
        {
            ipc.address = atoi (argv[1]);
            ipc.cmd = !strcasecmp (argv[2], "on") ? SEND_LOW_LATENCY_FRAMES : STOP_LOW_LATENCY_FRAMES;
        }
        else
        {
            fprintf (stdout, "Invalid parameter\n");
        }
        pthread_mutex_unlock (&send_serial_mutex);
    }
    else
    {
        fprintf (stdout, "Usage:\tsend ll dest_addr { on | off }\n");
    }
    
    return OK;
}

// Command to set certain parameters.
static int
set_cmd (int argc, char *argv[])
{
    if (argc > 1)
    {
        pthread_mutex_lock (&send_serial_mutex);
        if (!strcasecmp (argv[0], "zch"))
        {
            int ch = atoi (argv[1]);
            if (ch < 11 || ch > 26)
            {
                fprintf (stdout, "Invalid parameter (only channels 11 to 26 are accepted)\n");
            }
            else
            {
                ipc.cmd = SET_CHANNEL;
                ipc.parameter = atoi (argv[1]) - 11;
            }
        }
        else  if (!strcasecmp (argv[0], "master"))
        {
            ipc.cmd = SET_MASTER;
            if (!strcasecmp (argv[1], "on"))
            {
                ipc.parameter = 0;
            }
            else if (!strcasecmp (argv[1], "off"))
            {
                ipc.parameter = 1;
            }
            else
            {
                ipc.cmd = NOP;
                fprintf (stdout, "Invalid parameter (on or off accepted)\n");
            }
        }
        else if (!strcasecmp (argv[0], "rate"))
        {
            ipc.cmd = SET_RATE;
            
            if (!strcasecmp (argv[1], "100K"))
            {
                ipc.parameter = MOD_OQPSK_100K;
            }
            else if (!strcasecmp (argv[1], "250K"))
            {
                ipc.parameter = MOD_OQPSK_250K;
            }
            else if (!strcasecmp (argv[1], "1M"))
            {
                ipc.parameter = MOD_GFSK_1M;
            }
            else if (!strcasecmp (argv[1], "2M"))
            {
                ipc.parameter = MOD_GFSK_2M;
            }
            else
            {
                ipc.cmd = NOP;
                fprintf (stdout, "Invalid data rate; valid values are 100K, 250K, 1M and 2M\n");
            }
        }
        else if (!strcasecmp (argv[0], "hop"))
        {
            if (argc == 3)
            {
                ipc.cmd = SET_HOP_PARAMS;
                ipc.parameter = atoi (argv[1]);
                ipc.parameter1 = atoi (argv[2]);
            }
            else
            {
                fprintf (stdout, "Insuficient arguments");
            }
        }
        else if (!strcasecmp (argv[0], "baud"))
        {
            if (argc == 2)
            {
                ipc.cmd = SET_BAUD;
                ipc.parameter = atoi (argv[1]);
            }
            else
            {
                fprintf (stdout, "Insuficient arguments");
            }
        }
        else
        {
            fprintf (stdout, "Invalid parameter\n");
        }
        pthread_mutex_unlock (&send_serial_mutex);
    }
    else
    {
        fprintf (stdout, "Usage:\tset { zch | master | rate | hop | baud }\n");
    }
    
    return OK;
}


// Statistics related commands.
static int
stats_cmd (int argc, char *argv[])
{
    if (argc > 0)
    {
        if (!strcasecmp (argv[0], "clear"))
        {
            clear_stats();
        }
    }
    else
    {
        for (int i = 0; i < 255; i++)
        {
            if (g_stats[i].frames_recvd)
            {
                fprintf (stdout, "Node %d: avg rssi: %d dBm, total frames %d, lost frames %d (%.2f%%)\n"
                         "Average/min/max latency (ms): %.2f/%.2f/%.2f\n",
                         i, g_stats[i].rssi_sum  / g_stats[i].rssi_samples * -1,
                         g_stats[i].frames_recvd + g_stats[i].frames_lost, g_stats[i].frames_lost,
                         g_stats[i].frames_lost * 100.0 / (g_stats[i].frames_recvd + g_stats[i].frames_lost),
                         (g_stats[i].latency_sum / (g_stats[i].latency_samples) / 1000.0),
                         g_stats[i].latency_min / 1000.0, g_stats[i].latency_max / 1000.0);
                fprintf (stdout, "Frames with CRC errors %u (%.2f%% from total frames received)\n",
                         g_crc_error_count, g_crc_error_count * 100.0 / g_total_recvd_frames);
            }
        }
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

