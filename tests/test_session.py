from types import SimpleNamespace
import pytest
from sakura_avenue.command import DriveCommand
from sakura_avenue.protocol import Action, Packet
from sakura_avenue.safety import Safety, State
from sakura_avenue.session import ArmSession, Phase

NEUTRAL = DriveCommand()
MOVED = DriveCommand(0.5, 0.0, 0.0)


def run(session, times, command=NEUTRAL):
    return [session.update(t, command) for t in times]


def test_disarm_until_link_time_then_arm_then_drive():
    s = ArmSession(link_time=1.0, arm_packets=3)
    early = run(s, [0.0, 0.5, 0.99])
    assert all(a == Action.DISARM for a, _ in early)
    arms = run(s, [1.0, 1.02, 1.04])
    assert [a for a, _ in arms] == [Action.ARM] * 3
    assert all(c == NEUTRAL for _, c in arms)
    assert s.phase == Phase.DRIVING
    action, cmd = s.update(1.06, MOVED)
    assert (action, cmd) == (Action.DRIVE, MOVED)


def test_moving_sticks_restart_link_timer():
    s = ArmSession(link_time=1.0)
    s.update(0.0, NEUTRAL)
    assert s.update(0.9, MOVED) == (Action.DISARM, NEUTRAL)
    # 動かした時点から数え直す。0.9+1.0に届くまでARMしない。
    assert s.update(1.5, NEUTRAL)[0] == Action.DISARM
    assert s.update(1.89, NEUTRAL)[0] == Action.DISARM
    assert s.update(1.91, NEUTRAL)[0] == Action.ARM


def test_move_during_arming_aborts_to_linking():
    s = ArmSession(link_time=1.0, arm_packets=5)
    s.update(0.0, NEUTRAL)
    assert s.update(1.0, NEUTRAL)[0] == Action.ARM
    assert s.phase == Phase.ARMING
    assert s.update(1.02, MOVED) == (Action.DISARM, NEUTRAL)
    assert s.phase == Phase.LINKING


def test_no_arm_never_arms():
    s = ArmSession(link_time=0.2, auto_arm=False)
    acts = {a for a, _ in run(s, [i * 0.1 for i in range(50)])}
    assert acts == {Action.DISARM}


@pytest.mark.parametrize(
    "kwargs", [{"link_time": 0.1}, {"link_time": float("nan")}, {"arm_packets": 0}]
)
def test_invalid_args(kwargs):
    with pytest.raises(ValueError):
        ArmSession(**kwargs)


def test_receiver_reaches_armed_and_drives():
    """参照安全状態機械が、この送信手順でARMEDになりDRIVEを受け付ける。"""
    s = ArmSession(link_time=0.5, arm_packets=3)
    safety = Safety()
    seq, now, armed_at = 0, 0.0, None
    for _ in range(60):
        action, cmd = s.update(now, NEUTRAL)
        armed = safety.accept(Packet(seq, action, cmd), now)
        if armed and armed_at is None:
            armed_at = now
        seq, now = seq + 1, now + 0.02
    assert armed_at is not None and armed_at >= 0.5
    assert safety.state == State.ARMED
    assert safety.accept(Packet(seq, *s.update(now, MOVED)), now) is True


def test_cli_waits_links_arms_and_drives(monkeypatch):
    import sakura_avenue.main as pilot

    packets, released = [], []
    clock = {"t": 100.0}
    calls = {"n": 0}

    def read_events(timeout):
        calls["n"] += 1
        if calls["n"] > 80:  # 50Hz で約1.6秒後に終了
            raise KeyboardInterrupt
        return []

    fake = SimpleNamespace(
        axis_info={}, read_events=read_events, close=lambda: released.append("device")
    )

    class Transport:
        def __init__(self, *args):
            pass

        def send(self, packet):
            packets.append(packet)

        def poll_status(self):
            return []

        def close(self):
            released.append("transport")

    monkeypatch.setattr(pilot, "find_controller", lambda: fake)
    monkeypatch.setattr(pilot, "UdpTransport", Transport)
    monkeypatch.setattr(pilot.time, "monotonic", lambda: clock["t"])
    monkeypatch.setattr(
        pilot.time, "sleep", lambda s: clock.__setitem__("t", clock["t"] + 0.02)
    )
    monkeypatch.setattr(
        "sys.argv", ["sakura-pilot", "--host", "127.0.0.1", "--port", "50000"]
    )
    pilot.main()
    actions = [p.action for p in packets]
    first_arm = actions.index(pilot.Action.ARM)
    assert all(a == pilot.Action.DISARM for a in actions[:first_arm])
    assert first_arm >= 50  # 約1秒のリンク時間
    assert actions[first_arm : first_arm + 5] == [pilot.Action.ARM] * 5
    assert pilot.Action.DRIVE in actions
    assert actions[-1] == pilot.Action.DISARM
    assert released == ["transport", "device"]


def test_cli_no_arm_sends_only_disarm(monkeypatch):
    import sakura_avenue.main as pilot

    packets, clock, calls = [], {"t": 0.0}, {"n": 0}

    def read_events(timeout):
        calls["n"] += 1
        if calls["n"] > 100:
            raise KeyboardInterrupt
        return []

    fake = SimpleNamespace(axis_info={}, read_events=read_events, close=lambda: None)

    class Transport:
        def __init__(self, *args):
            pass

        def send(self, packet):
            packets.append(packet)

        def poll_status(self):
            return []

        def close(self):
            pass

    monkeypatch.setattr(pilot, "find_controller", lambda: fake)
    monkeypatch.setattr(pilot, "UdpTransport", Transport)
    monkeypatch.setattr(pilot.time, "monotonic", lambda: clock["t"])
    monkeypatch.setattr(
        pilot.time, "sleep", lambda s: clock.__setitem__("t", clock["t"] + 0.02)
    )
    monkeypatch.setattr(
        "sys.argv",
        ["sakura-pilot", "--host", "127.0.0.1", "--port", "50000", "--no-arm"],
    )
    pilot.main()
    assert {p.action for p in packets} == {pilot.Action.DISARM}
