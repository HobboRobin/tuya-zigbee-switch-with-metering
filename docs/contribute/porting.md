*Open the **Outline** (table of contents) from the top right.*  

# Porting  

*Follow this guide if [supported_devices.md](/docs/supported_devices.md) does not include your device.* 

### Steps
1. Check compatibility
2. **Obtain the board pinout**
3. Add an entry to [`device_db.yaml`](../../device_db.yaml)
4. Build and install the firmware

## Compatibility 

[ZT series]: https://developer.tuya.com/en/docs/iot/zt-series-module?id=Kaiuym8ctid7k
[ZS series]: https://developer.tuya.com/en/docs/iot/zs-series-module?id=Kaiuyljrfi0wv
[TYZS series]: https://developer.tuya.com/en/docs/iot/tyzs-series-module?id=Kaiuylr3s96a6
[updating.md]: /docs/updating.md
[flashing/]: ../flashing/
[IEEE Address]: /docs/.images/screen_telink_mac.png
[TLSR825x]: https://wiki.telink-semi.cn/wiki/chip-series/TLSR825x-Series/

The firmware works on **Telink** ([TLSR825x]) and **Silabs** (EFR32MGxx) microcontrollers.  
⤷ Check the **[IEEE Address]** or open the device to identify the MCU.  

|                   | Telink                                | Silabs                                                |
|------------------:|---------------------------------------|-------------------------------------------------------|
| Devices           | • Most Tuya devices after 2023        | • Some Tuya remotes, switches <br> • All SONOFF, IKEA |
| Tuya modules      | [ZT series]                           | [ZS series], [TYZS series]                            |
| IEEE Address      | `0xa4c138xxxxxxxxxx`                  | Use *MAC lookup* website                              |
| Stock ➡ Custom FW | OTA [updating.md] or wire [flashing/] | wire [flashing/]                                      |

## Pinout

Every device has a different GPIO mapping.  
⤷ You must find **which pins the peripherals are connected to.**  

Example: button on D2, LED on C2, switch on B5, relay on C4.  

### Diagrams

**See [`diagrams/`](../diagrams/) for some simplifications and improvements over official datasheets.**

[labels]: https://github.com/romasku/tuya-zigbee-switch/issues/145#issuecomment-3303035527
[visible traces]: https://github.com/romasku/tuya-zigbee-switch/issues/146#issuecomment-3302750944
[lamp trick]: https://github.com/romasku/tuya-zigbee-switch/pull/188#issuecomment-3506916760
[solder points]: https://github.com/romasku/tuya-zigbee-switch/issues/183#issuecomment-3491147138
[follow pattern]: https://github.com/romasku/tuya-zigbee-switch/issues/181#:~:text=Pictures-,Configs,-We%20obtained%20the
[Tasmota]: https://templates.blakadder.com/switch.html
[ESPHome]: https://devices.esphome.io/type/switch
[OpenBeken]: https://openbekeniot.github.io/webapp/devicesList.html
[Tuya WiFi modules]: https://developer.tuya.com/en/docs/iot/wifi-module?id=Kaiuyi301kmk4

### Obtaining

There are multiple safe ways to obtain the pinout:
- **Look for clues on the PCB**: [labels], [solder points] and [visible traces] ([lamp trick]) 
- **Test continuity** (resistance) with a multimeter
- **Try each pin** until something works (edit *config string* in Z2M)
- Extract pinout from original firmware (memory dump)
- Truncate the pinout of a higher-gang model ([follow pattern])
- Get pinout from WiFi variant ([Tasmota], [ESPHome], [OpenBeken] devices ↔ [Tuya WiFi modules])
- Ask someone else to do it 🙂

> [!CAUTION]  
> Tuya devices do not have galvanic isolation! *The DC circuit may operate at 230-235V.*  
> ***Do not plug a dissasembled device into mains power!*** 

### Config string

The pinout is stored in the **device config string**.
- Prepare it for the database entry  
- Update it in Z2M after flashing  
(try different pins, until the device works properly)

**Format**:  
⤷ `<new manufacturer>;<new model>;<pin setup 1>;<pin setup 2>;...;<pin setup n>;`  

**Simple example** (1-gang module with LED on `A2`, switch on `A3` and relay on `A4`):  
⤷ `ljasd9as;TS0001-ABC;LA2;SA3u;RA4;`  

**Complex example** (2-gang switch with bi-stable relays):  
⤷ `osap2dsa;TS0002-ABC;BC3u;LC2i;SB5u;RD2D4;IA0;SB4u;RD3B1;IA1;M;i43533;`

**Cover example** (1-gang cover controller):  
⤷ `mfgname;TS130F-CVR;BC5u;LA3;XA2B3u;CC4D2;`

