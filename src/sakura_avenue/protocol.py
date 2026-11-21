"""UDP v1の符号化と検証。仕様の基準はprotocol/specification.md。"""

import struct
from dataclasses import dataclass
from enum import IntEnum
from .command import DriveCommand, valid


class Action(IntEnum):
    """UDP v1の操作種別。"""

    DISARM = 0
    ARM = 1
    DRIVE = 2
    STOP = 3


WIRE = struct.Struct("!2sBBIfff")


@dataclass(frozen=True)
class Packet:
    """連番・操作種別・走行要求を持つ通信パケット。"""

    sequence: int
    action: Action
    command: DriveCommand = DriveCommand()


def encode(packet: Packet) -> bytes:
    """パケットをbig endianの20バイトへ符号化する。

    Raises:
        ValueError: 操作・連番・要求値が仕様外の場合。
    """
    if not valid(packet.command) or not 0 <= packet.sequence <= 0xFFFFFFFF:
        raise ValueError("invalid command or sequence")
    action = Action(packet.action)
    if action != Action.DRIVE and packet.command != DriveCommand():
        raise ValueError("non-drive command must be neutral")
    return WIRE.pack(
        b"SA",
        1,
        action,
        packet.sequence,
        packet.command.vx,
        packet.command.vy,
        packet.command.wz,
    )


def decode(data: bytes) -> Packet:
    """受信bytesを検証し、Packetへ変換する。

    Raises:
        ValueError: 長さ・ヘッダー・要求値が仕様外の場合。
    """
    if len(data) != WIRE.size:
        raise ValueError("length")
    magic, version, action, seq, vx, vy, wz = WIRE.unpack(data)
    if magic != b"SA" or version != 1:
        raise ValueError("magic/version")
    packet = Packet(seq, Action(action), DriveCommand(vx, vy, wz))
    encode(packet)
    return packet


def newer(sequence: int, previous: int) -> bool:
    """uint32のwrapを考慮して連番を比較する。

    Returns:
        差分が0より大きく半周未満ならTrue。
    """
    return 0 < ((sequence - previous) & 0xFFFFFFFF) < 0x80000000


STATUS_WIRE = struct.Struct("!2sBBBBI")


class DeviceState(IntEnum):
    """受信側(ESP)の安全状態。値は仕様のstateと一致する。"""

    DISARM = 0
    ARMED = 1
    STOP = 2
    FAULT = 3


@dataclass(frozen=True)
class Status:
    """受信側から1秒ごとに届くハートビート。"""

    state: DeviceState
    flags: int
    counter: int

    @property
    def wifi_ok(self) -> bool:
        """Wi-Fiが接続済みか。"""
        return bool(self.flags & 1)

    @property
    def can_ok(self) -> bool:
        """CANが健全か。"""
        return bool(self.flags & 2)

    @property
    def feedback_ok(self) -> bool:
        """全C620のフィードバックが期限内か。"""
        return bool(self.flags & 4)

    @property
    def interlock_ok(self) -> bool:
        """物理インターロックが許可か。"""
        return bool(self.flags & 8)

    @property
    def motor_ok(self) -> bool:
        """モーター系が使える状態か(CAN健全かつ全C620のフィードバック有効)。"""
        return self.can_ok and self.feedback_ok


def encode_status(status: Status) -> bytes:
    """ハートビートを10バイトへ符号化する(テスト・疑似受信側用)。"""
    return STATUS_WIRE.pack(
        b"ST", 1, DeviceState(status.state), status.flags & 0xFF, 0, status.counter
    )


def decode_status(data: bytes) -> Status:
    """受信bytesをハートビートへ変換する。

    Raises:
        ValueError: 長さ・magic・version・stateが仕様外の場合。
    """
    if len(data) != STATUS_WIRE.size:
        raise ValueError("length")
    magic, version, state, flags, reserved, counter = STATUS_WIRE.unpack(data)
    if magic != b"ST" or version != 1 or reserved != 0:
        raise ValueError("magic/version")
    return Status(DeviceState(state), flags, counter)
