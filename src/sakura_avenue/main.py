"""PS操縦CLI。送信先を明示した場合のみ送信し、リンク成立後にARMする。"""

import argparse
import logging
import time
from .command import CommandMapper, DriveCommand
from .controller.device import find_controller
from .controller.enum import ButtonCode, ButtonState
from .controller.model import AxisInputEvent, ControllerState
from .network import UdpTransport
from .protocol import Action, DeviceState, Packet
from .session import ArmSession

log = logging.getLogger(__name__)
STATUS_TIMEOUT_S = 3.0  # ハートビート(1秒周期)がこの時間途絶えたら警告


def _new_state(device) -> ControllerState:
    """デバイスの現在軸値から入力状態を作る。"""
    return ControllerState(
        {k: v.value for k, v in device.axis_info.items() if v.value is not None},
        device.axis_info,
    )


def main() -> None:
    """PS入力を読み、送信先の指定時にリンク成立後のARMとDRIVEを送信する。

    コントローラー接続後、スティックがニュートラルのままlink-time続いたら
    ARMを送る。切断・終了時はDISARMを送って終了する。
    """
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", help="Explicit test receiver IPv4; omitted = preview")
    parser.add_argument("--port", type=int, default=0)
    parser.add_argument("--verbose", action="store_true")
    parser.add_argument("--source-port", type=int, default=0)
    parser.add_argument("--hz", type=float, default=50.0)
    parser.add_argument(
        "--link-time",
        type=float,
        default=1.0,
        help="seconds of neutral sticks after connect before ARM (>= 0.2)",
    )
    parser.add_argument("--arm-packets", type=int, default=5)
    parser.add_argument(
        "--no-arm", action="store_true", help="send DISARM only; never ARM/DRIVE"
    )
    args = parser.parse_args()
    if not 1 <= args.hz <= 200:
        parser.error("hz must be 1..200")
    try:
        session_args = dict(
            link_time=args.link_time,
            arm_packets=args.arm_packets,
            auto_arm=not args.no_arm,
        )
        ArmSession(**session_args)
    except ValueError as e:
        parser.error(str(e))
    logging.basicConfig(level=logging.DEBUG if args.verbose else logging.INFO)
    transport = (
        UdpTransport(args.host, args.port, args.source_port) if args.host else None
    )
    device = None
    state = None
    session = None
    mapper = CommandMapper()
    sequence = 0
    last_scan = float("-inf")
    last_status = time.monotonic()
    no_status_warned = False
    try:
        while True:
            start = time.monotonic()
            if device is None and start - last_scan >= 2.0:
                last_scan = start
                device = find_controller()
                if device is not None:
                    state = _new_state(device)
                    session = ArmSession(**session_args)
                    log.info("controller connected; linking (neutral sticks)")
            if transport:
                for status in transport.poll_status():
                    last_status, no_status_warned = start, False
                    log.info(
                        "STATUS=%s モータ%s",
                        status.state.name,
                        "OK" if status.motor_ok else "NG",
                    )
                    if session is not None and session.on_status(
                        start, status.state == DeviceState.ARMED
                    ):
                        log.warning("受信側がARMEDでないため、DISARMからやり直します")
                if not no_status_warned and start - last_status > STATUS_TIMEOUT_S:
                    no_status_warned = True
                    log.warning("STATUS応答なし(%.0f秒)", STATUS_TIMEOUT_S)
            action, command = Action.DISARM, DriveCommand()
            if device is not None:
                for event in device.read_events(0):
                    if isinstance(event, AxisInputEvent):
                        state.axis_values[event.code] = event.value
                    else:
                        state.buttons[event.code] = event.state
                        log.info("button=%s state=%s", event.code, event.state)
                        if (
                            event.code == ButtonCode.PS_BTN
                            and event.state == ButtonState.PRESSED
                        ):
                            raise KeyboardInterrupt
                demand = mapper.map(state)
                log.debug("preview demand=%s", demand)
                before = session.phase
                action, command = session.update(start, demand)
                if session.phase != before:
                    log.info("session phase: %s -> %s", before.name, session.phase.name)
            if transport:
                transport.send(Packet(sequence, action, command))
                sequence = (sequence + 1) & 0xFFFFFFFF
            time.sleep(max(0, 1 / args.hz - (time.monotonic() - start)))
    except (KeyboardInterrupt, OSError):
        log.info("controller stopped/disconnected; DISARM")
    finally:
        try:
            if transport:
                try:
                    transport.send(Packet(sequence, Action.DISARM, DriveCommand()))
                finally:
                    transport.close()
        finally:
            if device is not None:
                device.close()