| Ch      | Peripheral    | Function                                                                                                          |
|--------:|---------------|-------------------------------------------------------------------------------------------------------------------|
| **`B`** | Reset button  | • Puts device in pairing                                                                                          |
| **`L`** | Network led   | • Blinks while pairing <br> • Is the backlight sometimes                                                          |
| **`S`** | Switch        | • User input <br> • Tactile/touch button or external switch <br> • Spam to put in pairing mode                    |
| **`R`** | Relay / Triac | • Output <br> • Non-latching: `RC1` - 1 pin: on when high <br> • Non-latching inverted: `RC1i` - 1 pin: active-low (on when low) <br> • Latching: `RC2C3` - 2 pins: pulse on, pulse off  |
| **`X`** | Cover Switch  | • User input for cover control <br> • Format: `XA2B3u` - 2 pins + pull resistor: open button, close button        |
| **`C`** | Cover         | • Motor control for curtains/blinds/shades <br> • Format: `CA2B3` - 2 pins: open relay, close relay               |
| **`I`** | Indicator LED | • 1 per relay, follows state <br> • Briefly flashes on button press (binding confirmation) <br> • Blinks while pairing if there is no network led |

For buttons (`B`), switches (`S`), and cover switches (`X`), the next character chooses the internal pull-up/down resistor:  
⤷ **`u`: up 10K**, `U`: up 1M, `d`: down 100K, `f`: float (external resistor)  

Usually, pressing the button bridges the GPIO pin to Ground (active low).  
⤷ So we need a pull-up resistor `u`: to hold it at VCC (high) while not-pressed.

For LEDs, add `i` to invert the state. Add `p` to make an LED PWM-dimmable
(works for both `I` indicator LEDs, e.g. `IB4ip`, and the `L` network/status
LED, e.g. `LA0ip`): the firmware then exposes a brightness (0-255) and a fade
transition time (ms) for that LED in Z2M. On TLSR825x each pin maps to one
fixed PWM channel (pins with PWM: A0, A2-A4, B0-B5, C0-C7, D2-D5; two LEDs
must not share a channel — e.g. B4 and C6 both use PWM4). If the pin has no
PWM the LED silently falls back to plain on/off.

Additional options: 
| Format       | Option                       | Function                                                                          |
|-------------:|------------------------------|-----------------------------------------------------------------------------------|
| **`D<N>`**   | Debounce delay               | • Set software debounce in ms for all buttons/switches <br> • Example: `D0` disables it |
| **`i00000`** | Image type                   | • Change OTA image_type (migrate to another build)                                |
| **`M`**      | Momentary                    | • Defaults buttons to momentary mode (for devices with built-in switches)         |
| **`BT<pin>`** | Battery mode                | • Enables battery-powered behavior <br> • Adds battery measurement/reporting using the selected ADC pin |
| **`SLP`**    | Simultaneous Latching Pulses |  • Enable simultaneous pulses for latching relays (they are disallowed by default)|
| **`EP<CF><CF1><SEL>`** | Energy monitoring (HLW8012/BL0937) | • Adds power/voltage/current/energy on EP1 <br> • 3 pins: CF (power), CF1 (voltage+current, time-multiplexed), SEL (mode select) <br> • Example: `EPC0C2C1` (CF=C0, CF1=C2, SEL=C1) |
| **`EB<TX><RX>`** | Energy monitoring (BL0942, UART) | • Adds power/voltage/current/energy on EP1 <br> • 2 pins from the MCU's point of view: TX (poll command out), RX (data in) <br> • Optional `S<baud>` overrides the 4800 default (e.g. `EBB0B7S9600`) <br> • On TLSR825x RX must be a UART RX pin (A0/B0/B7/C3/C5/D6); TX may be any pin (bit-banged if not A2/B1/C2/D0/D3/D7) <br> • On EFR32 (Silabs) any two pins work (flexible routing) |
| **`OL[C<mA>][P<mA>]`** | Overload limits | • Sets the device's rated overload-protection caps (defaults: 10 A continuous / 16 A peak) <br> • `C`: continuous (soft) current in mA — warn, then trip after the configurable delay <br> • `P`: peak (hard) current in mA — instant trip <br> • Matching wattages are derived at 230 V <br> • Example: `OLC16000P20000` = 16 A soft / 20 A hard <br> • Only meaningful after an `EP`/`EB` token |

The `EP` and `EB` tokens may be followed by optional calibration multipliers, in any
order, to override the compiled-in defaults **without a rebuild** — useful when
the same fingerprint ships in several PCB revisions with different sense
resistors/dividers:
⤷ **`V<n>`**: voltage multiplier · **`A<n>`**: current multiplier · **`W<n>`**: power multiplier

The physical value is `pulses * multiplier / 65536` (voltage in cV, current in
mA, power in W). Example — same board but a revision with CF/SEL swapped and a
different voltage divider: `EPC1C2C0V1284815` (pins swapped, voltage multiplier
overridden, current/power left at their defaults). Calibrate against a known
reference: `new = old * (measured / reported)`.

## Build and install

[`device_db.yaml`]: /device_db.yaml
[device_db_explained.md]: ./device_db_explained.md
[building.md]: ./building.md

1. Fork the repository and add an entry to [`device_db.yaml`].  
*Remove other devices to build faster (or use build: no).*   
Follow [device_db_explained.md] and validate with `device_db.schema.json` (e.g. YAML VSCode extension).  

2. Visit GitHub Actions on your fork (web) and run `build.yml`. More info: [building.md]

3. Follow [updating.md] with the index and converters from your branch.  
Alternatively, try [flashing/] via wire.

> Thank you for trying this firmware!  
