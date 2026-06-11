# **Master Software Architecture & Engineering Specification V8**

*Superseded for product behavior by [Master Software Architecture V9](Master%20Software%20Architecture%20V9.md); retained for co-processor, protocol, and hardware engineering detail.*

**Project:** Mobile Smart Braille Notetaker & Embosser

**Lead Architect:** Addison

**Target Hardware:** Orange Pi 3B (SBC) \+ Custom PCB (HAT)

**Operating System:** DietPi Linux (Debian 13 Trixie, Vendor Kernel 6.1.115 for maximum peripheral stability)

**Co-Processor:** Arduino Micro (ATmega32U4, 5V Logic)

## **1\. Unified Hardware I/O & The ScreenReader Paradigm**

---

The system utilizes a "ScreenReader UI Paradigm" (focus-based linear navigation). This unified architecture ensures that whether a user is deaf, blind, or deaf-blind, the underlying software logic remains identical, seamlessly routing the focused item to the user's preferred output method.

### **1.1 Universal Inputs (Concurrent Processing)**

Users can utilize multiple input methods simultaneously without locking out others:

* **Built-in Braille Keyboard:** Standard 6-key Perkins-style layout with a center D-pad (Up/Down) and specialized Action Keys.  
* **Backspace:** Located to the left of the D-pad.  
* **Enter:** Located to the right of the D-pad.  
* **Shift / TTS Control:** Located directly beneath the Enter key. Acts as a universal hardware interrupt to pause/resume speech synthesis (similar to NVDA screen-reader designs).  
* **Speech Button:** Universal Push-to-Talk for Vosk STT (Speech-to-Text) voice dictation input. Works globally in menus, naming prompts, and writing interfaces.  
* **Menu Button:** Instantly invokes the global system-wide overlay.  
* **Peripheral Keyboards:** Standard USB or Bluetooth QWERTY keyboards can be connected. System key mappings mirror desktop standards (e.g., Windows/Super Key \= Menu, Win \+ H \= Dictation).  
* **Refreshable Braille Displays:** Seamless integration via USB/Bluetooth (e.g., Mantis Q40) utilizing BRLTTY or a custom direct driver socket.

### **1.2 Production Keyboard Driver Specification (Direct Pin Topology)**

The traditional 4x4 switch matrix, steering diodes, and external 10kΩ pull-up resistors have been completely abandoned in favor of a Direct Pin Wiring topology (see Skeleton Prototype V5.1 Build Guide, Part 3.1). One side of every Cherry MX switch ties to a common ground bus; the other side routes to its own dedicated Arduino Micro input pin using the internal pull-up (INPUT\_PULLUP, active LOW). Because every key has its own pin, complete N-Key Rollover is inherent and phantom key ghosting is physically impossible — no diodes required:

1. **Scanning Routine:** The Arduino Micro samples all key pins once per millisecond from a non-blocking main loop (no delay() calls, no heavy ISR work that could starve the freefall interrupt).  
2. **Debounce Logic:** A per-key software integrator (15 ms threshold, tailored for Cherry MX switches) charges while a pin reads pressed and discharges while released; the debounced state flips only at the rails.  
3. **Chord Assembly Window:** When a Braille dot keypress is detected, the firmware opens a 40 ms temporal integration window. All dot presses occurring within this window are aggregated into a single discrete multi-finger chord event.

### **1.3 Universal Outputs (The Distribution Hub)**

Whenever the UI state changes or a word is focused, the "UI Output Hub" distributes the content to three parallel outputs simultaneously:

1. **TTS Engine (Speech):** Generates spoken feedback. This is routed via Rockchip I2S (I2S1 bus) to the **MAX98357A Class D Mono Amplifier** and integrated **4Ω 3W micro speaker capsule** for internal playback, or to the system's native 3.5mm headphone jack on the Orange Pi 3B for private listening. Audio is mixed and synthesized system-wide using eSpeak NG.  
2. **Tactile ScreenReader (Haptics):** Driven by the DRV2605L haptic driver and an LRA motor, converting visual cues, spelling errors, or navigation boundaries into distinct vibrotactile waveforms for deaf-blind users.  
3. **Embosser Feed (Dynamic Braille):** Drives the stepper arrays to physical paper.

## **2\. Co-Processor Hardware Integration & Real-Time Processing**

