//
//  main.c
//  serialtest
//
//  Created by Lix N. Paulian on 17/05/2017.
//  Copyright © 2017 ARRI Wien. All rights reserved.
//

#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <termios.h>
#include <fcntl.h>


#define VERSION_MAJOR 0
#define VERSION_MINOR 1


int main(int argc, char * argv[])
{
    int ch;
    char *port = NULL;
    int fd;
    struct termios options;
    int baudRate = 115200;
    
    while ((ch = getopt(argc, argv, "hvD:b:")) != -1)
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
                fprintf(stdout, "Serial Test, version %d.%d\n", VERSION_MAJOR, VERSION_MINOR);
                exit(EXIT_SUCCESS);
                break;
                
            case 'h':
            default:
                fprintf(stdout, "Usage: sertest -D <tty>\n");
                exit(EXIT_SUCCESS);
                break;
         }
    }
    
    if (port == NULL)
    {
        fprintf(stdout, "Missing device (-D option)\n");
        exit(EXIT_FAILURE);
    }
    
    fd = open(port, O_RDWR | O_NOCTTY | O_NDELAY);
    if (fd != -1)
    {
        fcntl(fd, F_SETFL, FNDELAY);
        tcgetattr(fd, &options);
        cfsetispeed(&options, baudRate);
        cfsetospeed(&options, baudRate);
        options.c_cflag |= (CLOCAL | CREAD);
        
        options.c_cc[VTIME] = 2;
        options.c_cc[VMIN] = 0;
        tcsetattr(fd, TCSANOW, &options);
        tcsetattr(fd, TCSAFLUSH, &options);
    }
    else
    {
        fprintf(stdout, "Failed to open serial device %s\n", port);
        exit(EXIT_FAILURE);
    }

    char msg [] = { "Baburiba hey!" };
    char buff[100];
    memset (buff, 0, sizeof (buff));
    ssize_t res = write (fd, msg, strlen (msg));
    sleep (1);
    res = read (fd, buff, sizeof (buff));
    if (res > 0)
        fprintf(stdout, "%s\n", buff);
    
    return EXIT_SUCCESS;
}

