# Setup

## 対象機種

- M5Stack CoreS3 Lite

## arduino-cli

`arduino/basic-pet` を `arduino-cli` でビルドします。M5Stackのボード定義と `M5Unified` ライブラリを入れてから使います。

ボードFQBNは環境に入れたM5Stack/ESP32パッケージに合わせて確認してください。

```sh
arduino-cli board listall | grep -i cores3
arduino-cli lib install M5Unified
arduino-cli compile --fqbn <your-core-s3-fqbn> arduino/basic-pet
```

## ESP-IDF

公式ESP-IDF環境で使います。初回は依存コンポーネントの取得が走ります。

```sh
cd esp-idf/basic-pet
idf.py set-target esp32s3
idf.py build
idf.py flash monitor
```

シリアルポートを明示する場合は `-p /dev/cu.usbmodem...` を付けます。
