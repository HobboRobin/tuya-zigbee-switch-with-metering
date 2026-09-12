# How to Flash via Wire

### What You'll Need

- A UART programmer (preferably one with an exposed RTS pin). I used the FT232RL.
- A soldering iron
- Some spare jumper wires

### Step 1: Disassemble the Device

Unplug the device from the mains. **Never attempt to disassemble it while it is plugged in!**  

Start by removing the back cover. Use any flat object to pop it off.  Here is a picture for TS0012, other devices can be opened in a similar way:

![Back cover disassembly](/docs/.images/ts0012_back_cover.jpg)  

Next, remove the board from the case by pushing on the screws; it should come out easily.

### Step 2: Solder the Wires

You’ll need to solder four wires as shown in the photo: 

<details>
  <summary>TS0012</summary>
  
 ![Wiring](/docs/.images/ts0012_wires.jpg)  
  
</details>


<details>
  <summary>TS0011 (or any ZT3L module)</summary>

   Note that I desoldered capacitors for easier access to SWS pin. It is OK to flash a device without them, but you need to reinstall them if you plan to use the device. Alternatively, you can try to solder SWS pin without this manipulation.

  ![Wiring](/docs/.images/ts0011_wires.jpg)  

</details>



The RESET wire is optional if your UART programmer lacks an RTS pin. It may work without it, but having it is preferable.  

Attach the wires to your UART as follows:  
- **3.3V** to **3.3V**  
- **Ground** to **Ground**  
- **SWS** to the **TX pin** of the programmer  
- **Reset** to the **RTS pin** of the programmer  

### Step 3: Flash the Firmware

Download the full firmware for your device from [bin/](https://github.com/romasku/tuya-zigbee-switch/raw/refs/heads/main/bin).  

Plug the UART into your PC, then open pvvx's [web flasher](https://pvvx.github.io/ATC_MiThermometer/USBCOMFlashTx.html).  

1. Select Baud **1500000** (for faster flashing), click "Open," and choose your UART port.  
2. Click "Choose file" and select the firmware you downloaded.  
3. Click "Erase All Flash" and wait until the process finishes.  
4. Click "Write to Flash" and wait until the process finishes.  

> [!IMPORTANT]  
> **The flasher cannot tell you when something went wrong.** Its title says it:
> *TX-SWS only* — it sends on TX and never reads back, so it reports `Done`
> whether or not a chip was listening. Two failures look identical to a
> successful flash:
>
> - **SWS on the programmer's RX instead of TX.** Nothing is written at all.
>   The device keeps its old firmware, which for a stock Tuya module also
>   blinks its LED while unpaired — so it looks alive, just never joins.
> - **Skipping "Erase All Flash".** The write lands, but the old firmware's
>   Zigbee NV survives. The device boots believing it is still joined, so it
>   never searches for a network and its status LED stays dark. Erasing is
>   not optional.
>
> If the device does not join, check the wiring and repeat with the erase
> before suspecting the firmware.

> [!WARNING]
> **"Erase All Flash" also erases the factory sectors**, which hold the
> module's Zigbee MAC address and its RF calibration. They sit at the very top
> of the flash - `0x76000` and `0x77000` on a 512 KB part, `0xFF000` and
> `0xFE000` on a 1 MB one - and the erase takes the whole chip, those included.
>
> The device still works afterwards: with the MAC sector blank the firmware
> generates one on first boot and stores it. But it is a *different* address,
> so the device joins as a new device. Anything pinned to the old one - the
> Zigbee2MQTT entry, its bindings, group memberships and every automation
> naming those entities - points at a device that no longer exists, and has to
> be set up again. Losing the calibration costs a little range and transmit
> accuracy, which the firmware falls back on defaults for.
>
> **Read those two sectors out before erasing** if you want the device to come
> back as itself. The web flasher cannot do it - it only transmits - so use a
> tool that reads back over SWS, such as
> [TlsrPgm](https://github.com/pvvx/TLSRPGM) (`make telink/tools` fetches it)
> or TlsrComSwireWriter, and save both 4 KB sectors to a file. Writing the MAC
> sector back restores the original address.
>
> This only matters if the sectors still hold anything: a module that has
> already been erased once has nothing left to save.

If done correctly, the onboard LED will start flashing automatically. You can try joining the device to a Zigbee network to verify that it connects properly. Once confirmed, disconnect everything, unsolder the wires, and reassemble the case.  
