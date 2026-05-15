# ESP32-CAM SOS Recording System Flow

## System Workflow

```text
Power ON
    ↓
initSD()
    ↓
SD Ready
    ↓
SOS detected
    ↓
startRecording()
    ↓
avi_open()
    ↓
Camera Task (Core 0)
    ↓
Capture JPEG frame (OV2640)
    ↓
Send frame → Queue
    ↓
SD Writer Task (Core 1)
    ↓
xSemaphoreTake(sdMutex)
    ↓
avi_write_frame()
    ↓
esp_camera_fb_return()
    ↓
monitorRecording()
    ↓
Check time (60 sec) OR stop signal
    ↓
avi_close()
    ↓
Recording stopped



✔ Implemented full ESP32-CAM SOS recording system

✔ Step 1:
- SD card initialization (initSD)
- File system test

✔ Step 2:
- Implemented recording start logic
- AVI file creation (avi_open)
- Start timer control

✔ Step 3:
- Camera frame capture using OV2640
- FreeRTOS camera task (Core 0)
- Queue-based frame transfer

✔ Step 4:
- SD writer task (Core 1)
- AVI frame writing system
- Mutex protection for SD safety

✔ Step 5:
- Recording monitor system
- 60-second auto stop
- SOS stop signal handling
- Safe AVI file closing
