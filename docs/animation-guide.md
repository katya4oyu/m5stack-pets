# Animation Guide

Bitomos の素材を M5Stack CoreS3 で扱うためのメモです。Codex pet 用に作った
spritesheet をそのまま firmware に持ち込むより、まずは切り出し済み PNG フレームを
小さく変換して使う方針を推奨します。

## 元素材

現在の bitomos-umi は、ローカルの Codex 作業ディレクトリにある
`bitomos-hybrid-run` から取り込んだ素材です。Codex で生成した hatch-pet run は、
環境によりますが、おおむね次のような場所を探すと見つかります。

```text
~/Documents/Codex/<date>-<task-or-skill-name>/<pet-name>-run
~/Documents/Codex/<date>-<task-or-skill-name>/<pet-name>-run-v3
```

たとえば run directory 名だけなら `bitomos-hybrid-run` や `aomi-run-v3` のようになります。
repo 内では Bitomos のサンプル asset を `bitomos-umi` として扱います。Git 管理する
ファイルには、ローカルユーザ名つきの絶対パスを残しません。

主な出力は次の通りです。

```text
frames/                 192 x 208 の透過 PNG フレーム
final/spritesheet.webp  Codex pet 用 8 x 9 atlas
qa/contact-sheet.png    全フレーム確認用シート。repo の assets には入れない
qa/videos/*.mp4         state ごとの preview 動画。repo の assets には入れない
```

M5Stack CoreS3 向けの試作では `frames/` を変換元 asset として扱います。1フレームずつ
確認、縮小、減色、RGB565 変換ができます。`spritesheet.webp` は Codex pet 元形式の
保存用です。CoreS3 実機で WebP atlas を直接読む想定ではありません。

## フレーム構成

この `hatch-pet` skill が現在作る Codex pet atlas は固定構造です。

```text
8 columns x 9 rows
1 cell = 192 x 208 px
total = 1536 x 1872 px
```

ただし、M5Stack 側の runtime library はこの state 数や frame 数を固定値として持たせません。
別の pet format や将来の hatch-pet で state/frame 数が変わってもよいように、pet 固有の
state table は asset 生成時に C++ header として出します。

Bitomos Umi では次の state を使います。

| State | Frames | Hybrid の採用元 |
| --- | ---: | --- |
| `idle` | 6 | front |
| `running-right` | 8 | original |
| `running-left` | 8 | original |
| `waving` | 4 | front |
| `jumping` | 5 | original |
| `failed` | 8 | original |
| `waiting` | 6 | front |
| `running` | 6 | original |
| `review` | 6 | front |

再生時間の目安です。

| State | Timing |
| --- | --- |
| `idle` | 280, 110, 110, 140, 140, 320 ms |
| `running-right` | 各 120 ms、最後だけ 220 ms |
| `running-left` | 各 120 ms、最後だけ 220 ms |
| `waving` | 各 140 ms、最後だけ 280 ms |
| `jumping` | 各 140 ms、最後だけ 280 ms |
| `failed` | 各 140 ms、最後だけ 240 ms |
| `waiting` | 各 150 ms、最後だけ 260 ms |
| `running` | 各 120 ms、最後だけ 220 ms |
| `review` | 各 150 ms、最後だけ 280 ms |

## repo 内での素材配置案

この repo に素材を取り込む場合は、生成元と変換後を分けます。`assets/` はローカルの
Codex pet/run directory から取り込む作業場所です。Git では `assets/aomi/display-96-png` と
`assets/bitomos-umi/display-96-png` だけを runtime サンプルとして管理し、`source/` とそれ以外の
local import は `.gitignore` します。

Codex pet の run directory をそのまま取り込むには importer を使います。

```sh
python3 tools/import-codex-pet.py \
  --run-dir /path/to/bitomos-umi-run \
  --pet-id bitomos-umi \
  --display-name "Bitomos Umi"
```

importer は `frames/` と `final/spritesheet.webp` をコピーします。既存の取り込み結果を
置き換える場合は `--force` を付けます。`qa/` や mp4 preview は人間の確認用なので、
repo の `assets/` には入れません。

import 後の配置:

```text
assets/bitomos-umi/source/codex-pet.json
assets/bitomos-umi/source/frames/<state>/<nn>.png
assets/bitomos-umi/source/atlas/spritesheet.webp
```

`source/` は build-display-assets の入力です。ローカルには置きますが、Git 管理する
example asset には含めません。

変換後の表示用素材は `display-96-png` に出します。

```sh
python3 tools/build-display-assets.py \
  --pet-dir assets/bitomos-umi \
  --width 96 \
  --resample box \
  --force
```

```text
assets/bitomos-umi/display-96-png/manifest.json
assets/bitomos-umi/display-96-png/bitomos_umi_anim.h
assets/bitomos-umi/display-96-png/idle/00.png
assets/bitomos-umi/display-96-png/waiting/00.png
```

M5Stack 側の再生処理は `libs/codex-pet-anim` に置きます。pet 固有の state/frame/duration
table は `display-96-png/bitomos_umi_anim.h` のような
generated header に置きます。

```text
libs/codex-pet-anim/include/codex_pet_anim.h
libs/codex-pet-anim/README.md
assets/bitomos-umi/display-96-png/bitomos_umi_anim.h
```

`codex_pet_anim.h` は画像そのものも、Bitomos 固有の state 数も持ちません。`PetSpec` を
受け取って、現在 frame の duration 判定、frame index の更新、path 生成だけを行います。
JSON は build-time の manifest です。Arduino/ESP-IDF runtime で JSON library に依存する
必要はありません。

