//
//  cli.c
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
//  Created on 27/07/2017 (lnp)
//

#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <sys/termios.h>

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
interval_cmd (int argc, char *argv[]);

static int
len_cmd (int argc, char *argv[]);

static int
ser_cfg (int argc, char *argv[]);

static int
set_cmd (int argc, char *argv[]);

static int
quit_cmd (int argc, char *argv[]);

static int
help (int argc, char *argv[]);

static int
spy_cmd (int argc, char *argv[]);


//===============================================================================
// Commands table.
//
const cmds_t rxcmds[] =
//	CMD,	function,	  help string
{
    { "ver", getver, "Returns current version" },
    { "dump", dump_rec, "Switch on/off dumping of received frames" },
    { "send", send_cmd, "Send various types of frames over the serial port" },
    { "interval", interval_cmd, "Set the interval between low latency frames" },
    { "len", len_cmd, "Set the length of the low latency frames" },
    { "set", set_cmd, "Set various parameters" },
    { "stat", stats_cmd, "Show/clear statistics" },
    { "spy", spy_cmd, "Spy on the current radio channel" },
    { "sercfg", ser_cfg, "Configure the serial port" },
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
    static char tmp_buff[256];
    char *p;
    
    if (argc > 0)
    {
        if (!strcasecmp (argv[0], "off"))
        {
            // stop sending low latency frames
            pthread_mutex_lock (&send_serial_mutex);
            ipc.cmd = STOP_LOW_LATENCY_FRAMES;
            pthread_mutex_unlock (&send_serial_mutex);
        }
        else if (!strcasecmp (argv[0], "ll") && (argc > 1))
        {
            // send low latency frames
            pthread_mutex_lock (&send_serial_mutex);
            ipc.address = atoi (argv[1]);
            ipc.cmd =  SEND_LOW_LATENCY_FRAMES;
            pthread_mutex_unlock (&send_serial_mutex);
        }
        else if (!strcasecmp (argv[0], "llh") && (argc > 2))
        {
            // send ll frames with header
            pthread_mutex_lock (&send_serial_mutex);
            ipc.address = atoi (argv[1]);
            ipc.cmd = SEND_LOW_LATENCY_FRAMES_WITH_HEADER;
            ipc.parameter0 = atoi (argv[2]);
            pthread_mutex_unlock (&send_serial_mutex);
        }
        else if (!strcasecmp (argv[0], "plain") && (argc > 1))
        {
            // send a plain frame (a text)
            int cnt = 0;
            for (p = argv[1]; *p; p++)
            {
                if (cnt > sizeof (tmp_buff))
                {
                    break;
                }
                if (*p != '\\')
                {
                    tmp_buff[cnt++] = *p;
                }
                else
                {
                    p++;
                    switch (*p)
                    {
                        case 'r':
                            tmp_buff[cnt++] = 0xd;
                            break;
                            
                        case 'n':
                            tmp_buff[cnt++] = 0xa;
                            break;
                            
                        case '\\':
                            tmp_buff[cnt++] = '\\';
                            break;

                        default:
                            tmp_buff[cnt++] = *p;
                            break;
                    }
                }
            }
            pthread_mutex_lock (&send_serial_mutex);
            ipc.text = tmp_buff;
            ipc.parameter0 = cnt;
            ipc.cmd =  SEND_PLAIN_FRAME;
            pthread_mutex_unlock (&send_serial_mutex);
        }
        else
        {
            fprintf (stdout, "Invalid parameter\n");
        }
    }
    else
    {
        fprintf (stdout, "Usage:\tsend ll dest_addr\n"
                 "\tsend llh dest_addr slot_number\n"
                 "\tsend off\n"
                 "\tsend plain\n"
                 "\twhere dest_addr 0...255, slot_number 0...31\n");
    }
    
    return OK;
}

// Command to request protocol statistics.
static int
spy_cmd (int argc, char *argv[])
{
    pthread_mutex_lock (&send_serial_mutex);

    if (argc > 0 && !strcasecmp (argv[0], "red"))
    {
        ipc.cmd = GET_RED_TRAFFIC_STATS;
    }
    else
    {
        ipc.cmd = GET_TRAFFIC_STATS;
    }
    pthread_mutex_unlock (&send_serial_mutex);

    return OK;
}

// Set the interval between low latency frames.
static int
interval_cmd (int argc, char *argv[])
{
    if (argc > 0)
    {
        int value = atoi (argv[0]);
        if (value > 0 && value <= 100)
        {
            pthread_mutex_lock (&send_serial_mutex);
            ipc.cmd = INTERVAL;
            ipc.parameter0 = value;
            pthread_mutex_unlock (&send_serial_mutex);
        }
        else
        {
            fprintf (stdout, "Invalid parameter, should be between 1 and 100 (ms)\n");
        }
    }
    else
    {
        fprintf (stdout, "Usage:\tinterval <nn>\n");
    }
    
    return OK;
}