---

To ensure maximum operating system responsiveness and protect real-time processing tasks from OS scheduling jitter, all low-level, high-frequency physical I/O tasks are offloaded to the Arduino Micro co-processor.

\[13 Direct-Pin Tactile Keys\]      \[MPU6050 Accelerometer\]  
            │                               │  
            ▼ (1 kHz Polling Loop)           ▼ (Hardware Interrupt, Pin 7\)  
  ┌─────────────────────────────────────────────────────────────┐  
  │                 ARDUINO MICRO CO-PROCESSOR                  │  
  │ \- 15-ms Integrator Software Debounce                        │  
  │ \- 40-ms Temporal Chord Integration                          │  
  │ \- Real-time Freefall Protection (Interlock \<10ms)           │  
  └──────────────────────────────┬──────────────────────────────┘  
                                 │  
                                 ▼ (USB Virtual Serial CDC @ 115200 bps)  
                       \[Orange Pi 3B (SBC)\]

### **2.1 Microcontroller Direct-Pin Scanning & Debounce State Machine**

The Arduino Micro runs a bare-metal execution environment. The main loop samples all 13 key pins once per millisecond (1 kHz) without blocking; interrupt service routines are reserved exclusively for the freefall interlock so its sub-10 ms budget can never be starved by keyboard work.

* **The Scan Phase:** Each of the 13 keys (6 Braille dots, D-pad Up/Down, Backspace, Enter, Shift/TTS, Speech, Menu) is read directly from its dedicated pin (INPUT\_PULLUP, active LOW). With one pin per key there is no row strobing, no diode network, and no possibility of ghosting.  
* **The Debounce Phase:** The Arduino maintains an independent integrator counter per key. On each 1-ms tick the counter charges toward 15 while the raw pin reads pressed and discharges toward 0 while released; the debounced state changes only when a counter reaches a rail. This implements a non-blocking 15 ms debounce threshold tailored for Cherry MX switches.

### **2.2 Temporal Chord Assembly Loop**

Once individual key presses are debounced, they must be parsed as a unified Braille chord:

* When the first debounced dot key-down event is registered, a 40-ms temporal integration window timer is started.  
* During this 40-ms window, any other dot keys transitioning to a pressed state are aggregated into a single raw chord buffer.  
* Once the 40-ms integration window expires, the accumulated chord (e.g., Dots 1+3+5) is locked and transmitted to the Orange Pi as a CHORD frame carrying the raw dot bitmask. The Pi-side driver translates the dot mask to its character value, keeping the translation table host-side so Grade 2 / liblouis support can be added without reflashing firmware.  
* Function keys bypass the chord window entirely and are transmitted immediately as edge-triggered key-state frames.

## **3\. Bi-Directional CDC Serial Packet Protocol**

---

The interface between the Arduino Micro and the Orange Pi 3B utilizes a high-reliability, virtual serial port (CDC) over the physical USB interface running at 115200 bps (mapped to /dev/ttyACM0 in DietPi).

### **3.1 4-Byte Fixed-Width Binary Packet Specification**

To maximize transmission speeds, eliminate parsing overhead, and protect the limited 2.5 KB SRAM of the ATmega32U4 from memory fragmentation, the protocol utilizes highly optimized, fixed-width binary packets rather than text-based JSON/CSV strings.

Every transaction consists of exactly 4 bytes:

Byte 0: STX (0x02) ──► \[Start of Text Header\]    
Byte 1: STATUS ──► \[Bit flags indicating system events/state\]    
Byte 2: PAYLOAD ──► \[Dynamic event-dependent data value\]    
Byte 3: CHECKSUM ──► \[XOR Verification: (STATUS ^ PAYLOAD)\]  

**Status Register Bitmask Configuration (Byte 1):**

| Bit | Name | Description   |
| :---: | :---- | :---- |
| 7 | **CRITICAL\_ERROR** | 1 \= High-priority safety hazard / emergency event; 0 \= Standard state |
| 6 | **BATTERY\_ALERT** | 1 \= Battery capacity is below critical 5% threshold; 0 \= Nominal |
| 5 | **HAPTIC\_STATUS** | 1 \= DRV2605L haptic driver is active; 0 \= Driver is idle |
| 4 | **EVENT\_TYPE\_1** | High bit of Event Category (see mapping below) |
| 3 | **EVENT\_TYPE\_0** | Low bit of Event Category (see mapping below) |
| 2 | **RESERVED** | Reserved for future state expandability (defaults to 0\) |
| 1 | **RESERVED** | Reserved for future state expandability (defaults to 0\) |
| 0 | **DIRECTION** | 0 \= Upstream (Arduino to SBC); 1 \= Downstream (SBC to Arduino) |

