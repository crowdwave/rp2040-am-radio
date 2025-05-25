# Ubuntu Build Instructions: RP2040 Comprehensive AM Transmitter

## Complete Step-by-Step Guide for Ubuntu 20.04/22.04/24.04

This guide provides detailed instructions to build the comprehensive AM transmitter on Ubuntu from scratch.

---

## 🔧 **Step 1: Install Dependencies**

### **Update System**
```bash
sudo apt update
sudo apt upgrade -y
```

### **Install Build Tools**
```bash
# Essential build tools
sudo apt install -y \
    build-essential \
    cmake \
    gcc-arm-none-eabi \
    libnewlib-arm-none-eabi \
    libstdc++-arm-none-eabi-newlib \
    git \
    python3 \
    python3-pip

# Additional useful tools
sudo apt install -y \
    minicom \
    picocom \
    tree \
    wget \
    curl \
    unzip
```

### **Verify Toolchain Installation**
```bash
# Check ARM compiler
arm-none-eabi-gcc --version
# Should show: arm-none-eabi-gcc (GNU Arm Embedded Toolchain) 10.3.1 or newer

# Check CMake
cmake --version
# Should show: cmake version 3.16.3 or newer

# Check Git
git --version
# Should show: git version 2.25.1 or newer
```

---

## 📦 **Step 2: Install Pico SDK**

### **Download Pico SDK**
```bash
# Navigate to home directory
cd ~

# Clone Pico SDK with all submodules
git clone --recursive https://github.com/raspberrypi/pico-sdk.git

# Verify download
ls -la pico-sdk/
# Should show multiple directories including src/, external/, etc.
```

### **Set Environment Variables**
```bash
# Add to current session
export PICO_SDK_PATH=~/pico-sdk

# Make permanent by adding to ~/.bashrc
echo 'export PICO_SDK_PATH=~/pico-sdk' >> ~/.bashrc

# Reload bashrc
source ~/.bashrc

# Verify environment variable
echo $PICO_SDK_PATH
# Should show: /home/yourusername/pico-sdk
```

### **Build SDK Tools**
```bash
# Navigate to SDK
cd ~/pico-sdk

# Build pioasm (PIO assembler)
cd tools/pioasm
mkdir build && cd build
cmake ..
make -j$(nproc)

# Add to PATH (optional but recommended)
echo 'export PATH=$PATH:~/pico-sdk/tools/pioasm/build' >> ~/.bashrc
source ~/.bashrc

# Test pioasm
pioasm --version
# Should show: pioasm version information
```

---

## 📁 **Step 3: Create Project**

### **Create Project Directory**
```bash
# Create project directory
mkdir -p ~/projects/rp2040_am_transmitter
cd ~/projects/rp2040_am_transmitter

# Verify current directory
pwd
# Should show: /home/yourusername/projects/rp2040_am_transmitter
```

### **Create Source Files**

**Create main program file:**
```bash
cat > comprehensive_am_transmitter.c << 'EOF'
// Paste the complete comprehensive_am_transmitter.c content here
// (This would be the full content from the artifacts)
EOF
```

**Create basic PIO program:**
```bash
cat > am_carrier.pio << 'EOF'
; am_carrier.pio
; High-precision AM carrier generation using RP2040 PIO

.program am_carrier

; PIO program for generating amplitude-modulated carrier
; Input format: 32-bit word with high_time (upper 16 bits) and low_time (lower 16 bits)
; Output: Square wave with variable duty cycle for AM

.wrap_target
    pull block          ; Pull 32-bit modulation data from FIFO
    out y, 16          ; Extract high_time (upper 16 bits) to Y register
    out x, 16          ; Extract low_time (lower 16 bits) to X register
    
    set pins, 1        ; Set output pin HIGH
high_loop:
    jmp y--, high_loop ; Decrement Y and loop while Y > 0 (high period)
    
    set pins, 0        ; Set output pin LOW  
low_loop:
    jmp x--, low_loop  ; Decrement X and loop while X > 0 (low period)
.wrap

% c-sdk {
// Helper function to calculate PIO clock divider
static inline void am_carrier_program_init(PIO pio, uint sm, uint offset, uint pin, float freq) {
    pio_sm_config c = am_carrier_program_get_default_config(offset);
    
    // Configure pin as output
    sm_config_set_set_pins(&c, pin, 1);
    sm_config_set_out_pins(&c, pin, 1);
    
    // Calculate clock divider for desired carrier frequency
    float div = (float)clock_get_hz(clk_sys) / (freq * 2); // *2 because of high+low periods
    sm_config_set_clkdiv(&c, div);
    
    // Configure output shifting for 32-bit data
    sm_config_set_out_shift(&c, false, true, 32);
    
    // Join FIFOs for more buffering
    sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_TX);
    
    // Initialize GPIO
    pio_gpio_init(pio, pin);
    pio_sm_set_consecutive_pindirs(pio, sm, pin, 1, true);
    
    // Load and start the program
    pio_sm_init(pio, sm, offset, &c);
    pio_sm_set_enabled(pio, sm, true);
}
%}
EOF
```

