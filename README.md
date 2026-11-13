# Blockchain simulator

PoWのブロック生成・配送と、報酬シェア・遅延改善インセンティブを調べるシミュレータです。

## 収録している実装

| ファイル | 用途 |
| --- | --- |
| `blockchainSimulator.cpp` | もともとのシミュレータ原本。既存の内容を保持しています。 |
| `src/uniform_simulator.cpp` | 共通の一定遅延を扱う独立した検証版。高さ別の開始者・採用者を集計します。 |
| `src/latency_simulator.cpp` | 大規模マイナーAの送信・受信遅延と、小規模マイナー間遅延を別々に設定する版。 |

新しい2実装はブロックの祖先を全て保持し、JSONを出力します。原本のGit blob SHAは
`ebadfd662a3cddade3b4aaa0bfe9b7008753c96b`で、`make test`でも一致を確認します。

## ビルドとテスト

C++17コンパイラ、GNU Make、Python 3が必要です。分析にはPython 3.12で動作確認しています。

```sh
make
make original
make test
```

`build/latency_simulator`、`build/uniform_simulator`、`build/original_simulator`が生成されます。
シミュレーションとテストの実行にはPythonの追加パッケージは不要です。

テストには独立した配送ヒープとの180,000ブロックの照合、共通遅延版との108,000ブロックの照合、
送受信操作の切り分け、ゼロ遅延での採用、乱数seedの再現、微小改善のペアリングを含みます。

## もともとのシミュレータ

```sh
mkdir -p results
./build/original_simulator > results/original.log
```

ノード数・遅延・生成間隔・seedなどは原本内の定数で設定されています。
原本はイベントログを出力します。現在のエントリーポイントは`simulation(0)`を実行し、
定義されている`mainChain`集計関数は呼び出しません。
原本のブロック再利用を含む挙動を保持し、JSON報酬集計には`src/`の検証版を用意しています。

## 共通遅延版

```sh
./build/uniform_simulator simulate 1000 3 10 1 100000 10 1000
```

引数は順に次のとおりです。

```text
simulate N alpha分子 alpha分母 Delta/T 到達高さ seed 追加採掘高さ
```

この例ではAのハッシュ比率が3/10、残りを999マイナーに均等分割します。
初期1,000高さと末尾200高さを除いて、最初に到達高さへ達したチェーンを集計します。
追加採掘後に同じ測定範囲が変わったブロック数も出力します。
これは有限回の安定性確認で、一般の最終合意保証ではありません。

出力には`r_A`、`pi_A`、`pi_O`、`W_A`、`W_O`、`X_A`、`X_O`と、その分母・分子の整数件数を含みます。
O開始ラウンドなどの観測が0回なら、条件付き確率は`null`です。

採掘トレースの再生にも対応しています。

```sh
./build/uniform_simulator replay trace.csv 1000 1
```

トレースはヘッダー`id,time,minter,parent,height`を持つCSVです。IDはgenesis=0、以後1から連番とし、
既存の親IDを指定します。全ブロックの親・高さを再計算して照合します。
集計範囲を確保するため、トレースは高さ1,201以上に達する必要があります。

## 送受信遅延版

```sh
./build/latency_simulator 1000 0.3 0.9999 1 2 2000000 10000 5000 2001
```

引数は次の順です。時間はネットワーク全体の平均生成間隔を`T=1`として正規化しています。

```text
N alpha_A A送信遅延 A受信遅延 小規模間遅延 観測終了時刻 burn時刻 追加観測時間 seed
```

モデルは、独立・一定率のPoisson採掘、即時公開、現在の最長先端への採掘、同高では現在の枝を保つfirst-seenです。
Aはノード0、O側の個々のハッシュは均等です。各マイナーは自分のブロックを即座に利用でき、
他者には指定された方向別の一定遅延で直接配送します。追加の中継・帯域制約・通信費用はモデル化しません。

Oへの共通配送は遅延評価で処理しますが、個々のOの自己採掘履歴を保持します。
無限小Oモデルへの置き換えは行っていません。

観測窓内に生成されたブロックについて、窓終了後の追加採掘で採用を判定し、さらに同じ時間だけ
採掘を続けて変化を確認します。例えば上の設定では、観測窓が`(10000, 2000000]`で、
`2005000`と`2010000`時点のチェーンを照合します。

主な出力は次のとおりです。