**Event Category Mapping (Bits 4 & 3):**

* 00 \= System Keepalive / Telemetry Heartbeat  
* 01 \= Keyboard Input Event (Chord Assembled)  
* 10 \= Haptic Feedback Command  
* 11 \= Diagnostic / Configuration Parameter

### **3.2 Packet Encoding Examples**

**A. Assembled Braille Keypress (Letter "W")**

* Event: User presses Dots 2+4+5+6, yielding the letter "W" (ASCII 0x57).  
* Header (Byte 0): 0x02  
* Status (Byte 1): 0x08 (Event type 01 \[Keyboard Event\], Upstream direction 0\)  
* Payload (Byte 2): 0x57 (ASCII value)  
* Checksum (Byte 3): 0x08 ^ 0x57 \= 0x5F  
* Resulting Serial Byte Stream: \[0x02, 0x08, 0x57, 0x5F\]

**B. High-Priority Freefall Emergency Signal**

* Event: MPU6050 accelerometer registers a freefall event. Arduino isolates the motor rail and alerts the SBC.  
* Header (Byte 0): 0x02  
* Status (Byte 1): 0x80 (Critical Error Bit 1, Upstream direction 0\)  
* Payload (Byte 2): 0xFF (Specific code for Hard-Interlock Tripped)  
* Checksum (Byte 3): 0x80 ^ 0xFF \= 0x7F  
* Resulting Serial Byte Stream: \[0x02, 0x80, 0xFF, 0x7F\]

**C. Downstream Haptic Feedback Command (SBC to Arduino)**

