# CoreS3-Lite basic-pet 設計ドキュメント

このドキュメントは、M5Stack CoreS3-Lite 向け `esp-idf/basic-pet` の v1 設計と
実装引き継ぎメモです。後続の実装者は、この内容を source of truth として実機確認や
改善を進めてください。

## 目的

`assets/aomi/display-96-png` と `assets/bitomos-umi/display-96-png` にある Codex pet 由来の
runtime asset を、CoreS3-Lite 上で再生できる最小サンプルを作ります。

v1 のゴールは次の通りです。

- 2 種類の pet を切り替えられる。
- generated header の state 順に従って state を切り替えられる。
- pet 本体タップは pet 切替に使わず、reaction 用に予約する。
- 実機 UI は日本語フォントに依存しない。
- runtime では JSON を parse しない。

## 対象環境

- Device: M5Stack CoreS3-Lite
- MCU: ESP32-S3
- Display: 320 x 240 landscape
- Runtime: ESP-IDF + M5Unified + M5GFX
- 推奨 ESP-IDF: v5.5
- 確認済み主要 component:
  - `espressif/m5stack_core_s3`: 3.0.2
  - `lvgl/lvgl`: 9.2.2

CoreS3-Lite には touch, IMU, speaker, microphone, proximity / ambient light
sensor などがありますが、v1 では touch のみを使います。

## 画面設計

画面は 320 x 240 固定想定です。

```text
+--------------------------------+
| Aomi | idle                    |
|                                |
|                                |
|           [  pet  ]            |
|           96 x 104             |
|                                |
|                                |
+----------+----------+----------+
|    <     |    *     |    >     |
+----------+----------+----------+
```

表示ルール:

- 左上に現在の pet と state を表示する。
  - format: `<pet> | <state>`
  - examples: `Aomi | idle`, `Bitomos | review`
- 中央に pet frame を表示する。
  - frame size: 96 x 104
- フッターは画面下部 48 px。
- フッターは 3 分割する。
  - left: `<`
  - center: `*`
  - right: `>`
- pet 本体の周辺にはラベルを出さない。
- 実機 UI では日本語を出さない。

## タッチ操作

フッターと pet 本体で役割を分けます。

| Area | Gesture | Behavior |
| --- | --- | --- |
| `<` | short tap | previous state |
| `>` | short tap | next state |
| `*` | short tap | no-op |
| `*` | long press | switch pet |
| pet body | short tap | reaction state |

設計意図:

- pet 本体 tap を pet 切替に使わない。
- pet 本体操作は「なでる」「反応する」系の余地として残す。
- 現在の asset には `petting`, `pat`, `stroke` のような明示的 state はない。
- v1 では pet body tap の reaction として `waving` に切り替える。
- 将来 `petting` 相当の state が追加されたら、pet body tap は `waving` ではなくその state に差し替える。

## Pet と State

v1 で扱う pet は 2 種類です。

| Pet id | 表示名 | Generated header |
| --- | --- | --- |
| `aomi` | `Aomi` | `assets/aomi/display-96-png/aomi_anim.h` |
| `bitomos-umi` | `Bitomos` | `assets/bitomos-umi/display-96-png/bitomos_umi_anim.h` |

state 順は generated header の `k_states` を source of truth にします。現在の asset では
両 pet とも同じ state を持ちます。

```text
idle
running-right
running-left
waving
jumping
failed
waiting
running
review
```

state 切替:

- `<`: current index - 1。先頭なら末尾へ循環する。
- `>`: current index + 1。末尾なら先頭へ循環する。
- pet 切替時は現在の state index を維持する。
- 将来 pet ごとに state 数が違う場合、範囲外 index は `codex_pet::makePlayer()` の fallback により 0 へ戻す。

v1 ではすべての state を loop 再生します。`waving`, `jumping`, `failed` を一度だけ再生して
`idle` へ戻す挙動は v2 以降の改善対象です。

## Asset Runtime

runtime では JSON を読みません。pet 固有の state/frame/duration/file 情報は generated
header から取得します。

使用する共通 helper:

```cpp
#include "codex_pet_anim.h"
```

使用する generated header:

```cpp
#include "aomi_anim.h"
#include "bitomos_umi_anim.h"
```

`display-96-png` asset の形式:

- `<state>/<nn>.png`
  - state ごとの 96 x 104 PNG frame
  - state 切替時に decode して RGB565 frame cache に置く
- `*_anim.h`
  - `codex_pet::PetSpec`
  - `codex_pet::StateInfo`
  - frame size
  - state duration
- `manifest.json`
  - build / inspection 用
  - firmware runtime では読まない

描画時は current state の PNG frames を state 切替時に decode して cache します。
再生中は PNG decode を行いません。

## Firmware 構成

主なファイル:

- `esp-idf/basic-pet/main/main.cpp`
  - UI 作成
  - SPIFFS mount
  - frame 読み込み
  - PNG decode
  - RGB565 frame cache rendering
  - touch event handling
  - animation task
- `esp-idf/basic-pet/main/CMakeLists.txt`
  - `main.cpp`
  - `libs/codex-pet-anim/include`
  - generated header include path
- `esp-idf/basic-pet/CMakeLists.txt`
  - `display-96-png` asset を build directory に staging
  - SPIFFS image 作成
- `esp-idf/basic-pet/partitions.csv`
  - 16MB flash 向け custom partition table
- `esp-idf/basic-pet/sdkconfig.defaults`
  - PSRAM: enabled, quad mode
  - 16MB flash
  - custom partition table
  - SPIFFS long object name

## SPIFFS Image

