# codex-pet-anim

Pet animation の state machine を扱うための小さな C++ header-only library です。

この library は pet 固有の state 一覧、frame 数、duration、画像を持ちません。それらは
`tools/build-display-assets.py` が `assets/<pet-id>/display-96-png/<pet>_anim.h` として
生成します。

## Responsibilities

- pet ごとの `PetSpec` / `StateInfo` を受け取る。
- 現在 state / frame / duration から次 frame へ進める。
- state 名から state index を引く。

## Example

```cpp
#include "bitomos_umi_anim.h"

auto player = codex_pet::makePlayer(bitomos_umi::k_pet);
codex_pet::setState(player, "idle", millis());
```

runtime で JSON を parse する必要はありませんが、pet 固有の generated header は include
してください。
