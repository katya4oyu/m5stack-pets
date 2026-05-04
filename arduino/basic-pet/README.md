# Arduino basic-pet

This sketch targets M5Stack CoreS3-Lite with M5Unified/M5GFX.

PNG files are stored in SPIFFS and decoded only when the current animation state
changes. Runtime playback uses the decoded RGB565 frame cache with
`M5.Display.pushImage()`.

Expected SPIFFS paths are shortened for Arduino SPIFFS filename limits:

```text
/p0/s0/00.png
/p1/s0/00.png
```

Generate PNG assets with:

```sh
mise run assets:build-display:all
```

Copy the generated `display-96-png` directories into the SPIFFS data image under
their pet IDs before flashing the filesystem.

Compile for CoreS3 with the M5Stack Arduino core and the SPIFFS partition
scheme used by this sketch:

```sh
mise run arduino:basic-pet:build
```

The default M5CoreS3 partition scheme is FATFS, so `SPIFFS.begin()` will fail
unless the sketch is built and uploaded with `PartitionScheme=factory_4apps`.
Use the repo tasks to upload both firmware and assets:

```sh
PORT=/dev/cu.usbmodemXXXX mise run arduino:basic-pet:upload
```
