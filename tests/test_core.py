import json
import math
import socket
from pathlib import Path
import pytest
from sakura_avenue.command import CommandMapper, DriveCommand, normalize
from sakura_avenue.controller.enum import AxisCode, ButtonState
from sakura_avenue.controller.model import AxisInfo, ControllerState
from sakura_avenue.network import UdpTransport
from sakura_avenue.protocol import Action, Packet, decode, encode, newer
from sakura_avenue.safety import Safety, State


@pytest.mark.parametrize(
    "value,expected", [(0, -1), (128, 0), (255, 1), (130, 0), (-1, -1), (256, 1)]
)
def test_normalize(value, expected):
    assert normalize(value, AxisInfo(128, 0, 255)) == expected


def test_missing_invalid_axis():
    assert normalize(2, None) == 0
    assert normalize(2, AxisInfo(0, 1, 1)) == 0
    with pytest.raises(ValueError):
        normalize(0, None, 1)


def test_mapping():
    infos = {
        code: AxisInfo(127, 0, 254)
        for code in (
            AxisCode.LEFT_STICK_X,
            AxisCode.LEFT_STICK_Y,
            AxisCode.RIGHT_STICK_X,
        )
    }
    state = ControllerState(
        {AxisCode.LEFT_STICK_X: 0, AxisCode.LEFT_STICK_Y: 0, AxisCode.RIGHT_STICK_X: 0},
        infos,
    )
    assert CommandMapper().map(state) == DriveCommand(1, 1, 1)
    state.axis_info[AxisCode.LEFT_TRIGGER_L2] = AxisInfo(0, 0, 255)
    state.axis_values[AxisCode.LEFT_TRIGGER_L2] = 255
    assert CommandMapper().map(state) == DriveCommand(0.25, 0.25, 0.25)
    assert ButtonState.PRESSED == 1 and ButtonState.RELEASED == 0


VECTORS = json.loads(
    (Path(__file__).resolve().parents[1] / "protocol/golden_vectors.json").read_text()
)


@pytest.mark.parametrize("v", VECTORS)
def test_vectors(v):
    packet = Packet(
        v["sequence"], Action(v["action"]), DriveCommand(v["vx"], v["vy"], v["wz"])
    )
    assert encode(packet).hex() == v["hex"]
    assert decode(bytes.fromhex(v["hex"])) == packet


@pytest.mark.parametrize("offset,value", [(0, 0), (2, 2), (3, 255)])
def test_bad_header(offset, value):
    b = bytearray(encode(Packet(0, Action.DISARM)))
    b[offset] = value
    with pytest.raises(ValueError):
        decode(bytes(b))


@pytest.mark.parametrize("value", [math.nan, math.inf, -math.inf, 1.1])
def test_invalid_values(value):
    with pytest.raises(ValueError):
        encode(Packet(0, Action.DRIVE, DriveCommand(value, 0, 0)))


def test_lengths():
    data = encode(Packet(0, Action.DISARM))
    for n in range(20):
        with pytest.raises(ValueError):
            decode(data[:n])
    with pytest.raises(ValueError):
        decode(data + b"0")
    with pytest.raises(ValueError):
        encode(Packet(0, Action.ARM, DriveCommand(1, 0, 0)))


def test_safety_rearm_and_timeout():
    safety = Safety()
    assert not safety.accept(Packet(0, Action.ARM), 0)
    assert not safety.accept(Packet(1, Action.DISARM), 0.01)
    assert safety.accept(Packet(2, Action.ARM), 0.02)
    safety.tick(0.201)
    assert safety.state == State.FAULT
    assert not safety.accept(Packet(3, Action.ARM), 0.21)
    safety.accept(Packet(4, Action.DISARM), 0.22)
    assert safety.accept(Packet(5, Action.ARM), 0.23)
    assert not safety.accept(Packet(5, Action.DRIVE), 0.24)
    assert safety.state == State.FAULT
    safety.accept(Packet(6, Action.DISARM), 0.25)
    safety.accept(Packet(7, Action.ARM), 0.26)
    safety.accept(Packet(8, Action.STOP), 0.27)
    assert safety.state == State.STOP
    assert not safety.accept(Packet(9, Action.ARM), 0.28)
    safety.tick(0.29, False)
    assert safety.state == State.FAULT


def test_wrap():
    assert newer(0, 0xFFFFFFFF) and not newer(1, 1) and not newer(0xFFFFFFFF, 0)


def test_udp_localhost():
    receiver = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    receiver.bind(("127.0.0.1", 0))
    receiver.settimeout(1)
    transport = UdpTransport(*receiver.getsockname())
    try:
        packet = Packet(1, Action.DISARM)
        transport.send(packet)
        assert decode(receiver.recv(100)) == packet
    finally:
        transport.close()
        receiver.close()
