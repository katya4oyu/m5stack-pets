# m5stack-pets

Codex app の Codex pets（`hatch-pet` skill で作成したカスタム pet）を M5Stack CoreS3 Lite で表示するデモリポジトリです。

## Requirements

ローカルに次のコマンドが必要です。

- `mise`: このリポジトリの task runner
- `arduino-cli`: Arduino 版の build / upload / monitor
- `eim`: ESP-IDF v5.5 の install / build / flash

初回セットアップは mise task から実行します。

```sh
mise install
mise run setup
```

`mise install` は Python / Ninja / `uv` を用意します。`mise run setup` は M5Stack Arduino core / `M5Unified`、ESP-IDF v5.5 をセットアップします。

## Hatch Pet asset flow

このリポジトリで重要なのは、Codex app の `hatch-pet` skill で作成した pet 素材を取り込み、M5Stack で扱いやすい runtime asset に変換する流れです。

1. `hatch-pet` skill の run directory を `assets/<pet-id>/source` に取り込む
2. `source/frames` の 192 x 208 PNG frames を M5Stack 表示向けの `display-96-png` に縮小変換する
3. 変換時に実機 runtime で include する C++ header も生成する
4. Arduino 版では `display-96-png` を SPIFFS image にして書き込む

新しい Hatch Pet run を取り込んで変換する場合は:

```sh
RUN_DIR=/path/to/bitomos-umi-run \
PET_ID=bitomos-umi \
DISPLAY_NAME="Bitomos Umi" \
FORCE=1 \
mise run assets:import-and-build
```

取り込み済み pet の表示用 asset だけを作り直す場合は:

```sh
PET_ID=bitomos-umi FORCE=1 mise run assets:build-display
```

curated assets をまとめて再生成する場合は:

```sh
mise run assets:build-display:all
```

`assets/<pet-id>/source` は Hatch Pet run 由来のローカル変換元です。通常は Git 管理せず、実機 runtime では `assets/<pet-id>/display-96-png` の PNG frames と生成 header を使います。

## Build and upload

Arduino 版:

```sh
mise run arduino:basic-pet:build
PORT=/dev/cu.usbmodemXXXX mise run arduino:basic-pet:upload
```

ESP-IDF 版:

```sh
mise run esp:basic-pet:set-target
mise run esp:basic-pet:build
PORT=/dev/cu.usbmodemXXXX mise run esp:basic-pet:flash-monitor
```

## Projects

- `arduino/basic-pet`: arduino-cli 版の最小サンプル
- `esp-idf/basic-pet`: ESP-IDF 版の最小サンプル

## Target device

- M5Stack CoreS3 Lite
- MCU: ESP32-S3
- Display: 2.0 inch 320 x 240 ILI9342C
- Flash: 16MB
- PSRAM: 8MB

## Docs

CoreS3-Lite 向け `esp-idf/basic-pet` の画面・タッチ操作・asset runtime・検証観点は
[`docs/basic-pet-cores3-lite.md`](docs/basic-pet-cores3-lite.md) にまとめています。

Bitomos の spritesheet / 切り出しフレームを M5Stack CoreS3 で扱う方針は
[`docs/animation-guide.md`](docs/animation-guide.md) にまとめています。

Pet animation の共通再生 helper は:

```cpp
#include "codex_pet_anim.h"
```

共通ライブラリは `libs/codex-pet-anim` に置いています。state/frame/duration は pet
ごとに変わりうるため、共通ライブラリには固定値を入れません。実機 runtime では JSON を
読まず、asset 生成時に作る C++ header を include します。
