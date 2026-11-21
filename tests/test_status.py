import socket
from types import SimpleNamespace
import pytest
from sakura_avenue.network import UdpTransport
from sakura_avenue.protocol import (
    DeviceState,
    Status,
    decode_status,
    encode_status,
)

ARMED = "535401010f0001020304"
FAULT = "53540103010000000000"


def test_status_vectors():
    assert encode_status(Status(DeviceState.ARMED, 0x0F, 0x01020304)).hex() == ARMED
    assert encode_status(Status(DeviceState.FAULT, 0x01, 0)).hex() == FAULT
    s = decode_status(bytes.fromhex(ARMED))
    assert s.state == DeviceState.ARMED and s.motor_ok and s.interlock_ok
    f = decode_status(bytes.fromhex(FAULT))
    assert f.wifi_ok and not f.motor_ok and not f.can_ok and not f.feedback_ok


@pytest.mark.parametrize(
    "data",
    [
        b"",
        bytes.fromhex(ARMED)[:-1],
        bytes.fromhex("5341010101" + "00" + "00000000"),  # magic
        bytes.fromhex("5354020101" + "00" + "00000000"),  # version
        bytes.fromhex("5354010101" + "01" + "00000000"),  # reserved
        bytes.fromhex("5354010901" + "00" + "00000000"),  # state
    ],
)
def test_status_rejects_invalid(data):
    with pytest.raises(ValueError):
        decode_status(data)


def test_motor_ok_needs_can_and_feedback():
    assert Status(DeviceState.DISARM, 0b0110, 0).motor_ok
    assert not Status(DeviceState.DISARM, 0b0100, 0).motor_ok
    assert not Status(DeviceState.DISARM, 0b0010, 0).motor_ok


def test_poll_status_over_udp():
    esp = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    esp.bind(("127.0.0.1", 0))
    stranger = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    stranger.bind(("127.0.0.1", 0))
    transport = UdpTransport(*esp.getsockname())
    pi = ("127.0.0.1", transport.socket.getsockname()[1])
    try:
        assert transport.poll_status() == []  # ノンブロッキング
        esp.sendto(bytes.fromhex(ARMED), pi)
        stranger.sendto(bytes.fromhex(FAULT), pi)  # 送信先以外は捨てる
        esp.sendto(b"junk", pi)
        import time

        time.sleep(0.05)
        got = transport.poll_status()
        assert [g.state for g in got] == [DeviceState.ARMED]
    finally:
        transport.close()
        esp.close()
        stranger.close()


def test_cli_logs_status_and_warns_on_silence(monkeypatch, caplog):
    import logging
    import sakura_avenue.main as pilot

    clock, calls = {"t": 0.0}, {"n": 0}
    beats = [Status(DeviceState.ARMED, 0x0F, 0), Status(DeviceState.DISARM, 0x04, 1)]

    def read_events(timeout):
        calls["n"] += 1
        if calls["n"] > 300:  # 50Hzで約6秒
            raise KeyboardInterrupt
        return []

    fake = SimpleNamespace(axis_info={}, read_events=read_events, close=lambda: None)

    class Transport:
        def __init__(self, *args):
            pass

        def send(self, packet):
            pass

        def poll_status(self):
            # 最初の2周期だけハートビートが届き、その後は途絶える
            return [beats.pop(0)] if beats and calls["n"] < 3 else []

        def close(self):
            pass

    monkeypatch.setattr(pilot, "find_controller", lambda: fake)
    monkeypatch.setattr(pilot, "UdpTransport", Transport)
    monkeypatch.setattr(pilot.time, "monotonic", lambda: clock["t"])
    monkeypatch.setattr(
        pilot.time, "sleep", lambda s: clock.__setitem__("t", clock["t"] + 0.02)
    )
    monkeypatch.setattr(
        "sys.argv", ["sakura-pilot", "--host", "127.0.0.1", "--port", "50000"]
    )
    with caplog.at_level(logging.INFO):
        pilot.main()
    text = caplog.text
    assert "STATUS=ARMED モータOK" in text
    assert "STATUS=DISARM モータNG" in text
    assert text.count("STATUS応答なし") == 1
