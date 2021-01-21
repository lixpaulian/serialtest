//
//  main.c
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
#include <sys/_select.h>
#include <sys/ioctl.h>
#include <IOKit/serial/ioss.h>
#include <pthread.h>

#include "frame-parser.h"
#include "cli.h"
#include "utils.h"
#include "statistics.h"


#define SERIAL_DEBUG 0


void
quit (void);

static int
handle_serial_line (int fd, bool print);

static int
locate_port (char *location, char* path, size_t len);



// Main entry point.

int
main (int argc, char * argv[])
{
    int ch;
    char *port = NULL;
    char *address = NULL;
    int fd;
    struct termios options;
    int baudRate = 115200;
    
    signal (SIGINT, (void *) quit);	/* trap ctrl-c calls here */
    
    while ((ch = getopt (argc, argv, "hvD:l:b:a:")) != -1)
    {
        switch (ch)
        {
            case 'D':
                port = optarg;
                break;
                
            case 'l':
                address = optarg;
                break;
                
            case 'b':
                baudRate = atoi (optarg);
                break;
                
            case 'a':
                own_address (SET_PARAMETER, atoi (optarg));
                break;
                
            case 'v':
                getver (0, NULL);
                exit (EXIT_SUCCESS);
                break;
                
            case 'h':
            default:
                fprintf (stdout, "Usage: serialtest -D <tty>\n\tor serialtest -l <usb_location_ID>\n");
                fprintf (stdout, "\tother options: -b <baudrate>, -a <own_address>, -v, -h\n");
                exit (EXIT_SUCCESS);
                break;
        }
    }
    
    char buff[200];
    if (address != NULL)
    {
        if (locate_port (address, buff, sizeof (buff)) == true)
        {
            port = buff;
        }
    }
    
    if (port == NULL)
    {
        fprintf (stdout, "Missing device (-D or -p option required)\n");
        exit (EXIT_FAILURE);
    }
    
    fd = open(port, O_RDWR | O_NOCTTY | O_NDELAY);
    if (fd != -1)
    {
        fcntl (fd, F_SETFL, 0);
        tcgetattr (fd, &options);
#if USE_IOSSIOSPEED == false
        cfsetispeed (&options, baudRate);
        cfsetospeed (&options, baudRate);
#endif
        options.c_cflag |= (CLOCAL | CREAD);
        options.c_oflag &= ~OPOST;
        options.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
        
        options.c_cc[VTIME] = 0;
        options.c_cc[VMIN] = 1;
        options.c_cc[VERASE] = 8;
        if (tcsetattr (fd, TCSANOW, &options) < 0)
        {
            fprintf (stdout, "Failed to set new parameters on the serial port\n");
            exit (EXIT_FAILURE);
        }
#if USE_IOSSIOSPEED == true
        speed_t speed = baudRate;
        if (ioctl(fd, IOSSIOSPEED, &speed) == -1 )
        {
            fprintf (stdout, "Failed to set new baudrate\n");
        }
#endif
    }
    else
    {
        fprintf (stdout, "Failed to open serial device %s\n", port);
        exit (EXIT_FAILURE);
    }
    
    // set proper backspace character
    tcgetattr (fileno (stdin), &options);
    options.c_cc[VERASE] = 0x8;
    if (tcsetattr (fileno (stdin), TCSANOW, &options) < 0)
    {
        fprintf (stdout, "Failed to set erase character on stdin\n");
    }
    set_serial_fd (fd);
    
    clear_stats (); // clear all statistic data
    
    pthread_t thread;
    
    // create a  thread for sending frames over the serial port
    if (pthread_create (&thread, NULL, send_frames, (void *) &fd))
    {
        fprintf(stdout, "Error creating thread\n");
        exit (EXIT_FAILURE);
    }
    
    // prepare for multiple input sources
    size_t res;
    fd_set readfs;
    int maxfd = fd + 1;
    
    do
    {
        FD_SET (fd, &readfs);
        FD_SET (fileno (stdin), &readfs);
        
        res = select (maxfd, &readfs, NULL, NULL, NULL);
        if (FD_ISSET(fd, &readfs))
        {
            // got a frame from serial
            res = handle_serial_line (fd, dump_frames (GET_PARAMETER, false));
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
// @param   fd: serial file descriptor.
// @retval  0 if successful, 1 if serial port error.

static int
handle_serial_line (int fd, bool print)
{
    ssize_t res;
    static uint8_t buff[400];
    static ssize_t offset = 0;
    int8_t rssi;
    struct timespec tp;
    static long last_entry = 0;
    
    clock_gettime (CLOCK_MONOTONIC, &tp);
    long tmp = tp.tv_nsec / 1000;
    long diff = tmp - last_entry;
    if (diff > 2000)    // 2 ms
    {
        offset = 0;
#if SERIAL_DEBUG == 1
        fprintf (stdout, "----\n");
#endif
    }
    last_entry = tmp;
    
    if ((res = read (fd, buff + offset, sizeof (buff) - offset)) > 0)
    {
#if SERIAL_DEBUG == 1
        fprintf (stdout, "\n%ld: read %ld bytes, offset %ld\n", offset ? diff : tmp, res, offset);
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
        
        op_mode_t mode = get_mode ();
        if (mode == WHITE_RADIO || mode == WHITE_RADIO_PLUS)
        {
            while ((result = parse_f0_f1_frames (&begin, &end, &rssi)) == FRAME_OK)
            {
                if (print)
                {
                    print_frames (begin, end - begin + 1, rssi);
                }
                
                int count = extract_f0_f1_frame (begin, end - begin + 1);
                if (count > 0)
                {
                    analyzer (begin, count - 1, rssi);
                }
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
        }
        else if (mode == ROTFUNK_PLUS)
        {
            // rot funk plus
#if SERIAL_DEBUG == 1
            fprintf (stdout, "count %ld, header %lu\n", res + offset, buff[0] + sizeof (red_header_t));
#endif
            if (buff[0] == 0xCC && res + offset >= 3)
            {
                // get rid of 0xCC answers
                offset = 0;
            }
            else if ((res + offset) == (buff[0] + sizeof (red_header_t)))
            {
                // frame complete
                size_t payload_len = buff[0];
                uint8_t* p = &buff[3];
                
                if (print)
                {
                    print_frames (p, payload_len, buff[2]);
                }
                if (payload_len)
                {
                    analyzer (p, payload_len, buff[2]);
                }
                offset = 0;
            }
            else
            {
                offset += res;
                if (offset >= sizeof (buff))
                {
                    offset = 0;
                }
            }
        }
        else if (mode == PLAIN)
        {
            fprintf (stdout, "read %ld bytes, offset %ld\n", res, offset);
            for (int i = 0; i < res; i++)
            {
                fprintf (stdout, "%02x ", (buff + offset)[i]);
            }
            fprintf (stdout, "\n");
        }
        return 0;
    }
    else
    {
        perror("serial port read");
        return 1;
    }
}

#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/usb/IOUSBLib.h>
#include <IOKit/IOBSD.h>
#include <IOKit/IOMessage.h>
#include <IOKit/serial/ioss.h>

static int
locate_port (char *location, char* path, size_t len)
{
    CFMutableDictionaryRef matchingDict;
    io_iterator_t iter;
    kern_return_t kr;
    io_service_t device;
    IOCFPlugInInterface **plugInInterface = NULL;
    IOUSBDeviceInterface **dev = NULL;
    SInt32 score;
    ULONG result;
    UInt16 vendor;
    UInt16 product;
    UInt16 address;
    int res = false;
    uint32_t find_location;
    
    sscanf (location, "%x", &find_location);
    
    /* set up a matching dictionary for the class */
    matchingDict = IOServiceMatching(kIOUSBDeviceClassName);
    if (matchingDict == NULL)
    {
        return -1; // fail
    }
    
    /* Now we have a dictionary, get an iterator.*/
    kr = IOServiceGetMatchingServices(kIOMasterPortDefault, matchingDict, &iter);
    if (kr != KERN_SUCCESS)
    {
        return -1;
    }
    
    /* iterate */
    while ((device = IOIteratorNext(iter)))
    {
        // Create an intermediate plug-in
        kr = IOCreatePlugInInterfaceForService(device,
                                               kIOUSBDeviceUserClientTypeID, kIOCFPlugInInterfaceID,
                                               &plugInInterface, &score);
        // Don’t need the device object after intermediate plug-in is created
        if ((kIOReturnSuccess != kr) || !plugInInterface)
        {
            fprintf(stdout, "Unable to create a plug-in (%08x)\n", kr);
            continue;
        }
        
        // Now create the device interface
        result = (*plugInInterface)->QueryInterface(plugInInterface,
                                                    // CFUUIDGetUUIDBytes(kIOUSBDeviceInterfaceID),
                                                    // this is for macos 10.13
                                                    CFUUIDGetUUIDBytes(kIOUSBDeviceInterfaceID650),
                                                    (LPVOID *)&dev);
        
        // Don’t need the intermediate plug-in after device interface is created
        (*plugInInterface)->Release(plugInInterface);
        
        if (result || !dev)
        {
            fprintf(stdout, "Couldn’t create a device interface (%08x)\n", (int) result);
            continue;
        }
        
        uint32_t locationID;
        
        kr = (*dev)->GetDeviceVendor(dev, &vendor);
        kr = (*dev)->GetDeviceProduct(dev, &product);
        kr = (*dev)->GetDeviceAddress (dev, &address);
        kr = (*dev)->GetLocationID(dev, &locationID);
        
        if ((vendor != 0x10c4) || (product != 0xea60))
        {
            // we look for Silicon Labs CP2102 chips only
            (void) (*dev)->Release(dev);
            continue;
        }
        
        if (locationID == find_location)
        {
            CFStringRef deviceBSDName_cf;
            deviceBSDName_cf = (CFStringRef) IORegistryEntrySearchCFProperty (device,
                                                                              kIOServicePlane,
                                                                              CFSTR (kIOCalloutDeviceKey),
                                                                              kCFAllocatorDefault,
                                                                              kIORegistryIterateRecursively);
            result = CFStringGetCString(deviceBSDName_cf,
                                        path,
                                        len,
                                        kCFStringEncodingUTF8);
            
            (void) (*dev)->Release(dev);
            IOObjectRelease(device);
            res = true;
            break;
        }
        
        (void) (*dev)->Release(dev);
        IOObjectRelease(device);
    }
    
    /* Done, release the iterator */
    IOObjectRelease(iter);
    
    return res;
}

void
quit (void)
{
    exit (1);
}