```cpp
#include "codex_pet_anim.h"
#include "bitomos_umi_anim.h"

auto player = codex_pet::makePlayer(bitomos_umi::k_pet);
codex_pet::setState(player, "idle", millis());
```

取り扱いルール:

- `source/frames` は生成された 192 x 208 PNG をそのまま置く。
- `source/atlas/spritesheet.webp` は Codex pet 元形式の保存用として置く。
- `source/` はローカル変換元として扱い、Git 管理しない。
- firmware で使う縮小版は `display-96-png` のように PNG runtime asset として出す。
- `display-*` は生成物なので、必要に応じて source から作り直す。
- `libs/codex-pet-anim` は pet 非依存の再生 helper として扱う。
- state/frame/duration は `display-*` の generated header に持たせる。
- `manifest.json` は build-time / 確認用。firmware runtime では読まない。
- source frame を直接上書きしない。
- 変換スクリプトを置く場合は `tools/convert-assets/` に集約する。
- preview 動画、contact sheet、validation/review JSON は QA 用なので `assets/` と firmware には入れない。

CoreS3 の画面は 320 x 240 なので、192 x 208 のままでも表示できます。ただし UI や
余白を考えると、常駐 pet としては `96 x 104` または `128 x 139` くらいが扱いやすいです。

## アニメーション再生モデル

実装は小さな state machine にします。

```text
currentState
currentFrame
frameStartedAtMs
stateFrames[]
stateDurations[]
```

loop ごとの処理:

1. `millis()` または RTOS tick を読む。
2. 現在フレームの表示時間を超えたか判定する。
3. 超えていたら `currentFrame` を進める。
4. loop state なら最後の次は 0 に戻す。
5. frame が変わった時だけ描画する。

`idle`, `waiting`, `review`, `running` 系は loop で使います。`waving`, `jumping`,
`failed` は一度再生したあと `idle` か `waiting` に戻すと扱いやすいです。

## Arduino / M5Unified での方針

Arduino 版では、最初は小さく始めるのが安全です。

1. `idle/00.png` 相当の1枚を C array にして表示する。
2. `idle` だけを loop 再生する。
3. Button A で `waving` を再生して `idle` に戻す。
4. Button B で `failed` を再生して `idle` に戻す。
5. 長時間 loop して heap が増えないことを確認する。

PNG について:

- LovyanGFX/M5GFX 系には PNG を描画する API や decoder を使う道があります。
- ただし PNG decode は CPU/heap/flash I/O を使うので、57枚のアニメを常時再生する形式としては重めです。
- PNG は repo 内の source/変換元として扱い、実機用には縮小済み PNG frame に変換します。

描画方法の候補:

- RGB565 の C array を `M5.Display.pushImage()` で描く。
- PNG alpha で透過を扱う。
- chroma-key 色を決めて、背景色を描かないようにする。
- フレーム数が増えたら LittleFS/SPIFFS/SD に変換済み binary を置いて読む。

まずは `96 x 104` の縮小版を C array 化するのが良さそうです。全部を raw RGBA で
RAM に持つ設計は避けます。

## ESP-IDF / LVGL での方針

ESP-IDF 版では LVGL の `lv_img` と `lv_timer` を使う構成が自然です。

- frame を LVGL image descriptor に変換する、または filesystem に置く。
- 画面中央に `lv_img` を1つ置く。
- `lv_timer` で表示する frame source を更新する。
- BSP の LVGL tick/timer と合わせる。

LVGL は pet の周囲に UI を足しやすいです。Arduino/M5Unified は直接描画で素早く
試せるので、最初の絵作り確認に向いています。

## メモリとフォーマット

元セルサイズの raw 展開サイズです。

```text
192 x 208 x 2 bytes RGB565 = 79,872 bytes / frame
192 x 208 x 4 bytes RGBA   = 159,744 bytes / frame
```

Bitomos Umi の使用フレームは合計 57 枚です。全フレームを raw RGBA のまま RAM に
置くのは現実的ではありません。

推奨:

- firmware では `96 x 104` などに縮小する。
- flash には圧縮または RGB565 で置く。
- RAM に置くのは current frame と next frame 程度にする。
- 背景固定で edge が気になる場合は、変換時に背景色へ合成することも検討する。

`96 x 104` の RGB565 なら:

```text
96 x 104 x 2 bytes RGB565 = 19,968 bytes / frame
```

かなり扱いやすくなります。

## 状態割り当て案

Bitomos を常駐 pet として動かす場合の state mapping です。

| App event | Pet state |
| --- | --- |
| 通常の常駐状態 | `idle` |
| しばらく入力がない | `waiting` |
| ボタンを押した | `waving` |
| 考え中 / 通信中 / 処理中 | `review` |
| 軽い成功演出 | `jumping` |
| エラー / 失敗 | `failed` |
| UI mode の切り替え | `running` |
| 左右方向の移動 | `running-left` / `running-right` |

## 最初の milestone

最初の到達点はこれくらいが良さそうです。

1. `96 x 104` の `idle` loop を作る。
2. 320 x 240 の中央に表示する。
3. Button A で `waving` を一度再生して `idle` に戻る。
4. Button B で `failed` を一度再生して `idle` に戻る。
5. 数分間 loop して表示崩れや heap 増加がないことを確認する。

ここまでできたら `waiting` と `review` を足して、最後に `running` 系をアプリの状態遷移に
合わせて使うか決めます。
