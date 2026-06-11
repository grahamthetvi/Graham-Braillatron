# **Skeleton Prototype V5.1 Build Guide**

**Project Lead:** Addison Graham | **Phase:** 2-Month "Coffee Table" Breadboard & 3-Tier Architecture Integration

This V5.1 Prototype Guide integrates the industrial-grade 3-tier architecture, adding the MKS Monster8 V2 32-Bit Control Board as a dedicated Klipper MCU, while retaining the Orange Pi 3B as the system Brain and the Arduino Micro as an isolated hardware safety Watchdog. This layout isolates logic and audio lines from heavy stepper motor EMI and tests safety failsafes, star grounding, digital I2S audio, multi-system handshakes, and Klipper integration.

## **Part 1: Comprehensive Bill of Materials (BOM)**

### **1\. High-Power & Safety Bus (Off-Breadboard)**

| Component | Specifications / Description | Qty Required |
| :---- | :---- | :---- |
| **Power Source** | 4S1P Molicel Lithium-Ion Pack (14.8V nominal, 16.8V max fully charged). Commercial battery holders equipped with Beryllium Copper leads/contacts to handle 15A+ continuous draw without overheating. (Generic and 3D printed sleds were deleted due to fire hazard and voltage sag risks). | 1 Pack |
| **Battery Management (BMS)** | 4S 30A Lithium BMS board with active cell balancing. | 1 Board |
| **Charge/Boost Controller** | IP2368 USB-C PD Module (Bi-directional, 100W capable). | 1 Module |
| **Main Motor Fuse** | 15A Automotive Inline Blade Fuse (placed on 14.8V Motor rail). | 1 Fuse |
| **Logic Fuse** | 3A-5A Inline Blade Fuse (placed on 14.8V Logic/SBC buck input line). | 1 Fuse |
| **Thermal Cutoff Fuse** | Non-resettable 85°C Thermal Fuse (wired in series with primary motor line). | 1 Fuse |
| **Power Distribution** | 12-Position Dual-Row Screw Terminal Strip Blocks. 5-Port WAGO Lever Nut. | 1 Strip, 1 Nut |
| **MOSFET Gate Driver** | TC4420 (or equivalent) High-Speed Non-Inverting MOSFET Gate Driver IC. | 1 Chip |
| **High-Side Power MOSFET** | IRLZ44N N-Channel Power MOSFET (rated 30A+, logic-level gate) for VMOT cutoff. | 1 MOSFET |
| **Spike Protection** | Transient Voltage Suppressor (TVS) Diode (18V-20V clamp rating) across 14.8V rail. | 1 Diode |
| **Heavy-Gauge Wire** | Solid copper grounding and main power wire (\#12 to \#14 AWG). | 1 Spool |

### **2\. Compute, Logic & UI Components**

| Component | Specifications / Description | Qty Required |
| :---- | :---- | :---- |
| **Primary Processor (Tier 1\)** | Orange Pi 3B (4GB LPDDR4, Rockchip RK3566, active system-level GPIO). | 1 Unit |
| **Motion Controller (Tier 2\)** | MKS Monster8 V2 (32-Bit STM32 Control Board). | 1 Board |
| **Safety Watchdog (Tier 3\)** | Arduino Micro or Nano (ATmega32U4, 5V native logic). | 1 Unit |
| **Logic Power Supply** | TPS5430-based Mini560 High-Efficiency Synchronous Buck Regulator Module (Input: 14.8V, Output: 5.0V/5A logic power). | 1 Module |
| **Audio Amplifier Module** | MAX98357A I2S Class D Mono Audio Amplifier Breakout Board. | 1 Board |
| **Integrated Speaker** | 4Ω 3W or 8Ω 2W Enclosed Micro Speaker Capsule. | 1 Capsule |
| **Audio Filter Capacitors** | 1x 470µF 35V Low-ESR Electrolytic Capacitor (Rubycon). 1x 0.1µF Ceramic Capacitor. | 1 Set |
| **Gate Driver Bypass Capacitor** | 1x 0.1µF Ceramic Capacitor. | 1 Unit |
| **Audio SD Resistor** | 100kΩ Resistor. | 1 Unit |
| **Haptic UI Driver** | DRV2605L I2C Haptic Driver Breakout Board \+ 10mm LRA. | 1 Set |
| **Sensors** | 1x MPU6050 6-Axis I2C Accelerometer, 1x LTC2944 Battery Gas Gauge, 1x TCRT5000 IR Sensor, 1x TCST2103 Optical Slot Sensor. | 1 Set |
| **Keyboard Input System** | 12x Cherry MX Mechanical Switches. (Note: The 4x4 matrix, 1N4148 diodes, and external 10kΩ pull-up resistors have been deleted from the architecture). | 12 Switches |
| **EMI Suppression** | Ferrite Rings. | 1 Set |

### **3\. Motor Driver & Motion Components**

| Component | Specifications / Description | Qty Required |
| :---- | :---- | :---- |
| **Motor Drivers** | TMC2209 StepStick Silent Stepper Drivers (slotted into Monster8 sockets). | 8 Boards |
| **X-Axis Carriage Motor** | NEMA 17 Bipolar Stepper Motor (Slimline/Pancake style). | 1 Motor |
| **Y-Axis Paper Motor** | NEMA 17 Bipolar Stepper Motor (1.8° step angle, high-torque). | 1 Motor |
| **Embossing Pin Actuators** | Miniature Stepper Motors or solenoids (Row A: 1, 3, 5; Row B: 2, 4, 6). | 6 Units |

## **Part 2: Power Routing & Segregation**

### **1\. Battery Management System (BMS) Wiring**

* **Main Terminals:** Connect the 4S Pack Main Negative terminal to the BMS **B-** pad (14 AWG) and the Main Positive terminal to the BMS **B+** pad (14 AWG).  
* **Balance Header (5-Pin JST-XH):** Connect Pin 1 (Black) to Cell 1 Negative (0V), Pin 2 (White) to Cell 1/2 junction (3.7V), Pin 3 (Yellow) to Cell 2/3 junction (7.4V), Pin 4 (Blue) to Cell 3/4 junction (11.1V), and Pin 5 (Red) to Cell 4 Positive (14.8V).  
* **Thermal Failsafe:** Tape or shrink-wrap the 85°C Thermal Fuse directly to the Molicel cells. Wire it in series immediately off the BMS **P+** pad using 14 AWG wire before reaching the main power distribution node.

### **2\. Main Power Distribution (Split Nodes)**

* **Central Positive Busbar (5-Port WAGO Lever Nut):** Use 14 AWG wire for all connections except the logic bus pigtail (18 AWG).  
  * Port 1: From BMS **P+** pad (via Thermal Fuse).  
  * Port 2: From IP2368 Module **BAT+**.  
  * Port 3: To the 15A Automotive Blade Fuse (Feeds Motor Bus).  
  * Port 4: To the 5A Automotive Blade Fuse (Feeds Logic Bus).  
* **Central Negative Star Ground (12-Position Terminal Block):** Returns high-voltage and system-level grounds to a single block to prevent motor noise EMI.  
  * Connection 1 (14 AWG): Returns to BMS **P-** pad.  
  * Connection 2 (14 AWG): Returns from IP2368 Module **BAT-**.  
  * Connection 3 (14 AWG): Returns from MKS Monster8 V2 Power IN (-).  
  * Connection 4 (18 AWG): Returns from Mini560 Buck Converter **IN-**.

### **3\. Charge Controller & Motor Bus Integration**

* **IP2368 USB-C PD Module:** Connect **BAT+** to the Central Positive Busbar (14 AWG) and **BAT-** to the Central Negative Star Ground (14 AWG).  
* **Motor Bus (14.8V):** Connect 14 AWG wire from the 15A Blade Fuse directly to the MKS Monster8 positive terminal. Connect 14 AWG wire from the Monster8 negative terminal back to the Central Negative Star Ground.

### **4\. Clean Logic Bus (5V Local Star Topology)**

* **Mini560 Input:** Connect **IN+** (18 AWG) to the output of the 5A Blade Fuse. Connect **IN-** (18 AWG) directly to the Central Negative Star Ground. Adjust the Mini560 potentiometer to 5.1V under load before connecting devices.  
* **Local 5V Power Bus (Small Lever Nut):** Input is 18 AWG from Mini560 **OUT+**. Outputs split to the Orange Pi 3B (18 AWG to Pin 2 or 4\) and the Arduino Micro (22 AWG to 5V Pin).  
* **Local 5V Ground Bus (Small Lever Nut):** Input is 18 AWG from Mini560 **OUT-**. Outputs split to the Orange Pi 3B (18 AWG to Pin 6\) and the Arduino Micro (22 AWG to GND Pin).

## **Part 3: Data, Logic & Peripheral Wiring**

### **1\. Direct Pin Keyboard Topology (Tier 3 Watchdog)**

The traditional 4x4 switch matrix, steering diodes, and external 10kΩ pull-up resistors have been completely abandoned in favor of a Direct Pin Wiring topology.

* **Common Ground Bus:** One side of all 12 Cherry MX braille input switches are tied together into a single common ground bus routed directly to an Arduino Micro GND pin.  
* **Direct Signal Routing:** The isolated side of each switch routes directly to its own dedicated input pin on the Arduino Micro.  
* **Pin Mappings:**  
  * Button 1: Pin 4  
  * Button 2: Pin 5  
  * Button 3: Pin 6  
  * Button 4: Pin 8  
  * Button 5: Pin 9  
  * Button 6: Pin 10  
  * Button 7: Pin 11  
  * Button 8: Pin A4 *(Moved from Pin 13 to avoid onboard LED circuitry interference)*

  * Button 9: Pin A0  
  * Button 10: Pin A1  
  * Button 11: Pin A2  
  * Button 12: Pin A3

### **2\. MKS Monster8 V2 Setup & EMI Routing (Tier 2 Klipper MCU)**

* **Driver Installation:** Insert the 8 TMC2209 StepStick drivers directly into Driver Slots 0 through 7 on the Monster8. Place jumper caps over the specific UART pins under each driver slot.  
* **Communication:** Connect the Monster8 to the Orange Pi 3B via a shielded USB-C to USB-A data cable. Ensure the logic power jumper is set to draw 5V from USB.  
* **Paper Feeding Sensors:** The TCRT5000 and TCST2103 sensors do not connect to the Arduino Watchdog. They wire directly to the MKS Monster8 V2 Endstop Ports (e.g., Y-MIN, Y-MAX) drawing 5V and GND directly from the Monster8 endstop headers.  
* **EMI Suppression (Ferrite Rings):** To protect digital lines from stepper motor interference, ferrite rings must be placed on:  
  1. The braided wire bundle for the X and Y axis stepper motors (placed as close to the Monster8 as possible).  
  2. The wire looms leading to the 6 embossing solenoids.  
  3. The shielded USB-C to USB-A cable running between the Orange Pi and Monster8.

### **3\. Hardware Failsafe Subsystem (MPU6050 & TC4420)**

* **MPU6050 (GY-521):** Keep all connections under 10cm using 24 or 26 AWG Dupont Wires.  
  * **VCC & GND:** To Arduino Micro 5V and GND.  
  * **SCL:** To Arduino Micro Digital Pin 3\.  
  * **SDA:** To Arduino Micro Digital Pin 2\.  
  * **INT:** To Arduino Micro Digital Pin 7 (Hardware Interrupt 4).  
  * **ADO:** Tie to the Local 5V Ground Bus to lock I2C address to 0x68.  
* **TC4420 MOSFET Gate Driver:** Must be powered from the 5V logic rail using 22 AWG Solid Core wire.  
  * **Pins 1 & 8 (VDD):** To Local 5V Power Bus.  
  * **Pins 4 & 5 (GND):** To Local 5V Ground Bus.  
  * **Pin 2 (IN):** To Arduino Micro Digital Pin 12 (using 24/26 AWG Dupont).  
  * **Pins 6 & 7 (OUT):** Tied together, routed to Gate (Pin 1\) of the IRLZ44N MOSFET.  
  * **Bypass:** Place a 0.1µF Ceramic Capacitor directly across Pin 1 (VDD) and Pin 4 (GND) of the TC4420 to prevent micro-brownouts during rapid failsafe switching.  
* **IRLZ44N Low-Side Gate:** Gate (Pin 1\) from TC4420 Output. Drain (Pin 2\) to Main VMOT Negative input on the Monster8 (14 AWG). Source (Pin 3\) to Main 4S 14.8V Battery Negative / BMS B- terminal (14 AWG).

### **4\. I2S MAX98357A Mono Amplifier Setup**

Wire directly to the 40-pin header of the Orange Pi 3B using 22 AWG Solid Core wire to bypass analog noise.

* **VDD (5V Input):** To Orange Pi Pin 4\.  
* **GND:** To Orange Pi Pin 6 (or Local 5V Ground Bus).  
* **LRCK (WS):** To Orange Pi Pin 35 (I2S1\_LRCK).  
* **BCLK:** To Orange Pi Pin 38 (I2S1\_SCLK).  
* **DIN:** To Orange Pi Pin 40 (I2S1\_SDO0).  
* **Power Decoupling Capacitor Array:** Solder a 470µF 35V Low-ESR Electrolytic Capacitor (Rubycon) and a 0.1µF Ceramic Capacitor in parallel directly across the VDD and GND terminals of the MAX98357A to buffer high-frequency switching.  
* **Gain & Channel Routing:** Tie the GAIN pin to GND via a wire link for 9dB hardware gain. Use a 100kΩ resistor to pull the SD (Shutdown/Mode) pin high to the 3.3V logic rail, enforcing a clean mono mix.  
* **Speaker Termination:** Connect the 4Ω 3W enclosed micro speaker capsule leads to the MAX98357A differential speaker outputs (OUT+ / OUT-).

## **Part 4: Firmware & Software Handshakes**

### **1\. Watchdog Keyboard & Debounce Logic**

Because the hardware matrix is gone, the firmware exclusively utilizes the Arduino's internal pull-up resistors via the INPUT\_PULLUP declaration. The delay() function cannot be used for debouncing, as it will block the sub-10ms MPU6050 freefall interrupt. The firmware must utilize a non-blocking, array-based state machine using millis() tracking to scan all 12 pins simultaneously with a 15ms debounce delay threshold tailored for Cherry MX switches.

### **2\. MPU6050 Freefall Configuration (Arduino Boot Routine)**

The Arduino configures the MPU6050 on boot to run freefall detection natively in hardware:

C++  
\#include \<Wire.h\>      
      
void setupMPU6050Freefall() {      
  Wire.begin();      
        
  // Wake up the MPU6050 (write 0x00 to Power Management register 0x6B)      
  Wire.beginTransmission(0x68);      
  Wire.write(0x6B);      
  Wire.write(0x00);      
  Wire.endTransmission();      
        
  // Configure Freefall Threshold (0x1D): Set to 0x30 (approx 300mg)      
  Wire.beginTransmission(0x68);      
  Wire.write(0x1D);      
  Wire.write(0x30);      
  Wire.endTransmission();      
        
  // Configure Freefall Duration (0x1E): Set to 0x14 (approx 40ms)      
  Wire.beginTransmission(0x68);      
  Wire.write(0x1E);      
  Wire.write(0x14);      
  Wire.endTransmission();      
        
  // Enable Freefall Hardware Interrupt (0x38): Write Bit 7 (0x80) or Bit 1 (0x02) based on MPU variant      
  Wire.beginTransmission(0x68);      
  Wire.write(0x38);      
  Wire.write(0x02); // Enable free-fall interrupt      
  Wire.endTransmission();      
}

### **3\. Hardware Interrupt Routing (Sub-10ms Power Gate)**

The Arduino Micro triggers the critical sub-10ms emergency power isolation to the Monster8 upon detecting freefall.

C++  
const int safetyGatePin \= 12; // Drives TC4420 MOSFET Driver Gate Input      
      
void setup() {      
  pinMode(safetyGatePin, OUTPUT);      
  digitalWrite(safetyGatePin, HIGH); // Enable main stepper VMOT to Monster8      
  setupMPU6050Freefall();      
        
  // Attach ISR to INT4 (fires instantly when MPU6050 interrupt line transitions high)      
  attachInterrupt(digitalPinToInterrupt(7), handleFreefallEmergency, RISING);  // Interrupt routed to Pin 7\[cite: 3\]  
}      
      
void loop() {      
  // Normal low-priority operations (e.g., non-blocking debounce & USB HID output)\[cite: 1, 2\]   
}      
      
void handleFreefallEmergency() {      
  // SUB-10ms POWER ISOLATION: Instantly discharge the gate of IRLZ44N      
  digitalWrite(safetyGatePin, LOW);       
        
  // Package high-priority 4-byte serial CDC emergency packet: \[STX, STATUS, PAYLOAD, CHECKSUM\]      
  // The Orange Pi reads this and immediately sends an M112 Klipper E-Stop command      
  uint8\_t emergencyFrame\[4\] \= {0x02, 0x80, 0xFF, 0x7F};      
  Serial.write(emergencyFrame, 4);      
}

### **4\. System Handshake & GPIO Diagnostics via gpiod**

DietPi strictly utilizes the user-space gpiod utility library to manage the Pi-to-Arduino heartbeat handshakes.

Bash  
\# Install the GPIO diagnostic utility      
sudo apt update && sudo apt install \-y gpiod      
      
\# Display the layout, banking, and active statuses of all pins      
gpioinfo      
      
\# Output a keepalive heartbeat pulse to the Arduino (e.g., Pi Chip 3, Line 22\)      
\# The Arduino expects this pulse every 500ms; if it stops, it cuts VMOT power.    
gpioset gpiochip3 22=1      
sleep 0.1      
gpioset gpiochip3 22=0

### **5\. Klipper MCU USB Verification**

Once the MKS Monster8 is flashed, verify that the Orange Pi successfully detects it over the USB data line:

Bash  
\# List serial devices mapped by their unique hardware IDs    
ls /dev/serial/by-id/\*    
    
\# Expected output should look similar to:    
\# /dev/serial/by-id/usb-Klipper\_stm32f407xx\_2B0033000F50314B46313620-if00  

Note: Copy this serial path into the \[mcu\] section of your Klipper configuration.

### **6\. Hardware Audio Output Validation**

Initialize the I2S kernel driver and test digital audio output through DietPi:

1. **Enable I2S Device Tree Overlay:**  
   sudo nano /boot/armbianEnv.txt

    Add overlays=rk3566-i2s1-overlay to enable the RK3566 I2S1 peripheral tree. Execute a cold reboot: sudo reboot.  
2. **Verify ALSA Card Discovery:** Run aplay \-l to confirm the I2S digital DAC/Amplifier has been registered as a hardware sound device (e.g., card 1, device 0).  
3. **Configure Sound Routing:**  
   sudo nano /etc/asound.conf

    Paste the following configuration:  
4. Plaintext

pcm.\!default {      
    type hw      
    card 1      
    device 0      
}    

ctl.\!default {      
    type hw      
    card 1      
}

5. 

4\.  \*\*Test Playback:\*\*   
    Run \`speaker-test \-t sine \-f 440 \-c 1\` to generate a test sine wave\[cite: 1\].

---

## Related documentation

- [Master Software Architecture V9](Master%20Software%20Architecture%20V9.md) — product architecture, applications, co-processor protocol, implementation status
- [Master Architecture V4.9](Master%20Architecture%20V4.9.md) — power rails, safety interlocks, TMC2209 daisy chain, PCB lifecycle
- [Pi SD Image Software Build Guide](Pi%20SD%20Image%20Software%20Build%20Guide.md) — DietPi image, overlayfs, deploy
- `shared/protocol.h` / `shared/protocol.md` — Arduino ↔ Pi frame protocol