**Create advanced PIO program:**
```bash
cat > advanced_am_carrier.pio << 'EOF'
; advanced_am_carrier.pio
; Advanced harmonic reduction using multi-bit output and precise timing

.program advanced_am_carrier

; Advanced PIO program for harmonic reduction
; Input format: 32-bit word with timing data for variable pulse width
; Output: Multi-bit digital signal with reduced harmonics

.wrap_target
    pull block          ; Pull timing data from FIFO
    
    ; Extract timing components
    mov y, osr         ; Copy OSR to Y
    out x, 16          ; Extract high_time (upper 16 bits) to X
    mov osr, y         ; Restore OSR
    out y, 16          ; Extract low_time (lower 16 bits) to Y
    
    ; Generate variable pulse width for amplitude control
    set pins, 0b1111   ; Set all 4 output pins HIGH (multi-bit)
high_pulse:
    jmp x--, high_pulse ; High period with variable width
    
    set pins, 0b0000   ; Set all output pins LOW
low_pulse:
    jmp y--, low_pulse  ; Low period
    
.wrap

% c-sdk {
// Advanced PIO initialization
static inline void advanced_am_carrier_program_init(PIO pio, uint sm, uint offset, 
                                                   uint pin_base, uint pin_count, float freq) {
    pio_sm_config c = advanced_am_carrier_program_get_default_config(offset);
    
    // Configure multi-bit output pins
    sm_config_set_out_pins(&c, pin_base, pin_count);
    sm_config_set_set_pins(&c, pin_base, pin_count);
    
    // Calculate clock divider
    float div = (float)clock_get_hz(clk_sys) / (freq * 64);
    sm_config_set_clkdiv(&c, div);
    
    sm_config_set_out_shift(&c, false, true, 32);
    sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_TX);
    
    // Initialize GPIO pins
    for (uint pin = pin_base; pin < pin_base + pin_count; pin++) {
        pio_gpio_init(pio, pin);
        pio_sm_set_consecutive_pindirs(pio, sm, pin, 1, true);
    }
    
    pio_sm_init(pio, sm, offset, &c);
    pio_sm_set_enabled(pio, sm, true);
}
%}
EOF
```

**Create CMakeLists.txt:**
```bash
cat > CMakeLists.txt << 'EOF'
cmake_minimum_required(VERSION 3.13)

# Include build functions from Pico SDK
include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)

project(comprehensive_am_transmitter C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# Initialize the SDK
pico_sdk_init()

# Generate PIO headers from assembly files
pico_generate_pio_header(comprehensive_am_transmitter ${CMAKE_CURRENT_LIST_DIR}/am_carrier.pio)
pico_generate_pio_header(comprehensive_am_transmitter ${CMAKE_CURRENT_LIST_DIR}/advanced_am_carrier.pio)

add_executable(comprehensive_am_transmitter
    comprehensive_am_transmitter.c
)

# Link required libraries
target_link_libraries(comprehensive_am_transmitter 
    pico_stdlib
    hardware_dma
    hardware_pio
    hardware_clocks
    hardware_gpio
    hardware_interp
    pico_multicore
    pico_fatfs
)

# Enable USB output, disable UART output for serial
pico_enable_stdio_usb(comprehensive_am_transmitter 1)
pico_enable_stdio_uart(comprehensive_am_transmitter 0)

# Create map/bin/hex/uf2 file etc.
pico_add_extra_outputs(comprehensive_am_transmitter)

# Add compiler definitions
target_compile_definitions(comprehensive_am_transmitter PRIVATE
    PICO_STDIO_UART=0
    PICO_STDIO_USB=1
    PICO_STACK_SIZE=0x2000
    PICO_CORE1_STACK_SIZE=0x1000
)
EOF
```

### **Verify Project Structure**
```bash
# Check all files are present
tree .
# Should show:
# .
# ├── CMakeLists.txt
# ├── advanced_am_carrier.pio
# ├── am_carrier.pio
# └── comprehensive_am_transmitter.c

# Check file sizes (all should be > 0)
ls -lh
```

---

## 🔨 **Step 4: Build Project**

