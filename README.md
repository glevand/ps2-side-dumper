# ps2--side-dumper

A USB packet dumper for Sony Playstation 2 Linux.

For PS2 linux-2.2.1 kernel with 2.2.18 USB backport.

  https://en.wikipedia.org/wiki/Linux_for_PlayStation_2

## Info

  SideDumper, a general purpose dumper for USB packets.

  Copyright (C) 2001  Sony Computer Entertainment Inc.

  This file is subject to the terms and conditions of the GNU General
  Public License Version 2. See the file "COPYING" in the main
  directory of this archive for more details.

  $Id: side_dumper.txt,v 1.5 2002/01/10 11:40:45 glevand Exp $

## Intro

SideDumper is a general purpose USB packet dumper that can be configured
to be built into the USB controller driver.  Dumper settings can be set
on a per device basis, making it useful for investigating a single USB
device.  It can interpret the standard USB device requests on the
default control pipe, as well as several other common packet types.   

## Configuration

Configure your kernel with the appropriate USB controller driver and
enable CONFIG_USB_SIDE_DUMPER. If building the driver as a module, the
module will be called ps2-ohci.o, usb-ohci-dumper.o, or
usb-uhci-dumper.o, depending on which USB controller is configured.

Currently only a single USB controller is supported.

## Dumper Settings

Valid dumper settings:

    Enabled  - Master dump enable.
    Async    - Asyncronous mode, uses the worker thread for processing.
    OutLimit - Limits output pipe transfer buffer dumps, in bytes.
    InLimit  - Limits input pipe transfer buffer dumps, in bytes.

Dumper settings can be set on a per device basis via the proc file
system entry "/proc/side_dumper".  The default file modes are 
"S_IFREG | S_IRUGO | S_IWUSR".  Currently all dumper output goes to
printk().  

The command "cat /proc/bus/usb/devices" will show what devices are
present on the bus. Please see the file
"Documentation/usb/proc_usb_info.txt" for more information on the
"/proc/bus/usb/devices" entry.

In asyncronous mode the driver's main thread queues the urb data to an
fifo queue.  A background thread then does the formatting and output of
the urb data.  This mode is useful for dumping data from high volume
devices like mass storage. It is also useful for dumping data when
syncronous mode causes device timing problems.

The trace limit settings are useful for dumping data in syncronous
mode when high data volumes cause device timing problems.

To view enabled devices:

    cat /proc/side_dumper/enabled

To view disabled devices:

    cat /proc/side_dumper/disabled

To enable devices:

    echo "xx yy" > /proc/side_dumper/enabled

To disable devices:

    echo "xx yy" > /proc/side_dumper/disabled

To view dumper settings:

    cat /proc/side_dumper/device_settings/device_xx

To view all device settings:

    cat /proc/side_dumper/device_settings/all

To set dumper settings:

    echo "setting:value" > /proc/side_dumper/device_settings/device_xx

Sample session:

    # cat /proc/side_dumper/enabled
    SideDumper: enabled devices = [ ]

    # cat /proc/side_dumper/disabled
    SideDumper: disabled devices = [ 01 02 03 04 05 06 07 08 09 10 11 12 13 14 15 ]

    # echo "3 9 7" > /proc/side_dumper/enabled

    # cat /proc/side_dumper/enabled
    SideDumper: enabled devices = [ 03 07 09 ]

    # cat /proc/side_dumper/disabled
    SideDumper: disabled devices = [ 01 02 04 05 06 08 10 11 12 13 14 15 ]

    # echo "3 9" > /proc/side_dumper/disabled

    # cat /proc/side_dumper/enabled
    SideDumper: enabled devices = [ 07 ]

    # cat /proc/side_dumper/disabled
    SideDumper: disabled devices = [ 01 02 03 04 05 06 08 09 10 11 12 13 14 15 ]

    # cat /proc/side_dumper/device_settings/device_08
    SideDumper: device_08 settings = [ Enabled:0 Async:0 OutLimit:4294967295 InLimit:4294967295 ]

    # echo "Enabled:1" > /proc/side_dumper/device_settings/device_08

    # cat /proc/side_dumper/device_settings/device_08
    SideDumper: device_08 settings = [ Enabled:1 Async:0 OutLimit:4294967295 InLimit:4294967295 ]

    # echo "Enabled:1 OutLimit:22 InLimit:33" > /proc/side_dumper/device_settings/device_11

    # cat /proc/side_dumper/device_settings/device_11
    SideDumper: device_11 settings = [ Enabled:1 Async:0 OutLimit:22 InLimit:33 ]

## Output Interpretation

