# Adapted from KTC-robot/UnagiFestival @ 42c839d (see docs/migration.md).
from enum import Enum, IntEnum
from typing import Self

from types import SimpleNamespace

# Linux input-event-codes.h values; evdev is confined to device.py.
ecodes = SimpleNamespace(
    EV_SYN=0,
    EV_KEY=1,
    EV_ABS=3,
    ABS_X=0,
    ABS_Y=1,
    ABS_RX=3,
    ABS_RY=4,
    ABS_Z=2,
    ABS_RZ=5,
    ABS_HAT0X=16,
    ABS_HAT0Y=17,
    BTN_SOUTH=304,
    BTN_EAST=305,
    BTN_NORTH=307,
    BTN_WEST=308,
    BTN_RIGHT=273,
    BTN_TL=310,
    BTN_TR=311,
    BTN_TL2=312,
    BTN_TR2=313,
    BTN_SELECT=314,
    BTN_START=315,
    BTN_MODE=316,
    BTN_THUMBL=317,
    BTN_THUMBR=318,
)


class EventType(IntEnum):
    """evdevから受信する入力イベントの種別を定義する。"""

    SYN = ecodes.EV_SYN
    KEY = ecodes.EV_KEY
    ABS = ecodes.EV_ABS


class AxisType(Enum):
    """Controllerの絶対軸を操作方法ごとに分類する。"""

    STICK = "STICK"
    TRIGGER = "TRIGGER"
    DPAD = "DPAD"


class AxisCode(Enum):
    """Controller軸のevdevコードと入力分類を対応付ける。"""

    LEFT_STICK_X = (ecodes.ABS_X, AxisType.STICK)
    LEFT_STICK_Y = (ecodes.ABS_Y, AxisType.STICK)
    RIGHT_STICK_X = (ecodes.ABS_RX, AxisType.STICK)
    RIGHT_STICK_Y = (ecodes.ABS_RY, AxisType.STICK)
    LEFT_TRIGGER_L2 = (getattr(ecodes, "ABS_Z", 2), AxisType.TRIGGER)
    RIGHT_TRIGGER_R2 = (getattr(ecodes, "ABS_RZ", 5), AxisType.TRIGGER)
    DPAD_X = (ecodes.ABS_HAT0X, AxisType.DPAD)
    DPAD_Y = (ecodes.ABS_HAT0Y, AxisType.DPAD)

    def __init__(self, code: int, axis_type: AxisType) -> None:
        """enum memberへevdevコードと軸分類を保持する。"""
        self.code = code
        self.axis_type = axis_type

    @classmethod
    def get_by_code(cls, code: int) -> Self | None:
        """raw eventの軸コードをapplication側の軸定義へ変換する。"""
        return next((axis for axis in cls if axis.code == code), None)


class ButtonState(IntEnum):
    """evdevが通知するボタンの押下状態を定義する。"""

    RELEASED = 0
    PRESSED = 1


class ButtonCode(Enum):
    """PS Controllerの各ボタンをLinuxのevdevコードへ対応付ける。"""

    CROSS_BTN = ecodes.BTN_SOUTH
    CIRCLE_BTN = ecodes.BTN_EAST
    TRIANGLE_BTN = ecodes.BTN_NORTH
    SQUARE_BTN = ecodes.BTN_WEST
    L1_BTN = ecodes.BTN_TL
    R1_BTN = ecodes.BTN_TR
    L2_BTN = ecodes.BTN_TL2
    R2_BTN = ecodes.BTN_TR2
    SHARE_BTN = ecodes.BTN_SELECT
    OPTIONS_BTN = ecodes.BTN_START
    PS_BTN = ecodes.BTN_MODE
    L3_BTN = ecodes.BTN_THUMBL
    R3_BTN = ecodes.BTN_THUMBR
    TOUCHPAD_BTN = ecodes.BTN_RIGHT

    @property
    def code(self) -> int:
        """enum memberが保持する数値コードを整数として取得する。"""
        return int(self.value)

    @property
    def display_name(self) -> str:
        """Controller操作を読みやすい名称でログへ表示するために利用する。"""
        return self.name.removesuffix("_BTN")

    @classmethod
    def get_by_code(cls, code: int) -> Self | None:
        """raw eventのボタンコードをapplication側のボタン定義へ変換する。"""
        return next((button for button in cls if button.code == code), None)