### **Create Build Directory**
```bash
# Create and enter build directory
mkdir build
cd build

# Verify we're in the right place
pwd
# Should show: /home/yourusername/projects/rp2040_am_transmitter/build
```

### **Configure with CMake**
```bash
# Configure the build
cmake ..

# Expected output should include:
# -- The C compiler identification is GNU 10.3.1
# -- The CXX compiler identification is GNU 10.3.1
# -- Build files have been written to: /path/to/build
```

**If CMake fails, check:**
```bash
# Verify PICO_SDK_PATH
echo $PICO_SDK_PATH

# Check SDK exists
ls $PICO_SDK_PATH/pico_sdk_init.cmake

# Re-run if needed
rm -rf *
cmake ..
```

### **Build the Project**
```bash
# Build using all CPU cores
make -j$(nproc)

# Monitor build progress
# Should see compilation of:
# - PIO programs
# - Main C file  
# - Linking
# - UF2 generation
```

### **Verify Build Success**
```bash
# Check for output files
ls -la *.uf2
# Should show: comprehensive_am_transmitter.uf2

# Check file size (should be reasonable, e.g., 100KB+)
ls -lh comprehensive_am_transmitter.uf2

# Optional: Check build artifacts
ls -la
# Should show various .bin, .elf, .hex files
```

---

## 📱 **Step 5: Flash to Pico**

### **Prepare Pico for Programming**
```bash
# Connect Pico via USB while holding BOOTSEL button
# OR press BOOTSEL button while connected

# Check if Pico is detected
lsusb | grep -i "raspberry"
# Should show: Bus XXX Device XXX: ID 2e8a:0003 Raspberry Pi RP2 Boot

# Check for mounted drive
ls /media/$USER/
# Should show: RPI-RP2 or similar
```

### **Flash Firmware**
```bash
# Copy UF2 file to Pico
cp comprehensive_am_transmitter.uf2 /media/$USER/RPI-RP2/

# Pico will automatically reboot and start the program
# The RPI-RP2 drive will disappear
```

### **Alternative Flash Method (if mount fails)**
```bash
# Install picotool (optional but useful)
sudo apt install picotool

# Or build from source:
git clone https://github.com/raspberrypi/picotool.git
cd picotool
mkdir build && cd build
cmake ..
make -j$(nproc)
sudo make install

# Flash using picotool
picotool load comprehensive_am_transmitter.uf2
picotool reboot
```

---

## 📺 **Step 6: Test and Monitor**

### **Connect Serial Monitor**
```bash
# Find the Pico's serial device
ls /dev/ttyACM*
# Should show: /dev/ttyACM0 (or similar)

# Connect using minicom
sudo minicom -D /dev/ttyACM0 -b 115200

# OR using picocom
picocom /dev/ttyACM0 -b 115200

# OR using screen
screen /dev/ttyACM0 115200
```

### **Expected Serial Output**
```
RP2040 Comprehensive AM Transmitter
===================================
Educational demonstration of harmonic suppression
using RP2040 special processors

Configuration:
- Station: 3LO (ABC Melbourne)
- Frequency: 774.0 kHz
- Signal Mode: Simple High Quality
- Modulation Depth: 80%
- WAV File: audio.wav

🚨 SAFETY CHECK REQUIRED 🚨
===========================
This transmitter is for EDUCATIONAL USE ONLY
You MUST use a dummy load, NOT an antenna

Are you using a dummy load (NOT antenna)? (y/N):
```

### **Basic Test Commands**
```bash
# Exit serial monitor (Ctrl+A, then X for minicom)
# Or Ctrl+A, then Ctrl+\ for screen

# Test with different options (after reconnecting USB)
# Note: You'll need to create a simple script to pass arguments
# since the Pico doesn't have command line args directly

# For now, the program uses built-in defaults
# Modify the source code to test different configurations
```

---

## 📁 **Step 7: Prepare SD Card and Audio**

### **Format SD Card**
```bash
# Insert SD card and find device
lsblk
# Look for your SD card (e.g., sdc1)

# Unmount if mounted
sudo umount /dev/sdc1

# Format as FAT32
sudo mkfs.vfat -F 32 /dev/sdc1

# Create mount point and mount
sudo mkdir -p /mnt/sdcard
sudo mount /dev/sdc1 /mnt/sdcard
sudo chmod 777 /mnt/sdcard
```

