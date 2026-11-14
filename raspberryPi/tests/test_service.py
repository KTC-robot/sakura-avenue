import io
import json
import socket
import struct
import threading
from argparse import Namespace
from pathlib import Path
import pytest
from sakura_avenue import service_client, tune
from sakura_avenue.network import UdpTransport
from sakura_avenue.service_client import ServiceClient, ServiceError
from sakura_avenue.service_protocol import (
    HEADER,
    Gains,
    LogSample,
    ServiceType,
    Status,
    decode_ack,
    decode_response,
    decode_sample,
    encode_log_start,
    encode_param_set,
    encode_simple,
)

VECTORS = {
    v["name"]: v
    for v in json.loads(
        (
            Path(__file__).parents[2] / "protocol" / "service_golden_vectors.json"
        ).read_text()
    )
}
GAINS = Gains(1.5, 0.25, 4000.0, 2000.0)


def hexb(name):
    return bytes.fromhex(VECTORS[name]["hex"])


def test_request_vectors():
    assert encode_log_start(1, 20, 1000) == hexb("log_start")
    assert encode_simple(ServiceType.LOG_STOP, 2) == hexb("log_stop")
    assert encode_simple(ServiceType.PARAM_GET, 3) == hexb("param_get")
    assert encode_param_set(4, GAINS) == hexb("param_set")
    assert encode_simple(ServiceType.PARAM_SAVE, 5) == hexb("param_save")
    assert encode_simple(ServiceType.PARAM_RESET, 6) == hexb("param_reset")


def test_response_vectors():
    ack = decode_ack(hexb("ack_ok"))
    assert (ack.req_id, ack.status, ack.gains) == (4, Status.OK, GAINS)
    s = decode_sample(hexb("log_sample"))
    want = VECTORS["log_sample"]
    assert (s.sequence, s.time_ms, s.state, s.fresh_mask) == (
        want["sequence"],
        want["time_ms"],
        want["state"],
        want["fresh_mask"],
    )
    assert [m.__dict__ for m in s.motors] == want["motors"]
    assert isinstance(decode_response(hexb("log_sample")), LogSample)
    assert decode_response(hexb("ack_ok")).req_id == 4


@pytest.mark.parametrize(
    "gains",
    [
        Gains(float("nan"), 0, 0, 0),
        Gains(-0.1, 0, 0, 0),
        Gains(10.1, 0, 0, 0),
        Gains(0, 5.1, 0, 0),
        Gains(0, 0, 8001, 0),
        Gains(0, 0, 0, 3001),
        Gains(0, 0, float("inf"), 0),
    ],
)
def test_gain_validation(gains):
    with pytest.raises(ValueError):
        gains.validate()
    with pytest.raises(ValueError):
        encode_param_set(1, gains)


def test_bad_inputs():
    with pytest.raises(ValueError):
        encode_log_start(1, 0, 1000)
    with pytest.raises(ValueError):
        encode_log_start(1, 51, 1000)
    with pytest.raises(ValueError):
        encode_log_start(1, 10, 5001)
    with pytest.raises(ValueError):
        encode_simple(ServiceType.PARAM_SET, 1)
    with pytest.raises(ValueError):
        decode_ack(hexb("ack_ok")[:-1])
    assert decode_response(b"garbage") is None
    assert decode_response(hexb("log_sample")[:-1]) is None


class FakeEsp(threading.Thread):
    """ESP32の最小模擬。要求を記録し、ACKとログを返す。"""

    def __init__(self, drop_first=0, status=Status.OK):
        super().__init__(daemon=True)
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.bind(("127.0.0.1", 0))
        self.sock.settimeout(0.05)
        self.address = self.sock.getsockname()
        self.gains = GAINS
        self.drop = drop_first
        self.status = status
        self.requests = []
        self.sending = None
        self.seq = 0
        self.skip_seq = None
        self.stop = threading.Event()

    def ack(self, req_id):
        return (
            HEADER.pack(b"SC", 1, ServiceType.ACK, req_id)
            + struct.pack("!B", self.status)
            + struct.pack("!4f", *self.gains.__dict__.values())
        )

    def run(self):
        while not self.stop.is_set():
            try:
                data, peer = self.sock.recvfrom(256)
            except TimeoutError:
                data = None
            if data:
                type_, req_id = data[3], int.from_bytes(data[4:6], "big")
                self.requests.append(type_)
                if self.drop > 0:
                    self.drop -= 1
                elif type_ == ServiceType.PARAM_SET and self.status == Status.OK:
                    self.gains = Gains(*struct.unpack("!4f", data[6:22]))
                    self.sock.sendto(self.ack(req_id), peer)
                else:
                    self.sock.sendto(self.ack(req_id), peer)
                if type_ == ServiceType.LOG_START:
                    self.sending = peer
                if type_ == ServiceType.LOG_STOP:
                    self.sending = None
            if self.sending:
                if self.seq == self.skip_seq:
                    self.seq += 1  # 1件欠落させる
                sample = bytearray(hexb("log_sample"))
                sample[HEADER.size : HEADER.size + 4] = self.seq.to_bytes(4, "big")
                self.seq += 1
                self.sock.sendto(bytes(sample), self.sending)

    def close(self):
        self.stop.set()
        self.join(1)
        self.sock.close()


