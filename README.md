# RealtimeTextureBaker

`BakeProjectionActor` を使って、カメラ投影系のテクスチャ出力や UV 生成を行うためのプラグインです。

## Author

- Author: Hironori Sugino
- Website: https://sugi.cc
- Original Repository: https://github.com/sugi-cho/RealtimeTextureBaker

## できること

- `OutputRenderTarget` の内容を PNG として保存できます
- `ProjectionCamera` の現在の描画結果を PNG として保存できます
- `TargetMesh` に対して、`ProjectionCamera` 基準のカメラプロジェクション UV を追加した StaticMesh を生成できます

## 対象ユーザー

- カメラマップ用のアタリ画像を作りたい人
- プロジェクション結果を静止画として保存したい人
- メッシュに投影用 UV を追加して、あとからマテリアルで使いたい人

## 主な機能

### RenderTarget の PNG 保存

`OutputRenderTarget` に出力された画像を PNG として保存します。

- エディタ上の Details から実行できます
- 保存先は `Saved/RealtimeTextureBaker/` 配下です
- ファイル名は実行した Actor 名を含む形式になります

### ProjectionCamera の PNG 保存

`ProjectionCamera` が現在見ている内容を PNG として保存します。

- カメラの見た目確認や、アタリ画像の書き出しに使えます
- 保存先は `Saved/RealtimeTextureBaker/` 配下です
- 保存時の描画は、見た目が暗くなりにくいように調整されています

### カメラプロジェクション UV の生成

`TargetMesh` に対して、`ProjectionCamera` から見た投影 UV を追加した StaticMesh を生成します。

- 元の UV は保持されます
- 末尾の UV チャンネルに新しい投影 UV を追加します
- `ProjectionCamera` の `FieldOfView` と `AspectRatio` を考慮します
- 画面外の領域は 0-1 に clamp せず、そのまま外側へ伸びます
- 見えない面も含めて UV を付与します

## 使い方

1. `BakeProjectionActor` をシーンに配置します
2. `ProjectionCamera` を設定します
3. `TargetMesh` や `OutputRenderTarget` を設定します
4. Details パネルの各ボタンから必要な処理を実行します

## 生成物の保存先

- PNG 出力: `Saved/RealtimeTextureBaker/`
- カメラプロジェクション UV 付き StaticMesh: 元メッシュの近くに `_CamProjUV` 付きで生成

## 注意点

- `ProjectionCamera` を動かした場合、投影 UV は再生成が必要です
- カメラプロジェクション UV は、そのカメラ位置・向き・画角に依存します
- 画面外まで伸びる UV は、用途に応じてマテリアル側で扱いを調整してください

