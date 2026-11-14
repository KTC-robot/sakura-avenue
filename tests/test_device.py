"""Optional evdev adapter checks with synthetic events and device handles."""

from types import SimpleNamespace
import pytest

pytest.importorskip("evdev")
from evdev import InputEvent, ecodes
from sakura_avenue.controller import device
from sakura_avenue.controller.enum import AxisCode, ButtonCode, ButtonState
from sakura_avenue.controller.model import AxisInputEvent, ButtonEvent, ControllerState


@pytest.mark.parametrize(
    "value,expected",
    [(0, ButtonState.RELEASED), (1, ButtonState.PRESSED), (2, None), (-1, None)],
)
def test_button_parse(value, expected):
    assert device.parse_button_state(value) == expected


def test_events():
    assert device.parse_input_event(
        InputEvent(0, 0, ecodes.EV_ABS, ecodes.ABS_X, 123)
    ) == AxisInputEvent(AxisCode.LEFT_STICK_X, 123)
    event = device.parse_input_event(
        InputEvent(0, 0, ecodes.EV_KEY, ecodes.BTN_SOUTH, 1)
    )
    assert event == ButtonEvent(ButtonCode.CROSS_BTN, ButtonState.PRESSED)
    state = ControllerState({}, {})
    state.buttons[event.code] = event.state
    assert state.buttons[ButtonCode.CROSS_BTN] == ButtonState.PRESSED
    assert (
        device.parse_input_event(InputEvent(0, 0, ecodes.EV_KEY, ecodes.BTN_SOUTH, 2))
        is None
    )
    assert device.parse_input_event(InputEvent(0, 0, ecodes.EV_SYN, 0, 0)) is None


def test_scoring():
    axes = {ecodes.ABS_X, ecodes.ABS_Y, ecodes.ABS_RX}
    keys = {ecodes.BTN_SOUTH}
    assert device._calculate_device_score(
        "sony dualsense", axes, keys
    ) > device._calculate_device_score("sony dualsense touchpad", axes, keys)


def test_discovery_closes_rejected_devices(monkeypatch):
    class Fake:
        def __init__(self, name, gamepad):
            self.name = name
            self.path = name
            self.closed = False
            self.gamepad = gamepad

        def capabilities(self, absinfo):
            return {
                ecodes.EV_ABS: [ecodes.ABS_X, ecodes.ABS_Y, ecodes.ABS_RX]
                if self.gamepad
                else [],
                ecodes.EV_KEY: [ecodes.BTN_SOUTH] if self.gamepad else [],
            }

        def close(self):
            self.closed = True

    mouse = Fake("mouse", False)
    pad = Fake("sony dualsense", True)
    monkeypatch.setattr(device, "list_devices", lambda: ["mouse", "pad"])
    monkeypatch.setattr(
        device, "InputDevice", lambda path: {"mouse": mouse, "pad": pad}[path]
    )
    found = device.find_controller()
    assert found is not None and found.name == pad.name and mouse.closed
    found.close()
    assert pad.closed


def test_syn_dropped_requires_reconnect(monkeypatch):
    fake = SimpleNamespace(
        fd=10, read=lambda: [InputEvent(0, 0, ecodes.EV_SYN, ecodes.SYN_DROPPED, 0)]
    )
    controller = object.__new__(device.ControllerDevice)
    controller._device = fake
    monkeypatch.setattr(device.select, "select", lambda *args: ([10], [], []))
    with pytest.raises(OSError):
        controller.read_events(0)


def test_cli_disconnect_disarms_and_releases(monkeypatch):
    import sakura_avenue.main as pilot

    packets = []
    released = []

    def unplugged(timeout):
        raise OSError("unplugged controller")

    fake = SimpleNamespace(
        axis_info={}, read_events=unplugged, close=lambda: released.append("controller")
    )

    class Transport:
        def __init__(self, *args):
            pass

        def send(self, packet):
            packets.append(packet)

        def close(self):
            released.append("transport")

    monkeypatch.setattr(pilot, "find_controller", lambda: fake)
    monkeypatch.setattr(pilot, "UdpTransport", Transport)
    monkeypatch.setattr(
        "sys.argv", ["sakura-pilot", "--host", "127.0.0.1", "--port", "50000"]
    )
    pilot.main()
    assert len(packets) == 1 and packets[0].action == pilot.Action.DISARM
    assert released == ["transport", "controller"]