| 項目 | 意味 |
| --- | --- |
| `generated_A`, `accepted_A`, `accepted_all` | Aの生成数、Aの採用数、全採用数 |
| `reward_share_A` | A採用数 / 全採用数 |
| `reward_rate_A` | A採用数 / 観測時間 |
| `acceptance_A` | A採用数 / A生成数 |
| `chain_rate` | 全採用数 / 観測時間 |
| `confirmation_blocks_reorganized` | 追加の継続採掘で測定範囲から外れたブロック数 |

分母0の比率は`null`です。報酬シェアと時間当たり報酬を区別してください。
また、全祖先を保持するため、メモリ使用量は生成ブロック数に比例します。

## 微小な遅延改善の実験

まず短い動作確認を実行できます。

```sh
python3 experiments/run.py --quick --output results/quick
```

完全な微小変化の設定では、Aの送信・受信遅延をそれぞれ1%、0.5%、0.1%、0.01%短縮します。
各条件16seed、計1,136実行で、境界では逆方向の遅延増加も測ります。

```sh
python3 experiments/run.py --plan experiments/plans/marginal.json --output results/marginal --workers 4
```

50%改善を比較する予備実験の設定も収録しています（352実行）。

```sh
python3 experiments/run.py --plan experiments/plans/pilot.json --output results/pilot --workers 4
```

同じseedでは全条件に同じ採掘時刻列・採掘者列を与えます。出力先には設定、各実行の生JSON、
ソースとバイナリのハッシュを含むmanifestを保存します。既存結果の上書きには`--overwrite`が必要です。
`--quick`は動作確認用で、研究上の精度評価には完全な設定を使ってください。

## 分析

```sh
python3 -m venv .venv
. .venv/bin/activate
python3 -m pip install -r requirements-analysis.txt
python3 experiments/analyze.py results/marginal --plots
```

`paired_effects.csv`と`summary.json`を生成し、`--plots`では図も保存します。
感度は、改善幅を`epsilon`、時間当たり報酬を`u`として
`(u_improved - u_baseline) / (epsilon * u_baseline)`で定義します。
値0.3は、局所的に「遅延1%短縮あたり報酬0.3%増加」に相当します。
実際の有限幅での増減率も別列に記録します。悪化側では損失を正として表示します。

95%区間はseed間のペア差から計算する点ごとの近似t区間です。全ペアで変化が無かった場合は
ゼロ幅区間を表示せず、観測変動なしとして区別します。微小有限差分の推定であり、
境界での微分可能性や真の効果0を証明するものではありません。

乱数seedに加え、コンパイラ・標準ライブラリ・Python依存関係も再現条件として記録してください。
異なるC++標準ライブラリ間のビット単位の一致は前提にしていません。

## d/T・最大ハッシュ比率と論文の比較

[On the Incentive Compatibility of Block Propagation in Bitcoin](https://arxiv.org/abs/2606.06860)
のfirst-seen一次近似と、同じ報酬シェア指標で比較する設定です。
基準でAの送信・受信遅延を`d`、小規模間遅延を`k*d`とし、
最大マイナーAだけの送信または受信を`(1-epsilon)*d`にします。

```sh
python3 experiments/sweep.py --quick --output results/grid-quick
python3 experiments/sweep.py --output results/grid --workers 4
python3 experiments/analyze_sweep.py results/grid --plots
```

完全設定は1,000ノード、Aの比率10〜60%、`d/T`が0.01〜3、`k`が1と1.9、
各16seedです。0.1%短縮を全条件で測り、代表点で0.01%短縮も測ります。
基準を含め4,224実行です。小規模マイナーのハッシュは残余を均等分割します。

論文に合わせて`MPR=(reward_share_A-alpha)/alpha`を用い、
`K=(MPR_improved-MPR_baseline)/(epsilon*d/T)`を出力します。
論文のfirst-seen一次予測は送信・受信とも`K=(1-alpha)*(1-2*alpha)`です。
全条件で基準と操作後の遅延行列は三角不等式を満たします。
高遅延の条件は論文の小遅延仮定の外側を調べるもので、論文の反証とは区別します。

`effects.csv`、`baselines.csv`、`paired_seed_effects.csv`、`step_stability.csv`と図を生成します。
送受信の平均はseed内で取り、独立標本数を増やしません。
信頼区間は点ごとの近似95% t区間です。効果0および一次予測との差について、
主実験の送受信別、seed内平均、微小幅の各検定群に分けてHolm補正したp値も保存します。
全ペアで変化がなければ区間・p値は欠測とし、真の効果0とは判定しません。
微小幅の比較も同じseed間で行います。