// Set the length of the low latency frames.
static int
len_cmd (int argc, char *argv[])
{
    if (argc > 0)
    {
        int value = atoi (argv[0]);
        if (value > 0 && value <= 120)  // max 120 bytes payoad
        {
            pthread_mutex_lock (&send_serial_mutex);
            ipc.cmd = LENGTH;
            ipc.parameter0 = value;
            pthread_mutex_unlock (&send_serial_mutex);
        }
        else
        {
            fprintf (stdout, "Invalid parameter, should be between 1 and 120 (bytes)\n");
        }
    }
    else
    {
        fprintf (stdout, "Usage:\tlen <nn>\n");
    }
    
    return OK;
}

// Configure serial port parameters.
static int
ser_cfg (int argc, char *argv[])
{
    struct termios options;
    int fd = get_serial_fd();
    
    if (argc > 0)
    {
        tcgetattr (fd, &options);
        
        // canonical mode
        if (!strcasecmp (argv[0], "icanon"))
        {
            options.c_lflag |= ICANON;
        }
        else if (!strcasecmp (argv[0], "-icanon"))
        {
            options.c_lflag &= ~ICANON;
        }
        
        // echo
        else if (!strcasecmp (argv[0], "echo"))
        {
            options.c_lflag |= ECHO;
        }
        else if (!strcasecmp (argv[0], "-echo"))
        {
            options.c_lflag &= ~ECHO;
        }

        // map NL to CR
        else if (!strcasecmp (argv[0], "inlcr"))
        {
            options.c_iflag |= INLCR;
        }
        else if (!strcasecmp (argv[0], "-inlcr"))
        {
            options.c_iflag &= ~INLCR;
        }

        // map CR to NL
        else if (!strcasecmp (argv[0], "icrnl"))
        {
            options.c_iflag |= ICRNL;
        }
        else if (!strcasecmp (argv[0], "-icrnl"))
        {
            options.c_iflag &= ~ICRNL;
        }

        // ignore CR
        else if (!strcasecmp (argv[0], "igncr"))
        {
            options.c_iflag |= IGNCR;
        }
        else if (!strcasecmp (argv[0], "-igncr"))
        {
            options.c_iflag &= ~IGNCR;
        }

        // postprocess output
        else if (!strcasecmp (argv[0], "opost"))
        {
            options.c_oflag |= OPOST;
        }
        else if (!strcasecmp (argv[0], "-opost"))
        {
            options.c_oflag &= ~OPOST;
        }

        // map nl to cr/nl
        else if (!strcasecmp (argv[0], "onlcr"))
        {
            options.c_oflag |= ONLCR;
        }
        else if (!strcasecmp (argv[0], "-onlcr"))
        {
            options.c_oflag &= ~ONLCR;
        }

        // map cr to nl
        else if (!strcasecmp (argv[0], "ocrnl"))
        {
            options.c_oflag |= OCRNL;
        }
        else if (!strcasecmp (argv[0], "-ocrnl"))
        {
            options.c_oflag &= ~OCRNL;
        }

        // nl performs cr function
        else if (!strcasecmp (argv[0], "onlret"))
        {
            options.c_oflag |= ONLRET;
        }
        else if (!strcasecmp (argv[0], "-onlret"))
        {
            options.c_oflag &= ~ONLRET;
        }

        // apply change
        if (tcsetattr (fd, TCSANOW, &options) < 0)
        {
            fprintf (stdout, "Failed to set the option on the serial port\n");
        }
    }
    else
    {
        fprintf (stdout, "Not enough parameters\n");
    }
    return OK;
}