### **Create Test Audio Files**
```bash
# Install audio tools
sudo apt install sox ffmpeg

# Create test tone (1 kHz, 10 seconds)
sox -n -r 44100 -b 16 /mnt/sdcard/tone_1khz.wav synth 10 sine 1000

# Create frequency sweep (200 Hz to 4 kHz, 10 seconds)
sox -n -r 44100 -b 16 /mnt/sdcard/sweep.wav synth 10 sine 200-4000

# Convert existing audio file
ffmpeg -i input.mp3 -ar 44100 -ac 1 -f wav /mnt/sdcard/audio.wav

# Create default file
cp /mnt/sdcard/tone_1khz.wav /mnt/sdcard/audio.wav

# Unmount SD card
sudo umount /mnt/sdcard
```

---

## 🔧 **Troubleshooting**

### **Common Build Issues**

**"PICO_SDK_PATH not found":**
```bash
# Check environment variable
echo $PICO_SDK_PATH

# If empty, set it:
export PICO_SDK_PATH=~/pico-sdk
echo 'export PICO_SDK_PATH=~/pico-sdk' >> ~/.bashrc

# Clean and rebuild
rm -rf build/*
cd build
cmake ..
make -j$(nproc)
```

**"arm-none-eabi-gcc not found":**
```bash
# Reinstall toolchain
sudo apt remove gcc-arm-none-eabi
sudo apt autoremove
sudo apt install gcc-arm-none-eabi libnewlib-arm-none-eabi

# Verify installation
arm-none-eabi-gcc --version
```

**"Permission denied" on /dev/ttyACM0:**
```bash
# Add user to dialout group
sudo usermod -a -G dialout $USER

# Log out and log back in, or:
newgrp dialout

# Try serial connection again
minicom -D /dev/ttyACM0 -b 115200
```

### **Hardware Issues**

**Pico not detected:**
```bash
# Check USB connection
lsusb | grep -i raspberry

# Try different USB cable/port
# Press and hold BOOTSEL while connecting
# Check dmesg for connection messages
dmesg | tail
```

**No serial output:**
```bash
# Verify correct device
ls -la /dev/ttyACM*

# Check if other programs using port
sudo lsof | grep ttyACM

# Try different baud rate
minicom -D /dev/ttyACM0 -b 9600
```

---

## 📋 **Quick Build Script**

Create an automated build script:

```bash
cat > build.sh << 'EOF'
#!/bin/bash

echo "RP2040 AM Transmitter Build Script"
echo "================================="

# Check environment
if [ -z "$PICO_SDK_PATH" ]; then
    echo "Error: PICO_SDK_PATH not set"
    echo "Run: export PICO_SDK_PATH=~/pico-sdk"
    exit 1
fi

# Clean previous build
echo "Cleaning previous build..."
rm -rf build/*

# Configure
echo "Configuring with CMake..."
cd build
cmake .. || { echo "CMake failed"; exit 1; }

# Build
echo "Building project..."
make -j$(nproc) || { echo "Build failed"; exit 1; }

# Check output
if [ -f "comprehensive_am_transmitter.uf2" ]; then
    echo "Build successful!"
    echo "Output: comprehensive_am_transmitter.uf2"
    ls -lh comprehensive_am_transmitter.uf2
else
    echo "Build failed - no UF2 file generated"
    exit 1
fi

echo ""
echo "To flash:"
echo "1. Hold BOOTSEL and connect Pico via USB"
echo "2. Copy comprehensive_am_transmitter.uf2 to RPI-RP2 drive"
echo "3. Connect serial monitor to /dev/ttyACM0"
EOF

chmod +x build.sh

# Run the build script
./build.sh
```

---

## ✅ **Verification Checklist**

Before first use, verify:

- [ ] **Build completes** without errors
- [ ] **UF2 file created** (comprehensive_am_transmitter.uf2)
- [ ] **Pico detected** in BOOTSEL mode (`lsusb` shows Raspberry Pi)
- [ ] **Firmware uploads** successfully (RPI-RP2 drive disappears after copy)
- [ ] **Serial connection** works (`/dev/ttyACM0` responds)
- [ ] **Program starts** (shows startup messages)
- [ ] **SD card detected** (if using audio files)
- [ ] **Safety check** prompts appear
- [ ] **LEDs functional** (if connected)
- [ ] **RF output present** (with oscilloscope on dummy load)

Your RP2040 AM transmitter is now ready for educational use!

---

## 📖 **Next Steps**

1. **Hardware Setup**: Connect SD card module and dummy load
2. **Signal Testing**: Use oscilloscope to verify RF output  
3. **Audio Testing**: Try different WAV files
4. **Educational Experiments**: Explore different signal modes
5. **Advanced Features**: Study filtering and harmonic reduction

The transmitter is now built and ready for educational RF signal processing experiments!