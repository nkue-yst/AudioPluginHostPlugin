# プロジェクトへの導入と使い方

自分の UE プロジェクトへ Audio Plugin Host を組み込み、音源を VST3 で処理して PIE で音を確認する手順です。仕組みと実装構成は [README](../README.md) を参照してください。

## 1. 必要なものを用意する

- Windows x64、Unreal Engine 5.8。
- Visual Studio 2022 の C++ ツールと Windows SDK、PowerShell。
- 導入先の C++ プロジェクト。Blueprint のみの場合は、先に C++ クラスを追加して Editor ターゲットをビルドできるようにします。
- Win64・ステレオ・32-bit float に対応する VST3 Audio Effect と、試聴に使う Sound Wave や Sound Cue。

VST3 の Main Input / Main Output は各1本が対象です。Instrument、MIDI、サイドチェーンなどの追加 Audio Bus には対応していません。VST3 自体のインストール・認証が必要な製品は、その準備も済ませてください。

ソースは Git で clone するか、SDK 同梱のソース zip を使います。GitHub の Download ZIP にはサブモジュールの SDK が入らないため、Git 取得には clone を使ってください。

## 2. SDK を準備し、プラグインをコピーする

Git で取得した場合は、PowerShell でリポジトリのルートへ移動し、SDK を取得します。使用する SDK の版はサブモジュールに固定されています。

```powershell
git submodule update --init --recursive
```

SDK 同梱 zip では SDK の取得は不要です。取得したリポジトリ、または展開した zip のルートで、導入先の Editor を閉じてからコピーします。次のパスとターゲット名は例なので、利用する環境に置き換えてください。

```powershell
$env:UE_ROOT = 'C:\Program Files\Epic Games\UE_5.8'
$ProjectFile = 'D:\UnrealProjects\MyGame\MyGame.uproject'
$ProjectRoot = Split-Path -Parent $ProjectFile
$EditorTarget = 'MyGameEditor'

robocopy .\Plugins\AudioPluginHost "$ProjectRoot\Plugins\AudioPluginHost" /E /XD Binaries Intermediate Saved .git /XF .git
if ($LASTEXITCODE -ge 8) { throw 'プラグインのコピーに失敗しました。' }
Copy-Item -LiteralPath .\LICENSE -Destination "$ProjectRoot\Plugins\AudioPluginHost\LICENSE"
```

SDK 本体を含めてコピーし、生成物と `.git` 管理データを除くコマンドです。手動でコピーする場合も、SDK 配下にある `.git` ファイル・フォルダを含めないでください。

## 3. プラグインを有効にしてビルドする

導入先の `.uproject` をテキストエディタで開き、`Plugins` 配列へ次の項目を追加します。配列がなければ作成し、既存の項目がある場合は保持してください。

```json
{ "Name": "AudioPluginHost", "Enabled": true }
```

続けて、導入先の Development Editor ターゲットをビルドします。

```powershell
& "$env:UE_ROOT/Engine/Build/BatchFiles/Build.bat" $EditorTarget Win64 Development "-Project=$ProjectFile" -WaitMutex -NoHotReloadFromIDE -MaxParallelActions=4
```

成功したら導入先の `.uproject` を開きます。File メニューに VST、Content Browser の Audio カテゴリに VST Mixer が表示されれば、プラグインを利用できます。

## 4. 使用する VST3 をインポートする

File → VST から形式に合うメニューを選びます。

| 配布形式 | メニューと選ぶもの |
| --- | --- |
| 単一の `.vst3` ファイル | Import VST3 File でファイルを選択 |
| `.vst3` フォルダのバンドル | Import VST3 Bundle Directory でフォルダ全体を選択 |

バンドルは、例えば `Example.vst3/Contents/x86_64-win/Example.vst3` という構成です。名前と内部構成を保ってインポートします。

成功すると `/Game/AudioPluginHost/Modules` へ Module Asset が作られます。Save / Save All で保存してください。VST3 本体と付属ファイルは、プロジェクトの `Content/AudioPluginHost/NativeModules` へコピーされます。元の VST3 を更新しても、既存のプリセットは自動で差し替わりません。

## 5. ミキサーと音声経路を作る

1. Content Browser のプロジェクトコンテンツ（`/Game`）で、Audio → VST Mixer を作り、ダブルクリックします。
2. Create root bus でルートを作ります。必要なら Create bus で子バスを作ります。新しい子バスはルートへ接続されます。
3. 対象バスを選び、ADD VST3 で Module Asset とコンポーネントを選択して、Create insert を押します。
4. Content Browser で試聴する Sound Wave や Sound Cue を選び、送信先のバスで Route selected sounds here を押します。
5. Save mixer を押します。

作成したバスとプリセットは、ミキサーの隣の `<MixerName>_Assets/Buses` と `Presets` へ保存します。ミキサー・バス・プリセット・割り当てる音源は `/Game` 配下を使います。

この手順では、選んだ音源の Base Submix を有効にし、送信先を対象バスへ設定します。プロジェクト全体の Master Submix を変更する必要はありません。全体の既定出力へ適用する場合だけ、親を持たないルートで Set root as project master を実行し、UE を再起動します。

## 6. 再生して効果を確認する

試聴する音源をレベルへ配置し、Audio Component の Auto Activate を有効にするなどして、PIE で再生します。挿入スロットの On を切り替え、処理音と原音を比較してください。

GUI を持つ VST3 では、スロットの GUI からパラメーターを操作できます。停止中にも開けますが、その場合は音声を出さない編集専用インスタンスです。音を聴きながら調整する場合は PIE 中に開きます。

PIE 中は GUI、バイパス、バス出力音量を変更できます。バス接続、挿入の追加・削除・並べ替えは停止してから行います。

## 7. 保存し、別の用途へ再利用する

PIE 中に GUI を操作した場合は、Play を止める前に GUI を閉じて状態取得を待ち、その後 Save mixer を押します。停止中 GUI は Save mixer からも状態を取り込めます。保存エラーが出た場合は内容を確認して再試行してください。

Save mixer は、ミキサー、バス、挿入プリセット、変更した音源や親バスを保存します。新規挿入を作ったときは、選択した Module Asset も含みます。保存を保留して画面を閉じた場合は、UE の Save All で関連アセットを保存してください。

別の構成として使う場合は、Content Browser でミキサーを Duplicate し、複製側で Make independent を実行します。これでバスと挿入プリセットを複製できます。音源の送信先や Master 設定は自動で切り替わらないため、複製側へ明示的に割り当てます。

## うまく動かないとき

| 状況 | 確認すること |
| --- | --- |
| メニューが出ない・ビルドできない | SDK の準備、プラグインの有効化、導入先の Editor ターゲット、UE 版 |
| インポートできない | Win64、対応する Audio Effect 形式、バンドル全体の選択、製品のインストール・認証 |
| 音が変わらない | 音源の再生、Base Submix、Audio Component 側の送信先上書き、挿入の On、バス音量 |
| PIE 中に GUI が開かない | 音声がそのプリセットを通っているか、同じプリセットを複数段で共有していないか |

同じ VST3 を複数段へ使うときは、段ごとに Create insert で別のプリセットを作ります。処理が失敗して原音へ戻った場合は Output Log を確認し、原因を解消してから PIE を開始し直してください。

持ち込む VST3 や成果物を配布する場合は、[README のライセンス欄](../README.md#ライセンス)と各製品の再配布条件を確認してください。管理フォルダにある未使用の VST3 もステージ対象になるため、配布内容を確認します。
