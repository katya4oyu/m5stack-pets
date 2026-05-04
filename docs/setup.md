# Setup

## 対象機種

- M5Stack CoreS3 Lite

## arduino-cli

`arduino/basic-pet` を `arduino-cli` でビルドします。M5Stackのボード定義と `M5Unified` ライブラリを入れてから使います。

ボードFQBNは環境に入れたM5Stack/ESP32パッケージに合わせて確認してください。

```sh
arduino-cli board listall | grep -i cores3
arduino-cli lib install M5Unified
arduino-cli compile --fqbn m5stack:esp32:m5stack_cores3:PartitionScheme=factory_4apps arduino/basic-pet
```

## ESP-IDF

macOSでは、まずmiseでビルドに使う汎用ツールを入れます。

```sh
mise install
```

Homebrew側の依存関係は、ESP-IDFのmacOSセットアップに合わせて現行版を使います。

```sh
brew install libgcrypt glib pixman sdl2 libslirp dfu-util
```

公式ESP-IDF環境で使います。初回は依存コンポーネントの取得が走ります。

```sh
cd esp-idf/basic-pet
idf.py set-target esp32s3
idf.py build
idf.py flash monitor
```

シリアルポートを明示する場合は `-p /dev/cu.usbmodem...` を付けます。