'urb_0001: device #7, ctrl_0 >, 0 bytes.'

    urb_xxxx                         - A sequential URB number, for reference only.
    device #x                        - The USB device number, as shown in /proc/bus/usb/devices. 
    ctrl_x | bulk_x | intr_x | iso_x - The type of pipe, and the pipe number. 
    < | >                            - The data transfer direction: < = input, > = output. 
    x bytes                          - The number of data bytes transferred.

'*queue urb_0009 <'
    
    When using asyncronous mode, this marks when the urb was put on the
    worker thread queue. 

' sp = [ 00 05 0007 0000 0000 ]'

    Hex dump of the setup packet on the default control pipe, followed by 
    an interpretation.

' tb = [ 00 00 ]'

    Hex dump of the data transfer buffer, possibly followed by an interpretation.

## Typical Output Samples

Probing a device:

    urb_0001: device #7, ctrl_0 >, 0 bytes.
     sp = [ 00 05 0007 0000 0000 ]
       h->d, USB_REQ_SET_ADDRESS(@7), USB_RECIP_DEVICE
    urb_0002: device #7, ctrl_0 <, 8 bytes.
     sp = [ 80 06 0100 0000 0008 ]
       h<-d, USB_REQ_GET_DESCRIPTOR(USB_DT_DEVICE), USB_RECIP_DEVICE
     tb = [ 12 01 10 01 00 00 00 10 ]
       -- device descriptor -----
       short transfer (8/18 bytes)
       --------------------------
    urb_0003: device #7, ctrl_0 <, 18 bytes.
     sp = [ 80 06 0100 0000 0012 ]
       h<-d, USB_REQ_GET_DESCRIPTOR(USB_DT_DEVICE), USB_RECIP_DEVICE
     tb = [ 12 01 10 01 00 00 00 10 4c 05 58 00 00 01 01 02 00 01 ]
       -- device descriptor -----
       bLength            = 12h (18)
       bDescriptorType    = 01h
       bcdUSB             = 0110h
       bDeviceClass       = 00h 
       bDeviceSubClass    = 00h
       bDeviceProtocol    = 00h
       bMaxPacketSize0    = 10h (16)
       idVendor           = 054ch
       idProduct          = 0058h
       bcdDevice          = 0100h
       iManufacturer      = 01h (1)
       iProduct           = 02h (2)
       iSerialNumber      = 00h (0)
       bNumConfigurations = 01h (1)
       --------------------------

A mass-storage CBI command:

    urb_0019: device #7, ctrl_0 >, 12 bytes.
     sp = [ 21 00 0000 0000 000c ]
       h->d, USB_TYPE_CLASS, USB_RECIP_INTERFACE
     tb = [ 25 00 00 00 00 00 00 00 00 00 00 00 ]
    urb_0020: device #7, bulk_2 <, 8 bytes.
     tb = [ 00 01 ef 7f 00 00 02 00 ]
    urb_0021: device #7, intr_1 <, 2 bytes.
     tb = [ 00 00 ]

An asynchronous dump to printk():

    usb-storage.c: Command READ_10 (10 bytes)
    usb-storage.c:   28 00 00 00 00 00 00 00 02 00
    side_dumper: *queue urb_0331 >
    side_dumper| urb_0329: device #7, ctrl_0 >, 12 bytes.
    side_dumper|  sp = [ 21 00 0000 0000 000c ]
    usb-storage.c: Call to usb_stor_control_msg() returned 12
    usb-storage.c: us_transfer_partial(): xfer 1024 bytes
    side_dumper|    h->d, USB_TYPE_CLASS, USB_RECIP_INTERFACE
    side_dumper|  tb = [ 1b 00 00 00 01 00 00 00 00 00 00 00 ]
    side_dumper| urb_0330: device #7, intr_1 <, 2 bytes.
    side_dumper|  tb = [ 00 00 ]
    side_dumper| urb_0331: device #7, ctrl_0 >, 12 bytes.
    side_dumper|  sp = [ 21 00 0000 0000 000c ]
    side_dumper|    h->d, USB_TYPE_CLASS, USB_RECIP_INTERFACE
    side_dumper|  tb = [ 28 00 00 00 00 00 00 00 02 00 00 00 ]
    side_dumper: *queue urb_0332 <
    usb-storage.c: usb_stor_bulk_msg() returned 0 xferred 1024/1024
    usb-storage.c: us_transfer_partial(): transfer complete
    usb-storage.c: CBI data stage result is 0x0
    side_dumper: *queue urb_0333 <
    usb-storage.c: USB IRQ received for device on host 0
    usb-storage.c: -- IRQ data length is 2
    usb-storage.c: -- IRQ state is 0
    usb-storage.c: -- Interrupt Status (0x0, 0x0)
    side_dumper| urb_0332: device #7, bulk_2 <, 1024 bytes.
    usb-storage.c: scsi cmd done, result=0x0
