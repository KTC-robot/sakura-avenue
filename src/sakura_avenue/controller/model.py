# Adapted from KTC-robot/UnagiFestival @ 42c839d (see docs/migration.md).
from dataclasses import dataclass, field

from sakura_avenue.controller.enum import (
    AxisCode,
    ButtonCode,
    ButtonState,
)


@dataclass(frozen=True)
class AxisInfo:
    """Controller軸の初期値と正規化に必要な入力範囲を保持する。"""

    value: int | None
    minimum: int
    maximum: int


type AxisValueMap = dict[AxisCode, int]
type AxisInfoMap = dict[AxisCode, AxisInfo]


@dataclass
class ControllerState:
    """Handlerが走行Commandを生成するためのController状態を保持する。"""

    axis_values: AxisValueMap
    axis_info: AxisInfoMap
    buttons: dict[ButtonCode, ButtonState] = field(default_factory=dict)


@dataclass(frozen=True)
class AxisInputEvent:
    """evdevの絶対軸入力をapplicationで扱うイベントとして保持する。"""

    code: AxisCode
    value: int


@dataclass(frozen=True)
class ButtonEvent:
    """evdevのキー入力をapplicationで扱うイベントとして保持する。"""

    code: ButtonCode
    state: ButtonState


type ControllerInputEvent = AxisInputEvent | ButtonEvent
