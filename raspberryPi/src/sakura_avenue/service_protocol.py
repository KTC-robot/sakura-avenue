"""ログ要求・PIゲイン設定用サービスパケット(magic "SC")の符号化。

走行用の20バイトv1とは別系統。仕様の基準はprotocol/specification.md。
"""

import math
import struct
from dataclasses import dataclass
from enum import IntEnum

HEADER = struct.Struct("!2sBBH")
MOTOR = struct.Struct("!hhHBhhh")
SAMPLE_HEAD = struct.Struct("!IIBB")
GAINS_WIRE = struct.Struct("!4f")

KP_CEILING = 10.0
KI_CEILING = 5.0
INTEGRAL_LIMIT_CEILING = 8000.0
CURRENT_CEILING = 3000.0
MAX_RATE_HZ = 50
MAX_LEASE_MS = 5000
MOTOR_COUNT = 4


class ServiceType(IntEnum):
    """サービスパケット種別。"""

    LOG_START = 1
    LOG_STOP = 2
    PARAM_GET = 3
    PARAM_SET = 4
    PARAM_SAVE = 5
    PARAM_RESET = 6
    ACK = 0x81
    LOG_SAMPLE = 0x82


class Status(IntEnum):
    """ACKのstatus。"""

    OK = 0
    BAD_REQUEST = 1
    NOT_DISARMED = 2
    INVALID_VALUE = 3
    STORAGE_ERROR = 4
    UNKNOWN_TYPE = 5


@dataclass(frozen=True)
class Gains:
    """PI速度制御のゲインと電流上限。"""

    kp: float
    ki: float
    integral_limit: float
    max_current: float

    def validate(self) -> None:
        """有限・0以上・コンパイル時上限以内を検査する。

        Raises:
            ValueError: 範囲外の場合。ESP32側の検証と同じ上限。
        """
        for name, value, ceiling in (
            ("kp", self.kp, KP_CEILING),
            ("ki", self.ki, KI_CEILING),
            ("integral_limit", self.integral_limit, INTEGRAL_LIMIT_CEILING),
            ("max_current", self.max_current, CURRENT_CEILING),
        ):
            if not math.isfinite(value) or not 0 <= value <= ceiling:
                raise ValueError(f"{name} must be 0..{ceiling:g}")


@dataclass(frozen=True)
class Ack:
    """要求に対する応答。gainsは応答時点の現在値。"""

    req_id: int
    status: Status
    gains: Gains


@dataclass(frozen=True)
class MotorLog:
    """1モーター分のログ値。"""

    rpm: int
    current: int
    angle: int
    temperature: int
    target_rpm: int
    command: int
    integral: int


@dataclass(frozen=True)
class LogSample:
    """ESP32が送るログ1件。fresh_maskのbit iがモーターiのフィードバック有効。"""

    sequence: int
    time_ms: int
    state: int
    fresh_mask: int
    motors: tuple[MotorLog, ...]


def _header(type_: ServiceType, req_id: int) -> bytes:
    if not 0 <= req_id <= 0xFFFF:
        raise ValueError("req_id")
    return HEADER.pack(b"SC", 1, type_, req_id)


def encode_log_start(req_id: int, rate_hz: int, lease_ms: int) -> bytes:
    """ログ開始要求。leaseを更新しない限りESP32は送信を止める。"""
    if not 1 <= rate_hz <= MAX_RATE_HZ or not 1 <= lease_ms <= MAX_LEASE_MS:
        raise ValueError("rate_hz or lease_ms out of range")
    return _header(ServiceType.LOG_START, req_id) + struct.pack(
        "!BH", rate_hz, lease_ms
    )


def encode_simple(type_: ServiceType, req_id: int) -> bytes:
    """payloadなしの要求(LOG_STOP/PARAM_GET/PARAM_SAVE/PARAM_RESET)。"""
    if type_ not in (
        ServiceType.LOG_STOP,
        ServiceType.PARAM_GET,
        ServiceType.PARAM_SAVE,
        ServiceType.PARAM_RESET,
    ):
        raise ValueError("type has payload")
    return _header(type_, req_id)


def encode_param_set(req_id: int, gains: Gains) -> bytes:
    """ゲイン設定要求。範囲外は送信前に拒否する。"""
    gains.validate()
    return _header(ServiceType.PARAM_SET, req_id) + GAINS_WIRE.pack(
        gains.kp, gains.ki, gains.integral_limit, gains.max_current
    )


def decode_ack(data: bytes) -> Ack:
    """ACKをdecodeする。

    Raises:
        ValueError: 長さ・ヘッダー・種別が不正な場合。
    """
    if len(data) != HEADER.size + 1 + GAINS_WIRE.size:
        raise ValueError("length")
    magic, version, type_, req_id = HEADER.unpack_from(data)
    if magic != b"SC" or version != 1 or type_ != ServiceType.ACK:
        raise ValueError("not an ack")
    return Ack(
        req_id,
        Status(data[HEADER.size]),
        Gains(*GAINS_WIRE.unpack_from(data, HEADER.size + 1)),
    )


def decode_sample(data: bytes) -> LogSample:
    """ログ1件をdecodeする。

    Raises:
        ValueError: 長さ・ヘッダー・種別が不正な場合。
    """
    expected = HEADER.size + SAMPLE_HEAD.size + MOTOR_COUNT * MOTOR.size
    if len(data) != expected:
        raise ValueError("length")
    magic, version, type_, _ = HEADER.unpack_from(data)
    if magic != b"SC" or version != 1 or type_ != ServiceType.LOG_SAMPLE:
        raise ValueError("not a sample")
    seq, time_ms, state, mask = SAMPLE_HEAD.unpack_from(data, HEADER.size)
    offset = HEADER.size + SAMPLE_HEAD.size
    motors = tuple(
        MotorLog(*MOTOR.unpack_from(data, offset + i * MOTOR.size))
        for i in range(MOTOR_COUNT)
    )
    return LogSample(seq, time_ms, state, mask, motors)


def decode_response(data: bytes) -> Ack | LogSample | None:
    """ACKまたはログを判別してdecodeする。不正なものはNone。"""
    try:
        if len(data) >= HEADER.size and data[3] == ServiceType.LOG_SAMPLE:
            return decode_sample(data)
        return decode_ack(data)
    except ValueError:
        return None
