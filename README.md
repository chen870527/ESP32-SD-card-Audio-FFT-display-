# ESP32 Dual-Core Audio Spectrum Visualizer

A professional, high-performance real-time audio visualizer utilizing the ESP32. This project solves the common stuttering and lag issues in audio playback by implementing a strict dual-core FreeRTOS architecture, hardware SPI isolation, and a custom lock-free ring buffer.

---

## System Showcase

### 1. Real-time Audio Visualizer
The ST7789 SPI LCD displays a fluid, 32-band audio spectrum perfectly synced with the music read from the SD card.
*(Note: You can replace this placeholder with a photo or GIF of the running device)*
![Dashboard](./images/dashboard.jpg)

---

## Architecture

We used a **Dual-core** setup here. Core 0 handles the high-priority I/O tasks like reading the SD card and continuously feeding the I2S audio amplifier, while Core 1 performs the heavy DSP (Fast Fourier Transform) and graphical rendering. This keeps the audio playing smoothly without any interruptions.

### 1. Dual-Core Task Management
![Dual Core System Architecture](./images/architecture.jpg)

### 2. Memory Allocation & Optimization
![Memory Allocation](./images/memory_layout.jpg)

---

## Key Features

*   **Hardware Bus Isolation**: Utilized dedicated HSPI and VSPI hardware to independently drive the TFT screen and SD card. This completely eliminates bus contention and ensures zero-collision data transfers.
*   **OS-Level Separation**: Leveraged FreeRTOS to pin time-critical I/O tasks (SD reading and I2S amplifier) to Core 0, while moving heavy mathematical operations (FFT) and screen rendering to Core 1.
*   **Lock-Free Ring Buffer**: Instead of relying on traditional Mutex locks for sharing audio data (which cause context switch delays), we wrote a custom lock-free ring buffer from scratch for an ultra-fast Producer-Consumer pattern.
*   **Delta Rendering Algorithm**: When drawing the audio spectrum, we record the previous frame's bar heights and only redraw the exact areas that changed. This localized update replaces traditional full-screen refreshes and slashes SPI data overhead by over 90% per frame.

---

## Hardware & Environment
*   **Dev Env**: PlatformIO (Arduino Framework)
*   **Hardware**: ESP32 Dual-core (ESP32-WROOM/WROVER), MAX98357A I2S Amplifier, ST7789 SPI Display, MicroSD Card Module, Arcade Button (Hardware Interrupt).

---
*Created by [chen870527](https://github.com/chen870527)*