// Command to set certain parameters.
static int
set_cmd (int argc, char *argv[])
{
    struct timespec sts;
    int region = 0;
    
    if (argc > 1)
    {
        int rounds = 0;
        do
        {
            pthread_mutex_lock (&send_serial_mutex);
            if (!strcasecmp (argv[0], "zch"))
            {
                if (argc == 3)
                {
                    rounds = atoi (argv[2]);
                    argc = 10;  // kludge!
                }

                int ch = atoi (argv[1]);
                if (ch < 11 || ch > 26)
                {
                    fprintf (stdout, "Invalid parameter (only channels 11 to 26 are accepted)\n");
                }
                else
                {
                    ipc.cmd = SET_CHANNEL;
                    ipc.parameter0 = ch - 11;
                }
                sts.tv_nsec = 20000000;
                sts.tv_sec = 0;
                nanosleep (&sts, NULL);
            }
            else  if (!strcasecmp (argv[0], "master"))
            {
                ipc.cmd = SET_MASTER;
                if (!strcasecmp (argv[1], "on"))
                {
                    ipc.parameter0 = 0;
                }
                else if (!strcasecmp (argv[1], "off"))
                {
                    ipc.parameter0 = 1;
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
                
                if (!strcasecmp (argv[1], "250K"))
                {
                    ipc.parameter0 = MOD_OQPSK_250K;
                }
                else if (!strcasecmp (argv[1], "1M"))
                {
                    ipc.parameter0 = MOD_GFSK_1M;
                }
                else if (!strcasecmp (argv[1], "2M"))
                {
                    ipc.parameter0 = MOD_GFSK_2M;
                }
                else
                {
                    ipc.cmd = NOP;
                    fprintf (stdout, "Invalid data rate; valid values are 250K, 1M and 2M\n");
                }
            }
            else  if (!strcasecmp (argv[0], "region"))
            {
                if (argc == 3)
                {
                    rounds = atoi (argv[2]);
                    argc = 10;  // kludge!
                }
                if (argc == 10)
                {
                    if (++region > 19)
                    {
                        region = 0;
                    }
                }
                else
                {
                    region = atoi (argv[1]);
                }
                if (region < 20)
                {
                    ipc.cmd = SET_REGION;
                    ipc.parameter0 = region;
                }
                else
                {
                    fprintf (stdout, "Invalid region\n");
                }
                sts.tv_nsec = 100000000;
                sts.tv_sec = 0;
                nanosleep (&sts, NULL);
            }
            else if (!strcasecmp (argv[0], "hop"))
            {
                if (argc == 4)
                {
                    ipc.cmd = SET_HOP_PARAMS;
                    ipc.parameter0 = atoi (argv[1]);
                    ipc.parameter1 = atoi (argv[2]);
                    ipc.parameter2 = atoi (argv[3]);
                }
                else
                {
                    fprintf (stdout, "Insuficient arguments\n");
                }
            }
            else if (!strcasecmp (argv[0], "stretch"))
            {
                if (argc == 2)
                {
                    ipc.cmd = SET_HOP_STRETCHING;
                    ipc.parameter0 = atoi (argv[1]);
                }
                else
                {
                    fprintf (stdout, "Hop stretch (in us; min 500, max hop_high - 500)\n");
                }
            }
            else if (!strcasecmp (argv[0], "baud"))
            {
                if (argc == 2)
                {
                    ipc.cmd = SET_BAUD;
                    ipc.parameter0 = atoi (argv[1]);
                }
                else
                {
                    fprintf (stdout, "Insuficient arguments\n");
                }
            }
            else if (!strcasecmp (argv[0], "proto"))
            {
                int param = atoi (argv[1]);
                if (param < 4)
                {
                    ipc.parameter0 = param;
                    ipc.cmd = SET_PROTOCOL;
                }
                else
                {
                    fprintf (stdout, "Invalid parameter, must be 0 - white, 1 - white+, 2 - red+, 3 - plain\n");
                }
            }
            else if (!strcasecmp (argv[0], "slot"))
            {
                int slot, i;
                ipc.parameter0 = 0;
                for (i = 1; i < argc; i++)
                {
                    slot = atoi (argv[i]);
                    if (slot > 4)
                    {
                        fprintf (stdout, "<slot#> can be from 0 to 4\n");
                        break;
                    }
                    ipc.parameter0 |= (1 << slot);
                }
                if (i == argc)
                {
                    ipc.cmd = SET_SLOT;
                }
            }
            else if (!strcasecmp (argv[0], "bw"))
            {
                if (argc == 3)
                {
                    int bw = 0;
                    if (!strcasecmp (argv[2], "250K"))
                    {
                        bw = 1;
                    }
                    else if (!strcasecmp (argv[2], "1M"))
                    {
                        bw = 2;
                    }
                    else if (!strcasecmp (argv[2], "2M"))
                    {
                        bw = 3;
                    }
                    else
                    {
                        fprintf (stdout, "bw can be one of: 250K, 1M, 2M");
                    }
                    int which = atoi (argv[1]);
                    if (which > 4)
                    {
                        fprintf (stdout, "<slot#> can be from 0 to 4\n");
                    }
                    else
                    {
                        ipc.parameter0 = bw + ((which << 4) & 0x70);
                        ipc.cmd = SET_BW;
                    }
                }
                else
                {
                    fprintf (stdout, "Insuficient arguments (set bw <slot> <bw>\n");
                    fprintf (stdout, "<slot#> 0 to 4; <bw>: 250K, 1M, 2M\n");
                }
            }
            else
            {
                fprintf (stdout, "Invalid parameter\n");
            }
            pthread_mutex_unlock (&send_serial_mutex);
        }
        while (rounds--);
    }
    else
    {
        fprintf (stdout, "Usage:\tset { zch | master | rate | hop | stretch | region | baud | proto | bw | slot }\n");
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
                         i, g_stats[i].rssi_sum  / g_stats[i].rssi_samples,
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