* Event: ScreenReader UI encounters a menu boundary and requests the "Double Click" haptic pulse.  
* Header (Byte 0): 0x02  
* Status (Byte 1): 0x11 (Event type 10 \[Haptic Command\], Downstream direction 1\)  
* Payload (Byte 2): 0x03 (DRV2605L Library Waveform Pattern ID \#3)  
* Checksum (Byte 3): 0x11 ^ 0x03 \= 0x12  
* Resulting Serial Byte Stream: \[0x02, 0x11, 0x03, 0x12\]

### **3.3 Firmware-Level Developer Debug Mode**

To allow rapid, human-readable inspection on a test bench without breaking performance on production builds, a macro compilation switch is implemented within the Arduino IDE workspace:

\#define DEVELOPER\_DEBUG\_MODE false // Toggle to true during bench diagnostics    
    
void transmitPacket(uint8\_t status, uint8\_t payload) {    
 if (DEVELOPER\_DEBUG\_MODE) {    
 // Human-readable output formatted for standard ASCII Serial Monitors    
 Serial.print("TX\_DBG \>\> Header: STX | Status: 0x");    
 Serial.print(status, HEX);    
 Serial.print(" | Payload: 0x");    
 Serial.print(payload, HEX);    
 Serial.print(" | Checksum: 0x");    
 Serial.println((status ^ payload), HEX);    
 } else {    
 // High-efficiency, ultra-low-latency 4-byte production binary packet    
 uint8\_t binaryFrame\[4\];    
 binaryFrame\[0\] \= 0x02; // STX    
 binaryFrame\[1\] \= status; // Status byte    
 binaryFrame\[2\] \= payload; // Payload byte    
 binaryFrame\[3\] \= (uint8\_t)(status ^ payload); // XOR Checksum    
 Serial.write(binaryFrame, 4); // Send raw bytes directly    
 }    
}  

## **4\. Physical Safety & Power Distribution Architecture**

---

Maintaining deterministic physical safety boundaries is critical. Low-level safety monitoring tasks remain isolated within the hard-wired interrupts of the microcontroller.

┌───────────────────────────────┐    
 │ 14.8V Raw Power Input (LiPo)  │    
 └──────────────┬────────────────┘    
                │    
                ▼    
 ┌───────────────────────┐    
 │ IRLZ44N Power MOSFET  │ ◄───\[Power Gate\]    
 └───────────┬───────────┘    
                │    
                ▼    
 \[Stepper Driver Power Rails\]    
                ▲    
                │ (Hardware Interlock Gate Control Line)    
 ┌──────────────┴──────────────────────┐    
 │     ARDUINO MICRO CO-PROCESSOR      │    
 │ \- Reads MPU6050 Accelerometer       │    
 │ \- Detects Freefall Events           │    
 └─────────────────────────────────────┘  

### **4.1 Real-Time Hardware Interlock (MPU6050)**

* **Sensor Integration:** The MPU6050 6-axis IMU/accelerometer is connected directly to the Arduino Micro’s hardware I2C pins (SDA/SCL).  
* **Interrupt Binding:** The active-high interrupt output pin of the MPU6050 is tied directly to the Arduino's hardware interrupt pin INT0 (Pin 3).  
* **Hardware Interlock Drive:** An IRLZ44N N-channel logic-level Power MOSFET is placed in-series on the high-side of the main 14.8V stepper driver power rails supplying the TMC2209 chips. The gate of this MOSFET is driven by a high-current TC4420 Gate Driver connected directly to a dedicated GPIO pin on the Arduino Micro.

**The \<10ms Isolation Loop:**

1. If the MPU6050 detects a sudden freefall state (indicating the device has been dropped from a desk or slipped), its internal hardware engine immediately drives its interrupt pin high.  
2. The Arduino Micro captures the event instantly via INT0, bypassing any running software logic or matrix scanning.  
3. The interrupt service routine (ISR) immediately pulls the gate driver GPIO pin low, discharging the IRLZ44N gate and cutting all physical current to the 14.8V stepper and actuator rails in less than 10 milliseconds.  
4. Concurrently, the ISR transmits the high-priority safety packet (\[0x02, 0x80, 0xFF, 0x7F\]) to the Orange Pi to safely pause the software queue and warn the user.

### **4.2 Power Distribution & Logic Architecture**

* **SBC & Logic Power:** Power is supplied via a robust, TPS5430-based Synchronous Buck Regulator Module. This steps down the raw battery voltage directly to a highly filtered, clean 5V rail to power the Orange Pi 3B and Arduino Micro logic.  
* **Audio Power Separation:** The MAX98357A I2S Class D Mono Amplifier is powered directly from the 5V logic bus. To prevent Class D high-frequency switching noise and stepper motor inductive currents from polluting the 5V rail (which would cause stability anomalies in the Rockchip SoC), a dedicated dual-capacitor filtering array (470µF low-ESR electrolytic capacitor \+ 0.1µF ceramic capacitor) is placed in parallel directly at the VDD and GND terminals of the MAX98357A. This local capacitive buffer acts as an EMI shield and absorbs transient voltage ripples.  
* **Battery Telemetry:** A high-precision LTC2944 Battery Gas Gauge is tied directly to the system's main system I2C bus. It tracks battery capacity, output current, and voltage. If capacity drops beneath a designated 5% threshold, the Orange Pi triggers a safety interrupt to park the printhead, dump operational data to non-volatile flash, and execute a graceful system shutdown.

## **5\. Kinematics, Motion Control & Embossing Head Layout**

---

The mechanical layout requires exact, sub-millisecond coordination of the printer carriage, paper indexing, and pin solenoids to produce perfect, high-relief Braille patterns.

\[Carriage Assembly X-Axis Home Location\]    
                 │    
                 ├──► Row A (Solenoids 1, 3, 5\)    
                 │ │    
                 │ ▼ \[Separation Distance: Exactly 2.5 mm\]    
                 │ │    
                 └──► Row B (Solenoids 2, 4, 6\)  

### **5.1 Staggered Embossing Head Array**

Standard Braille cells consist of two parallel columns of three dots each (Left Column: Dots 1, 2, 3; Right Column: Dots 4, 5, 6). The physical embossing array uses a staggered spacing design:

* Row A (Top Row): Contains solenoids representing Dots 1, 3, and 5\.  
* Row B (Bottom Row): Contains solenoids representing Dots 2, 4, and 6\.  
* Spatial Offset: Row A and Row B are separated by a physical distance of exactly 2.5 mm along the X-axis (carriage path).  
* Software-Driven Mechanical Phase Offset: As the carriage travels horizontally at a constant speed, the motion controller application on the Orange Pi must not fire all solenoids simultaneously. Instead, the controller fires Row A solenoids as they cross the coordinates for the current Braille character's column, buffers the active data for the corresponding Row B solenoids, and, based on the real-time velocity of the carriage, calculates the exact microsecond delay required for the carriage to travel 2.5 mm, then fires the Row B solenoids. This aligns the two halves of the Braille cells perfectly with zero mechanical skew.

### **5.2 Stepper Driver Tuning (TMC2209)**

* **SilentStepping:** TMC2209 stepper drivers are utilized to control both the X-axis (carriage movement) and Y-axis (paper feed) motors.  
* **Homing and Calibration:**  
  * X-Axis: A high-accuracy TCST2103 Optical Slot Sensor acts as the physical Y-axis homing limit switch.  
  * Y-Axis: A TCRT5000 Reflective Infrared Sensor is directed at the continuous cardstock path to detect the physical page boundaries, tracking analog reflection changes to handle automatic paper feed alignment.

## **6\. Core ScreenReader UI Framework & File System Configuration**

---

The core user experience is managed by the high-level Python UI engine on the Orange Pi 3B. To ensure reliability over years of hard field deployment, the underlying OS configuration must be highly resilient against corruption.

### **6.1 DietPi Read-Only Overlay Architecture**

To prevent SD card/eMMC corruption from sudden battery depletion or hard hardware cutoffs, DietPi is configured to run on a read-only root partition:

* **The Overlay File System:** The root directory (/) is mounted as a read-only system partition. A fast RAM-based overlay partition (overlayfs) captures all active system logging and ephemeral files in volatile RAM workspace.  
* **Persistent Storage Boundaries:** A separate physical flash partition on the internal drive is mounted as a dedicated read-write workspace (/data/). This partition is reserved solely for user files, documents, and settings.  
* **Safe Serialization Daemon:** When a user modifies a file, changes are written to a RAM-based buffer. A dedicated background daemon regularly wakes up to synchronize those modifications to the /data/ partition using atomic transactions. This guarantees that if the hardware safety interlock drops the system's power rail, the core operating system files remain uncorrupted.

## **7\. Standardized Core Hardware Reference**

---

To eliminate structural ambiguities during physical PCB design and final HAT routing, the core hardware components are standardized as follows:

| Subsystem Component | Standardized Hardware Chip / Board Reference   |
| :---- | :---- |
| **SBC (Primary Processor)** | Orange Pi 3B (4GB LPDDR4, Rockchip RK3566, integrated WiFi/BT) |
| **Microcontroller (Co-Processor)** | Arduino Micro (ATmega32U4, 5V Logic, Native USB Controller) |
| **Logic Power Supply** | Synchronous Buck Regulator Module (TPS5430-based, high-efficiency) |
| **Safety Interlock Switch** | IRLZ44N N-Channel Power MOSFET \+ TC4420 High-Current Gate Driver |
| **Battery BMS** | Lithium Polymer 4S (14.8V) BMS with active cell balancing |
| **Battery Gas Gauge** | LTC2944 (I2C-compatible high-voltage coulomb counter) |
| **Audio Amplifier (Internal)** | **MAX98357A I2S Class D Mono Amplifier (Direct Rockchip DAC Bypass)** |
| **Internal Speaker** | **4Ω 3W or 8Ω 2W Enclosed Micro Speaker Capsule (foam-isolated mounting)** |
| **Audio Power Filter Array** | **Parallel Array: 470µF Low-ESR Electrolytic \+ 0.1µF Ceramic Capacitors** |
| **Haptic Driver Module** | DRV2605L I2C Haptic Driver Module |
| **Haptic Actuator** | LRA (Linear Resonant Actuator) High-Relief Vibration Motor |
| **Carriage Paper Sensor** | TCRT5000 Reflective Infrared Sensor (Analog/Digital feedback) |
| **Y-Axis Homing Endstop** | TCST2103 Optical Slot Sensor (Transmissive Photointerrupter) |
| **Keyboard Anti-Ghost Diodes** | Fast-Switching Axial Diodes (1N4148 or equivalent) |

