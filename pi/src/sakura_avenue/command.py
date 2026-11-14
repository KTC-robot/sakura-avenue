"""夏のHandlerから入力変換を抽出する。出典と変更点はdocs/migration.mdを参照。"""

from dataclasses import dataclass
import math
from .controller.enum import AxisCode
from .controller.model import AxisInfo, ControllerState


@dataclass(frozen=True)
class DriveCommand:
    """正規化走行要求。各軸は±1、+X前・+Y左・+wz反時計回り。"""

    vx: float = 0.0
    vy: float = 0.0
    wz: float = 0.0


def normalize(value: int, info: AxisInfo | None, deadzone: float = 0.08) -> float:
    """軸入力へdeadzoneを適用し、±1に正規化する。

    Returns:
        正規化値。軸範囲が無効なら0。
    """
    if not 0 <= deadzone < 1:
        raise ValueError("invalid deadzone")
    if info is None or info.maximum <= info.minimum:
        return 0.0
    normalized = (value - (info.minimum + info.maximum) / 2) / (
        (info.maximum - info.minimum) / 2
    )
    return 0.0 if abs(normalized) < deadzone else max(-1.0, min(1.0, normalized))


class CommandMapper:
    """コントローラー状態を走行要求へ変換する。"""

    def map(self, state: ControllerState) -> DriveCommand:
        """スティックとトリガーから、slow係数を適用した走行要求を生成する。"""

        def axis(code: AxisCode) -> float:
            info = state.axis_info.get(code)
            value = state.axis_values.get(code)
            if value is None:
                value = info.value if info and info.value is not None else 0
            return normalize(value, info)

        slow = False
        for code in (AxisCode.LEFT_TRIGGER_L2, AxisCode.RIGHT_TRIGGER_R2):
            info = state.axis_info.get(code)
            if info and info.maximum > info.minimum:
                value = state.axis_values.get(code, info.minimum)
                slow |= (value - info.minimum) / (info.maximum - info.minimum) >= 0.1
        scale = 0.25 if slow else 1.0
        # 夏のvy=+LXは右向き。春の+Y左に合わせてLXの符号を反転する。
        return DriveCommand(
            -axis(AxisCode.LEFT_STICK_Y) * scale,
            -axis(AxisCode.LEFT_STICK_X) * scale,
            -axis(AxisCode.RIGHT_STICK_X) * scale,
        )


def valid(command: DriveCommand) -> bool:
    """走行要求を検証する。

    Returns:
        全成分が有限かつ±1以内ならTrue。
    """
    return all(
        math.isfinite(v) and abs(v) <= 1 for v in (command.vx, command.vy, command.wz)
    )
