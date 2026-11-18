"""入力処理から独立したUDP送信アダプター。Wi-Fi接続はOSが管理する。"""

import socket
from .protocol import Packet, Status, decode_status, encode


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
            self.socket.setblocking(False)
        except OSError:
            self.socket.close()
            raise

    def send(self, packet: Packet) -> None:
        """パケットを符号化して送信する。到達は保証しない。"""
        self.socket.sendto(encode(packet), self.destination)

    def poll_status(self) -> list[Status]:
        """受信側からのハートビートを取り出す。送信先以外・不正なパケットは捨てる。"""
        statuses = []
        for _ in range(16):
            try:
                data, source = self.socket.recvfrom(64)
            except (BlockingIOError, InterruptedError):
                break
            if source != self.destination:
                continue
            try:
                statuses.append(decode_status(data))
            except ValueError:
                continue
        return statuses

    def close(self) -> None:
        """ソケットを解放する。"""
        self.socket.close()