firmware image には runtime display asset だけを含めます。`source/frames` や
`source/atlas` は含めません。

staging 後の SPIFFS image source:

```text
build/spiffs_assets/aomi/display-96-png/...
build/spiffs_assets/bitomos-umi/display-96-png/...
```

runtime path:

```text
/spiffs/aomi/display-96-png/idle/00.png
/spiffs/aomi/display-96-png/idle/01.png
/spiffs/bitomos-umi/display-96-png/review/00.png
/spiffs/bitomos-umi/display-96-png/review/01.png
```

partition:

- app: 3MB
- SPIFFS storage: 6MB

現在の 2 pet 分の `display-96-png` asset には十分なサイズです。

## 描画方式

PNG frame は state 切替時に decode し、96 x 104 の RGB565 frame cache として保持します。

1. current state の `<state>/<nn>.png` を順に開く。
2. `M5Canvas::drawPng()` で背景色つき RGB565 canvas に decode する。
3. decode 済み buffer を state frame cache にコピーする。
4. 再生中は `frameIndex` に対応する cache slice を `M5.Display.pushImage()` で描画する。

背景色で合成してから cache するため、再生中は PNG decode を行いません。

## Build

推奨は EIM + ESP-IDF v5.5 です。ESP-IDF v5.5 の downloadable tools では CMake
3.30.2 が使われますが、これは EIM の ESP-IDF 実行環境側で管理されます。この repo の
`mise.toml` では CMake を pin しません。グローバルの CMake 4.x は EIM install 前の
prerequisite check 用として使えます。

初回:

```sh
mise install
mise exec -- eim install -i v5.5
```

mise task にはスキップ判定があります。ESP-IDF v5.5 が install 済みなら
`esp:idf:install` はすぐ終了します。

```sh
mise run esp:idf:install
```

build:

```sh
cd esp-idf/basic-pet
mise exec -- eim run "idf.py set-target esp32s3" v5.5
mise exec -- eim run "idf.py build" v5.5
```

または repo root から mise task を使います。

```sh
mise run esp:basic-pet:set-target
mise run esp:basic-pet:build
```

`esp:basic-pet:set-target` も `sdkconfig` がすでに `esp32s3` なら何もしません。
`sdkconfig.defaults` を変えた後に `sdkconfig` を再生成したい場合は `FORCE=1` を付けます。

```sh
FORCE=1 mise run esp:basic-pet:set-target
```

### PSRAM note

CoreS3-Lite では PSRAM を有効にしますが、ESP32-S3 の ESP-IDF v5.5 Kconfig の
default と同じく `CONFIG_SPIRAM_MODE_QUAD=y` を使います。
`CONFIG_SPIRAM_MODE_OCT=y` にすると、実機によっては bootloader 後に次のようなログで
再起動を繰り返します。

```text
E (...) octal_psram: PSRAM ID read error: 0x00000000
E (...) cpu_start: Failed to init external RAM!
```

このログが出た場合は `sdkconfig.defaults` と生成済み `sdkconfig` が
`CONFIG_SPIRAM_MODE_QUAD=y` になっているか確認してください。

flash:

```sh
cd esp-idf/basic-pet
mise exec -- eim run "idf.py flash monitor" v5.5
```

mise task で flash + monitor する場合:

```sh
mise run esp:basic-pet:flash-monitor
```

port を明示する場合:

```sh
mise exec -- eim run "idf.py -p /dev/cu.usbmodemXXXX flash monitor" v5.5
PORT=/dev/cu.usbmodemXXXX mise run esp:basic-pet:flash-monitor
```

## 実装済み検証

以下の build は成功済みです。

```sh
cd esp-idf/basic-pet
mise exec -- eim run "idf.py build" v5.5
```

確認された build 条件:

- ESP-IDF: 5.5
- LVGL: 9.2.2
- `m5stack_core_s3`: 3.0.2
- target: `esp32s3`
- flash size: 16MB
- app binary size: 約 0x8f7a0
- SPIFFS image offset: `0x310000`

## 実機 Acceptance Checks

実機で次を確認してください。

- 起動直後に `Aomi | idle` が左上に表示される。
- Aomi の `idle` animation が画面中央で再生される。
- `<` tap で previous state に切り替わる。
- `>` tap で next state に切り替わる。
- `*` short tap では pet が切り替わらない。
- `*` long press で `Aomi` と `Bitomos` が切り替わる。
- pet 本体 tap で `waving` に切り替わる。
- pet 本体 tap では pet が切り替わらない。
- 日本語フォントなしで表示が成立する。
- 5 分以上放置して animation が止まらない。
- 5 分以上放置して目立つ表示崩れや heap 減少がない。

## Known Limitations

- v1 は ESP-IDF 版のみ。
- Arduino 版は更新していない。
- pet body の drag gesture はまだ区別していない。
- `waving`, `jumping`, `failed` も loop 再生する。
- one-shot state playback は未実装。
- state transition feedback sound は未実装。
- IMU, microphone, speaker, proximity / ambient light sensor は未使用。
- 実機 touch の press/release 距離による誤操作抑制は、LVGL object event に委ねている。

## Next Steps

優先度順の改善候補です。

1. 実機で touch hit area と long press の体感を確認する。
2. pet body の drag / stroke を reaction として扱う。
3. `waving`, `jumping`, `failed` を one-shot 再生にして `idle` へ戻す。
4. state ごとに loop / one-shot metadata を asset manifest か generated header に追加する。
5. petting 相当 state が asset に追加されたら、pet body reaction を `waving` から差し替える。
6. state cache の入れ替え時に decode 時間が気になる場合、pet 単位 cache も検討する。
