"""PS入力のプレビューCLI。送信先を明示した場合のみDISARMを送信する。"""

import argparse
import logging
import time
from .command import CommandMapper, DriveCommand
from .controller.device import find_controller
from .controller.enum import ButtonCode, ButtonState
from .controller.model import AxisInputEvent, ControllerState
from .network import UdpTransport
from .protocol import Action, Packet

log = logging.getLogger(__name__)


def main() -> None:
    """PS入力をプレビューし、送信先の指定時だけDISARMを周期送信する。"""
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", help="Explicit test receiver IPv4; omitted = preview")
    parser.add_argument("--port", type=int, default=0)
    parser.add_argument("--verbose", action="store_true")
    parser.add_argument("--source-port", type=int, default=0)
    parser.add_argument("--hz", type=float, default=50.0)
    args = parser.parse_args()
    if not 1 <= args.hz <= 200:
        parser.error("hz must be 1..200")
    logging.basicConfig(level=logging.DEBUG if args.verbose else logging.INFO)
    device = find_controller()
    if device is None:
        raise SystemExit("PS controller not found")
    transport = (
        UdpTransport(args.host, args.port, args.source_port) if args.host else None
    )
    state = ControllerState(
        {k: v.value for k, v in device.axis_info.items() if v.value is not None},
        device.axis_info,
    )
    mapper = CommandMapper()
    sequence = 0
    # 接続直後・再接続直後とも自動ARMしない。現在のCLIはDISARM送信に限定する。
    # TODO: 認証と物理インターロックの統合後、明示的なARM操作を追加する。
    try:
        while True:
            start = time.monotonic()
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
            log.debug("preview demand=%s", mapper.map(state))
            if transport:
                transport.send(Packet(sequence, Action.DISARM))
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
            device.close()
