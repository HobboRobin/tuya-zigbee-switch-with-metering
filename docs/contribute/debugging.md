# Debugging

[3D_model]: https://www.printables.com/model/763948-tuya-cbu-flashing-jig


Currently, debugging is very rudimentary.  

It consists of adding **prints in the code** and building with the DEBUG flag enabled.  
(See [`make_scripts/make_debug_single.sh`](/make_scripts/make_debug_single.sh).)  

The flag is all it takes - the firmware is already full of prints, they are just
compiled out of a release build:

```bash
BOARD=<board from device_db.yaml> DEVICE_TYPE=end_device DEBUG=1 make board/build-firmware
```

After you flash the debug build, the device will output messages to the **UART TX pin**
at **115200 baud**, 8N1.  
The TX pin is **PB1**. Check the [diagrams/](../diagrams/) for where it comes out on your board.  
Both are defined in [app_cfg.h](/src/telink/configs/app_cfg.h)
(`DEBUG_INFO_TX_PIN`, `DEBUG_BAUDRATE`).  

> [!TIP]
> This is the fastest way to tell *why* a device is not joining. A device that
> never appears on the network at all looks identical from the coordinator
> whether it is failing to parse its config, resetting in a loop, or sleeping
> through commissioning - and the log says which. Build the **end_device**
> variant with `DEBUG=1` when a battery device joins as a router but not as an
> end device, since that difference lives entirely in the power-management
> path.

You can safely power and use the device with 3.3V DC from the programmer.  
It behaves exactly the same way it does when powered with 220V AC.

> [!WARNING]  
> Do not open the device while connected to mains!  
> Do not connect both mains voltage and 3.3V at the same time!  

Attach a probe to the TX pin of the switch and connect it to the RX pin of the receiver.  
(You no longer need the SWS and RST connections. These are only for flashing.)  

There are multiple options available for keeping the wires connected:  
soldering, pogo-pins, [3D printed jig][3D_model], laser cut jig, PCBite kit, holding them with your hand.  
You might need to desolder some components to access the pins.

Use this command to find the port:

```bash
ls -l /dev/serial/by-id
```

Make sure the port is not already in use (stop the flasher) and  
start the serial monitor with this command (update the port):

```bash
sudo minicom -b 115200 -o -D /dev/ttyUSB0
```