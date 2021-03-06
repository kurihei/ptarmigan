# [BOLT](https://github.com/lightningnetwork/lightning-rfc) compliant

## BOLT#1

* Setup Messages
  * `init` : `initial_routing_sync` = 1 のみ送信。受信したfeaturesは無視。
  * `error` : 受信結果をログに出す
  * `ping` and `pong`
    * 無送信状態が60秒継続すると、 `ping` を送信する
    * `pong`受信の際、前回の`ping`に対応するかチェックしない

## BOLT#2

* Channel Establishment
  * `feerate_per_kw`は `estimatefee 6` の4分の1
  * `feerate_per_kw`の大きさチェックをしていない

* Channel Close
  * FEEは `estimatefee` の結果を使用している

* Normal Operation
  * エラーメッセージに対応していない(箇所によってはabortする)。
  * `commitment_signed` がなかった場合の取消にまだ対応できておらず、受信メッセージをすぐに反映させている。

* Message Retransmission
  * `c-lightning`とEstablish後、paymenetせずに切断し、再接続すると切断される([#138](https://github.com/nayutaco/ptarmigan/issues/138))

## BOLT#4

* Failure Messagesは実装中(固定値を返す)

## BOLT#5

* Mutual Closeの完了チェック(`closing_tx` の展開チェック)を `getblock` でのTXID監視に変更
  * よって、1ブロック以上マイニングされないと完了しない

## BOLT#7

* Initial Syncは行っていない
* Rebroadcastingは動作未確認
* Routingは `boost` の `dijkstra_shortest_paths()` を使用

## BOLT#11

* `description`は`ptarmigan`固定