# C言語で作成したチャットWebサーバ

クライアントからのリクエストを解析し、Webページを作成し、レスポンスを返すサーバをC言語で立てます。
poll関数によって複数のクライアントソケットを維持し、管理を行います。

## 使用技術

- C言語
- POSIXソケットAPI
- HTML

## 機能概要

- 複数人でのチャットのやり取り
- 接続人数の表示
- 接続が切れた人のグレー表示
