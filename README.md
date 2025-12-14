# ESP32-S3 Video Player

This project is a video player implementation for the **Waveshare ESP32-S3-Touch-LCD-1.83** development board. It plays MJPEG-encoded AVI files from a microSD card with synchronized audio.

![Waveshare ESP32-S3-Touch-LCD-1.83](https://www.waveshare.com/media/catalog/product/cache/1/image/800x800/9df78eab33525d08d6e5fb8d27136e95/e/s/esp32-s3-touch-lcd-1.83-1.jpg)

## Features

*   **Smooth Playback:** Optimized for ~30 FPS video playback.
*   **Audio Support:** Plays audio via the onboard speaker (PCM 22050Hz Mono).
*   **Touch Controls:**
    *   Tap screen to Pause / Resume.
    *   On-screen volume control button.
*   **Playback Control:**
    *   **Short Press BOOT:** Pause / Resume playback.
    *   **Double Press BOOT:** Next track.
    *   **Long Press BOOT:** Reload file list and restart playback (useful after changing SD card).
*   **Hot Reload:** Supports hot-swapping the SD card.
*   **Error Handling:** Displays a user-friendly error screen if the SD card is removed during playback.

## Hardware

*   **Device:** [Waveshare ESP32-S3-Touch-LCD-1.83](https://www.waveshare.com/esp32-s3-touch-lcd-1.83.htm)
*   **Display:** 1.83inch Capacitive Touch Display, 240×284 resolution.
*   **Storage:** MicroSD Card (formatted with FAT32).

## Video Format

The player requires videos to be converted to a specific format (MJPEG video, PCM audio) and pre-rotated for optimal performance. A Python script `convert_videos.py` is provided in the `scripts/` folder to automate this process using FFmpeg.

```bash
python scripts/convert_videos.py
```

## How to Use

1.  Flash the firmware to the ESP32-S3.
2.  Prepare a microSD card with a `videos` folder containing converted `.avi` files.
3.  Insert the SD card into the device.
4.  The player will automatically start looping through the videos.
5.  **Controls:**
    *   **Touch Screen:** Tap anywhere to Pause/Resume.
    *   **Volume:** Tap the speaker icon in the top-left corner to adjust volume.
    *   **BOOT Button:**
        *   Short Press: Pause / Resume.
        *   Double Press: Next Track.
        *   Hold (approx. 1s): Reload SD card and restart playback.