@pytest.fixture
def esp():
    fake = FakeEsp()
    fake.start()
    yield fake
    fake.close()


@pytest.fixture
def client(esp):
    transport = UdpTransport(*esp.address)
    yield ServiceClient(transport)
    transport.close()


def test_get_set_save_reset(client, esp):
    assert client.get() == GAINS
    new = Gains(2.0, 0.5, 1000.0, 1500.0)
    assert client.set(new) == new
    assert client.save() == new
    client.reset()
    assert [ServiceType(t) for t in esp.requests] == [
        ServiceType.PARAM_GET,
        ServiceType.PARAM_SET,
        ServiceType.PARAM_SAVE,
        ServiceType.PARAM_RESET,
    ]


def test_retry_after_lost_ack(client, esp, monkeypatch):
    monkeypatch.setattr(service_client, "ACK_TIMEOUT_S", 0.1)
    esp.drop = 2
    assert client.get() == GAINS
    assert esp.requests.count(ServiceType.PARAM_GET) == 3


def test_timeout_raises(client, esp, monkeypatch):
    monkeypatch.setattr(service_client, "ACK_TIMEOUT_S", 0.05)
    esp.drop = 99
    with pytest.raises(ServiceError, match="timeout"):
        client.get()


def test_error_status_raises(client, esp):
    esp.status = Status.NOT_DISARMED
    with pytest.raises(ServiceError, match="NOT_DISARMED"):
        client.set(GAINS)


def test_ack_from_other_source_ignored(esp, monkeypatch):
    monkeypatch.setattr(service_client, "ACK_TIMEOUT_S", 0.05)
    esp.drop = 99
    transport = UdpTransport(*esp.address)
    intruder = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        client = ServiceClient(transport)
        port = transport.socket.getsockname()[1]
        intruder.sendto(esp.ack(client._req_id + 1), ("127.0.0.1", port))
        with pytest.raises(ServiceError):
            client.get()
    finally:
        intruder.close()
        transport.close()


def test_monitor_csv_and_stop(client, esp, tmp_path):
    esp.skip_seq = 3
    out = io.StringIO()
    csv_path = tmp_path / "log.csv"
    args = Namespace(rate=10, lease_ms=1000, csv=csv_path, count=6, duration=0)
    assert tune.run_monitor(client, args, out) == 0
    text = out.getvalue()
    assert "lost 1 sample(s)" in text
    assert "M0 rpm=   100/   120" in text and "M2*" in text  # mask 0b1011 → M2 stale
    rows = csv_path.read_text().splitlines()
    assert rows[0].startswith("timestamp,sequence,time_ms,state,fresh_mask,m0_rpm")
    assert len(rows) == 7
    assert ServiceType.LOG_STOP in esp.requests


def test_cli_param_set_merges_current(esp, capsys, monkeypatch):
    argv = [
        "--host",
        esp.address[0],
        "--port",
        str(esp.address[1]),
        "--source-port",
        "0",
        "param",
        "set",
        "--kp",
        "3",
        "--save",
    ]
    assert tune.main(argv) == 0
    out = capsys.readouterr().out
    assert "kp=3 ki=0.25 integral_limit=4000 max_current=2000" in out
    assert esp.gains == Gains(3.0, 0.25, 4000.0, 2000.0)
    assert ServiceType.PARAM_SAVE in esp.requests


def test_cli_rejects_before_sending(esp, capsys):
    base = [
        "--host",
        esp.address[0],
        "--port",
        str(esp.address[1]),
        "--source-port",
        "0",
    ]
    assert tune.main(base + ["param", "set", "--kp", "99"]) == 1
    assert tune.main(base + ["param", "set"]) == 1
    assert ServiceType.PARAM_SET not in esp.requests
    assert "error" in capsys.readouterr().err


def test_console_param_and_monitor(client, esp):
    from sakura_avenue.console import Console

    out = io.StringIO()
    console = Console(client, lines=[], out=out)
    assert console.handle_line("param get")
    assert console.handle_line("param set --kp 2 --ki 1")
    assert esp.gains == Gains(2.0, 1.0, 4000.0, 2000.0)
    # 不正入力はエラー表示のみで継続する(終了しない)
    assert console.handle_line("param set --kp 99")
    assert console.handle_line("bogus")
    assert console.handle_line("param set")
    text = out.getvalue()
    assert text.count("error:") == 3
    # monitorは別スレッド。実行中のparamは拒否し、stopで止める。
    assert console.handle_line("monitor --rate 10")
    assert console.monitoring()
    assert console.handle_line("param get")
    assert "monitor running" in out.getvalue()
    assert console.handle_line("stop")
    assert not console.monitoring()
    assert ServiceType.LOG_STOP in esp.requests
    assert console.handle_line("param get")  # stop後は再び使える
    assert not console.handle_line("quit")


def test_console_monitor_count_finishes(client, esp):
    from sakura_avenue.console import Console

    out = io.StringIO()
    console = Console(client, lines=[], out=out)
    console.handle_line("monitor --count 2")
    console._monitor.join(5)
    assert not console.monitoring()
    assert out.getvalue().count("#") == 2
