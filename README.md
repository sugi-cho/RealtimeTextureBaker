# RealtimeTextureBaker

Unreal Engine plugin for Realtime Texture Baking and Mesh Projection.

## ABakeProjectionActor

BP から使う場合は `BakeProjectionActor` をレベルに置き、`BakeMode`、`TargetMesh`、`ProjectionCamera`、`SourceTexture`、`Settings` を設定します。

### 主な関数

- `AllocateDepthRenderTarget(AspectRatio)`
  - Depth 用 RenderTarget を作成します。
  - カメラ投影焼きの前処理です。
  - BP 用です。
- `AllocateDepthRenderTargetEditor()`
  - エディタ Details のボタン用です。
  - `AllocateDepthRenderTarget()` を引数なしで呼びます。
- `BakeCameraProjection()`
  - カメラ投影で焼き込みます。
  - `ProjectionCamera` と `TargetMesh` を使います。
  - BP 用です。
- `BakeCameraProjectionEditor()`
  - エディタ Details のボタン用です。
  - `BakeCameraProjection()` を呼びます。
- `BakeCurrentMode()`
  - `BakeMode` を見て `BakeUVTexture()` か `BakeCameraProjection()` を実行します。
  - BP からも呼べます。
  - BP 用です。
- `BakeCurrentModeEditor()`
  - エディタ Details のボタン用です。
  - `BakeCurrentMode()` を呼びます。
- `ClearOutput()`
  - 出力 RenderTarget を `Settings.ClearColor` で消去します。
  - BP とエディタ両方で使えます。

### 使い方

1. `BakeMode` を `BakeUV` か `BakeCamera` に設定します。
2. 自動実行したい場合は `bAutoBake = true` にします。
3. 手動実行したい場合は `bAutoBake = false` にして、`BakeCurrentMode()` を押します。
4. UV 焼きは `BakeMode = BakeUV`、カメラ投影焼きは `BakeMode = BakeCamera` を使います。

### 補足

- `BakeCurrentMode()` は手動実行の入口です。
- `bAutoBake = true` のときだけ `Tick()` で自動焼きします。

### ハマりどころ

- `CallInEditor` のボタンは、`void` のエディタ用関数にすると出しやすいです。
- 戻り値ありの関数は、Details にそのままボタン表示されないことがあります。
- その場合は、`void` の `...Editor()` ラッパーを作って中で本体関数を呼びます。
- `BlueprintCallable` は BP 呼び出し用で、Details ボタン化とは別です。
- 変更後に Details が更新されないときは、UE の再起動が必要なことがあります。
