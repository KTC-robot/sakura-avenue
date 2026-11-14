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
