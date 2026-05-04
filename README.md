# m5stack-pets

M5Stack CoreS3 Liteで小さなペット表現を試しながら、arduino-cli と ESP-IDF の開発に慣れるための実験リポジトリです。

## Projects

- `arduino/basic-pet`: arduino-cli版の最小サンプル
- `esp-idf/basic-pet`: ESP-IDF版の最小サンプル

## Target device

- M5Stack CoreS3 Lite
- MCU: ESP32-S3
- Display: 2.0 inch 320 x 240 ILI9342C
- Flash: 16MB
- PSRAM: 8MB

## First steps

Arduino版は `arduino-cli` から `arduino/basic-pet` を M5Stack CoreS3 ターゲットでビルドします。

```sh
mise run arduino:setup
mise run arduino:basic-pet:build
```

ESP-IDF版は公式ESP-IDF環境で次を実行します。

```sh
cd esp-idf/basic-pet
idf.py set-target esp32s3
idf.py build
idf.py flash monitor
```

## Assets and animation

CoreS3-Lite 向け `esp-idf/basic-pet` の画面・タッチ操作・asset runtime・検証観点は
[`docs/basic-pet-cores3-lite.md`](docs/basic-pet-cores3-lite.md) にまとめています。

Bitomos の spritesheet / 切り出しフレームを M5Stack CoreS3 で扱う方針は
[`docs/animation-guide.md`](docs/animation-guide.md) にまとめています。

`assets/` はローカルの Codex pet/run directory から取り込む作業場所です。実機 runtime では
`display-96-png` の PNG 形式を使います。
`source/` はローカル変換元なので Git 管理しません。

Codex pet の run directory を repo に取り込む場合は:

```sh
python3 tools/import-codex-pet.py \
  --run-dir /path/to/bitomos-umi-run \
  --pet-id bitomos-umi \
  --display-name "Bitomos Umi"
```

Pet animation の共通再生 helper は:

```cpp
#include "codex_pet_anim.h"
```

共通ライブラリは `libs/codex-pet-anim` に置いています。state/frame/duration は pet
ごとに変わりうるため、共通ライブラリには固定値を入れません。実機 runtime では JSON を
読まず、asset 生成時に作る C++ header を include します。

実機向けの PNG assets を事前生成する場合は:

```sh
python3 tools/build-display-assets.py \
  --pet-dir assets/bitomos-umi \
  --width 96 \
  --resample box \
  --force
```

このコマンドは `assets/bitomos-umi/display-96-png/bitomos_umi_anim.h` も生成します。

Arduino 版は SPIFFS ありの `factory_4apps` partition scheme でビルドし、PNG assets を SPIFFS image として
別途書き込みます。

```sh
PORT=/dev/cu.usbmodemXXXX mise run arduino:basic-pet:upload
```
