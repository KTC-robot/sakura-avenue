"""受信側安全状態の参照実装。FAULT復帰にはニュートラルDISARMが必要。"""

from enum import Enum, auto
import math
from .command import DriveCommand
from .protocol import Action, Packet, newer, encode


class State(Enum):
    """駆動禁止・許可・通常停止・異常停止の状態。"""

    DISARM = auto()
    ARMED = auto()
    STOP = auto()
    FAULT = auto()


class Safety:
    """受信期限と再許可を管理する、C++状態機械の参照実装。"""

    def __init__(self, timeout: float = 0.18):
        """駆動禁止で開始する。timeoutは受信期限[s]。"""
        if not math.isfinite(timeout) or timeout <= 0:
            raise ValueError("timeout")
        self.timeout = timeout
        self.state = State.DISARM
        self.last_time: float | None = None
        self.sequence: int | None = None
        self.neutral = False

    def fault(self) -> None:
        """FAULTへ移り、ニュートラル確認を取り消す。"""
        self.state = State.FAULT
        self.neutral = False

    def tick(self, now: float, healthy: bool = True) -> None:
        """受信期限と健全性を監視する。nowは単調時計[s]。"""
        if (
            not math.isfinite(now)
            or not healthy
            or (
                self.last_time is not None
                and (now < self.last_time or now - self.last_time > self.timeout)
            )
        ):
            self.fault()

    def accept(self, packet: Packet, now: float, healthy: bool = True) -> bool:
        """指令を検証し、安全状態を更新する。

        Args:
            now: 受信時の単調時計[s]。
            healthy: 通信・CAN・インターロックを含む健全性。

        Returns:
            更新後がARMEDならTrue。パケット受理の成否ではない。
        """
        self.tick(now, healthy)
        try:
            encode(packet)
        except (ValueError, TypeError):
            self.fault()
            return False
        if not math.isfinite(now):
            self.fault()
            return False
        if self.last_time is not None and now < self.last_time:
            self.fault()
            return False
        if self.sequence is not None and not newer(packet.sequence, self.sequence):
            self.fault()
            return False
        self.sequence, self.last_time = packet.sequence, now
        neutral = packet.command == DriveCommand()
        if not healthy:
            self.fault()
            return False
        if packet.action == Action.DISARM:
            self.state, self.neutral = State.DISARM, neutral
        elif packet.action == Action.STOP:
            self.state, self.neutral = State.STOP, False
        elif packet.action == Action.ARM:
            if self.state == State.DISARM and self.neutral and neutral:
                self.state = State.ARMED
                self.neutral = False
        elif self.state != State.ARMED:
            self.neutral = False
        return self.state == State.ARMED
