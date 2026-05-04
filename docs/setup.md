# Setup

## 対象機種

- M5Stack CoreS3 Lite

## 必要なコマンド

- `mise`
- `arduino-cli`
- `eim`

## 初回セットアップ

この repo で使うセットアップは mise task から実行します。

```sh
mise install
mise run setup
```

`mise run setup` は次を実行します。

- M5Stack Arduino core と `M5Unified` の導入
- ESP-IDF v5.5 の導入

Python 変換ツールの依存関係は script inline metadata に書き、mise が入れる `uv` で実行します。

## Arduino

build:

```sh
mise run arduino:basic-pet:build
```

upload:

```sh
PORT=/dev/cu.usbmodemXXXX mise run arduino:basic-pet:upload
```

## ESP-IDF

build:

```sh
mise run esp:basic-pet:set-target
mise run esp:basic-pet:build
```

flash + monitor:

```sh
PORT=/dev/cu.usbmodemXXXX mise run esp:basic-pet:flash-monitor
```
