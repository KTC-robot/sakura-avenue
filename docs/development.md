# コメントの書き方

夏のコードを参考にしつつ、説明は短く統一します。移植の経緯はmigration.md、設計の詳細はarchitecture.mdへ記載します。

## Python docstring

最初の一文に役割を書きます。`About`と`Properties`は使わず、型ヒントで分かる説明は省きます。`Args`は単位や制約、`Returns`は戻り値の意味、`Raises`は重要な例外に限ります。`None`の戻り値は説明しません。

```python
def accept(...) -> bool:
    """指令を検証し、安全状態を更新する。

    Returns:
        更新後がARMEDならTrue。
    """
```

## C++ Doxygen

- `@brief`: 処理の役割を一文で記載する。
- `@param`: 単位や入力条件を記載する。
- `@return`: 戻り値の意味を記載する。voidでは省略する。
- `@note`: 副作用や注意事項に限定する。

```cpp
/**
 * @brief 全モーターの受信期限を検査する。
 * @param now 単調時計[ms]。
 * @return 4台すべて受信済みかつ期限内ならtrue。
 */
bool fresh(uint64_t now, uint64_t timeout_ms = 100) const;
```

同じ内容の通常コメントとDoxygenを重ねません。安全条件・単位・バッファー寿命は残し、長い説明はdocsへ移します。未実装処理は実行コードのコメントアウトではなく、着手条件を添えたTODOで示します。
