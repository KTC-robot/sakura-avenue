"""入力処理から独立したUDP送信アダプター。Wi-Fi接続はOSが管理する。"""

import socket
from .protocol import Packet, encode


class UdpTransport:
    """入力処理から独立したUDP送信アダプター。"""

    def __init__(self, host: str, port: int, source_port: int = 0):
        """送信先と送信元ポートを設定する。source_port=0はOSによる自動割り当て。"""
        if not host or not 1 <= port <= 65535:
            raise ValueError("explicit destination required")
        if not 0 <= source_port <= 65535:
            raise ValueError("source port")
        self.destination = (host, port)
        self.socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        try:
            self.socket.bind(("0.0.0.0", source_port))
        except OSError:
            self.socket.close()
            raise

    def send(self, packet: Packet) -> None:
        """パケットを符号化して送信する。到達は保証しない。"""
        self.socket.sendto(encode(packet), self.destination)

    def send_raw(self, data: bytes) -> None:
        """符号化済みbytesを送信先へ送る。到達は保証しない。"""
        self.socket.sendto(data, self.destination)

    def recv(self, timeout: float) -> bytes | None:
        """送信先からの1データグラムを受信する。

        Returns:
            送信先(host, port)と一致するデータグラム。時間切れ・他の送信元はNone。
        """
        self.socket.settimeout(max(timeout, 0.0))
        try:
            data, source = self.socket.recvfrom(2048)
        except (TimeoutError, BlockingIOError):
            return None
        return data if source == self.destination else None

    def close(self) -> None:
        """ソケットを解放する。"""
        self.socket.close()
