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

Arduino版は `arduino-cli` から `arduino/basic-pet` をビルドします。

ESP-IDF版は公式ESP-IDF環境で次を実行します。

```sh
cd esp-idf/basic-pet
idf.py set-target esp32s3
idf.py build
idf.py flash monitor
```
