"""コントローラー接続からARM/DRIVE送信までの手順を決める状態機械。"""

import math
from enum import Enum, auto
from .command import DriveCommand
from .protocol import Action

NEUTRAL = DriveCommand()


class Phase(Enum):
    """接続確立の段階。"""

    LINKING = auto()
    ARMING = auto()
    DRIVING = auto()


class ArmSession:
    """コントローラー1接続分のリンク確立とARM送信を管理する。

    接続ごとに新しく作る。LINKINGで、スティックがニュートラルのままlink_time
    続くまでDISARMを送る。成立後にARMをarm_packets回送り、以降はDRIVEを送る。
    ARM中にスティックが動いたら、LINKINGからやり直す。受信側がARMEDでないと
    報告したときも、LINKINGからやり直す(on_status)。
    """

    # ARM送信の完了から、受信側の状態報告を信用するまでの猶予[s]。
    # 送信前に作られた古いハートビートでやり直さないための余裕。
    STATUS_GRACE_S = 0.5

    def __init__(
        self,
        link_time: float = 1.0,
        arm_packets: int = 5,
        auto_arm: bool = True,
    ):
        """link_timeはリンク成立とみなす安定時間[s]。auto_arm=FalseはDISARMのみ。"""
        if not math.isfinite(link_time) or link_time < 0.2:
            raise ValueError("link_time must be >= 0.2")
        if arm_packets < 1:
            raise ValueError("arm_packets must be >= 1")
        self.link_time = link_time
        self.arm_packets = arm_packets
        self.auto_arm = auto_arm
        self.phase = Phase.LINKING
        self._since: float | None = None
        self._sent = 0
        self._driving_since: float | None = None

    def on_status(self, now: float, armed: bool) -> bool:
        """受信側の状態報告を反映する。ARMEDでなければリンクからやり直す。

        受信側が起動途中やFAULTでARMを受け付けられなかった場合や、走行中に
        FAULTへ落ちた場合に、DISARM→ARMを再送して復帰させる。

        Args:
            now: 単調時計[s]。
            armed: 受信側の状態がARMEDか。

        Returns:
            やり直しを始めたらTrue。
        """
        if (
            armed
            or self.phase != Phase.DRIVING
            or self._driving_since is None
            or now - self._driving_since < self.STATUS_GRACE_S
        ):
            return False
        self.phase = Phase.LINKING
        self._since = None
        self._sent = 0
        self._driving_since = None
        return True

    def update(self, now: float, command: DriveCommand) -> tuple[Action, DriveCommand]:
        """今回送るべき操作と走行要求を返す。

        Args:
            now: 単調時計[s]。
            command: 現在のコントローラー入力から作った走行要求。
        """
        if self.phase == Phase.DRIVING:
            return Action.DRIVE, command
        if command != NEUTRAL:
            # 動かした状態では成立させない。ニュートラルに戻してからやり直す。
            self.phase = Phase.LINKING
            self._since = now
            self._sent = 0
            return Action.DISARM, NEUTRAL
        if self._since is None:
            self._since = now
        if self.phase == Phase.LINKING:
            if not self.auto_arm or now - self._since < self.link_time:
                return Action.DISARM, NEUTRAL
            self.phase = Phase.ARMING
            self._sent = 0
        self._sent += 1
        if self._sent >= self.arm_packets:
            self.phase = Phase.DRIVING
            self._driving_since = now
        return Action.ARM, NEUTRAL
