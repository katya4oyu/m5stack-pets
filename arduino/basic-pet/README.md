# Arduino basic-pet

This sketch targets M5Stack CoreS3-Lite with M5Unified/M5GFX.

This sketch reads PNG assets from the CoreS3-Lite microSD slot. It uses the
same SD card directory contract as the ESP-IDF `basic-pet` project.

PNG files are decoded only when the current animation state changes. Runtime
playback uses the decoded RGB565 frame cache with
`M5.Display.pushImage()`.

Expected SD card paths:

```text
/assets/aomi/display-96-png/idle/00.png
/assets/bitomos-umi/display-96-png/review/00.png
```

Generate PNG assets with:

```sh
mise run assets:build-display:all
```

Copy the generated `display-96-png` directories to a FAT32 microSD card before
booting the sketch:

```sh
SDCARD="/Volumes/NO NAME" mise run arduino:basic-pet:copy-sd-assets
```

`SDCARD` is the PC-side mount path. The sketch mounts the card at `/sdcard`
internally regardless of the volume name.

Compile for CoreS3 with the M5Stack Arduino core:

```sh
mise run arduino:basic-pet:build
```

Use the repo task to upload firmware:

```sh
PORT=/dev/cu.usbmodemXXXX mise run arduino:basic-pet:upload
```
