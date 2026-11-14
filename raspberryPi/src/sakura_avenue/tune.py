"""ESP32のフィードバックログ表示とPIゲイン変更のCLI(sakura-tune)。

sakura-pilotの--consoleからも同じサブコマンドを使える(送信元portを共有するため)。

ESP32は信頼済みの送信元IP/portからの要求にだけ応答するため、--source-portは
ESP32側に設定した送信元portと一致させる。sakura-pilotと同時には使えない。
"""

import argparse
import csv
import sys
import threading
import time
from datetime import datetime
from pathlib import Path
from .network import UdpTransport
from .service_client import ServiceClient, ServiceError
from .service_protocol import (
    MAX_LEASE_MS,
    MAX_RATE_HZ,
    Gains,
    LogSample,
)

STATE_NAMES = {0: "DISARM", 1: "ARMED", 2: "STOP", 3: "FAULT"}
CSV_FIELDS = ["timestamp", "sequence", "time_ms", "state", "fresh_mask"] + [
    f"m{i}_{name}"
    for i in range(4)
    for name in (
        "rpm",
        "current",
        "angle",
        "temperature",
        "target_rpm",
        "command",
        "integral",
    )
]


def format_gains(g: Gains) -> str:
    """ゲインを1行で表示する。"""
    return (
        f"kp={g.kp:g} ki={g.ki:g} "
        f"integral_limit={g.integral_limit:g} max_current={g.max_current:g}"
    )


def format_sample(s: LogSample) -> str:
    """ログ1件を1行で表示する。staleのモーターは*を付ける。"""
    parts = []
    for i, m in enumerate(s.motors):
        stale = "" if s.fresh_mask >> i & 1 else "*"
        parts.append(
            f"M{i}{stale} rpm={m.rpm:6d}/{m.target_rpm:6d} cur={m.current:6d}"
            f" cmd={m.command:5d} I={m.integral:6d} {m.temperature:3d}C"
        )
    state = STATE_NAMES.get(s.state, str(s.state))
    return f"#{s.sequence} t={s.time_ms}ms {state:6s} | " + " | ".join(parts)


def sample_row(s: LogSample) -> list:
    """CSV1行分の値をCSV_FIELDSの順で返す。"""
    row = [datetime.now().isoformat(timespec="milliseconds")]
    row += [s.sequence, s.time_ms, STATE_NAMES.get(s.state, s.state), s.fresh_mask]
    for m in s.motors:
        row += [
            m.rpm,
            m.current,
            m.angle,
            m.temperature,
            m.target_rpm,
            m.command,
            m.integral,
        ]
    return row


def run_monitor(
    client: ServiceClient,
    args: argparse.Namespace,
    out=None,
    stop: threading.Event | None = None,
) -> int:
    """ログを表示し、--csv指定時は保存する。欠落(連番の飛び)も表示する。"""
    out = out or sys.stdout
    writer = None
    handle = None
    if args.csv:
        handle = open(args.csv, "w", newline="", encoding="utf-8")  # noqa: SIM115
        writer = csv.writer(handle)
        writer.writerow(CSV_FIELDS)
    last_seq = None
    count = 0
    end = time.monotonic() + args.duration if args.duration else None
    try:
        for s in client.samples(args.rate, args.lease_ms, stop):
            if last_seq is not None and s.sequence != ((last_seq + 1) & 0xFFFFFFFF):
                print(
                    f"-- lost {(s.sequence - last_seq - 1) & 0xFFFFFFFF} sample(s)",
                    file=out,
                )
            last_seq = s.sequence
            print(format_sample(s), file=out)
            if writer:
                writer.writerow(sample_row(s))
            count += 1
            if (args.count and count >= args.count) or (
                end is not None and time.monotonic() >= end
            ):
                break
    except KeyboardInterrupt:
        pass
    finally:
        if handle:
            handle.close()
    return 0


def run_param(client: ServiceClient, args: argparse.Namespace, out=None) -> int:
    """param get/set/save/resetを実行する。"""
    out = out or sys.stdout
    if args.action == "get":
        print(format_gains(client.get()), file=out)
    elif args.action == "set":
        given = {
            "kp": args.kp,
            "ki": args.ki,
            "integral_limit": args.i_limit,
            "max_current": args.i_max,
        }
        changes = {k: v for k, v in given.items() if v is not None}
        if not changes:
            raise ValueError("specify at least one of --kp --ki --i-limit --i-max")
        current = client.get()
        new = Gains(**{**current.__dict__, **changes})
        new.validate()
        print(f"before: {format_gains(current)}", file=out)
        print(f"after : {format_gains(client.set(new))}", file=out)
        if args.save:
            client.save()
            print("saved to NVS", file=out)
    elif args.action == "save":
        print(f"saved: {format_gains(client.save())}", file=out)
    elif args.action == "reset":
        print(f"reset to defaults: {format_gains(client.reset())}", file=out)
    return 0


def add_commands(parser: argparse.ArgumentParser) -> None:
    """monitor / param サブコマンドを追加する。sakura-tuneとpilotのconsoleで共用。"""
    sub = parser.add_subparsers(dest="command", required=True)
    mon = sub.add_parser("monitor", help="フィードバックログを表示する")
    mon.add_argument("--rate", type=int, default=10, help=f"1..{MAX_RATE_HZ} Hz")
    mon.add_argument("--lease-ms", type=int, default=1000, help=f"1..{MAX_LEASE_MS}")
    mon.add_argument("--csv", type=Path, help="CSV保存先")
    mon.add_argument("--count", type=int, default=0, help="N件で終了(0=無制限)")
    mon.add_argument("--duration", type=float, default=0, help="秒で終了(0=無制限)")
    par = sub.add_parser("param", help="PIゲインを取得・変更する")
    ps = par.add_subparsers(dest="action", required=True)
    ps.add_parser("get")
    st = ps.add_parser("set", help="DISARM時のみ。未指定の項目は現在値のまま")
    st.add_argument("--kp", type=float)
    st.add_argument("--ki", type=float)
    st.add_argument("--i-limit", type=float, help="積分上限")
    st.add_argument("--i-max", type=float, help="電流指令の上限")
    st.add_argument("--save", action="store_true", help="適用後にNVSへ保存")
    ps.add_parser("save", help="現在のゲインをNVSへ保存")
    ps.add_parser("reset", help="既定値へ戻しNVS保存値を消去(DISARM時のみ)")


def check_args(parser: argparse.ArgumentParser, args: argparse.Namespace) -> None:
    """monitorの範囲を検証する。不正ならparser.error。"""
    if args.command == "monitor":
        if not 1 <= args.rate <= MAX_RATE_HZ:
            parser.error(f"rate must be 1..{MAX_RATE_HZ}")
        if not 1 <= args.lease_ms <= MAX_LEASE_MS:
            parser.error(f"lease-ms must be 1..{MAX_LEASE_MS}")


def execute(
    client: ServiceClient,
    args: argparse.Namespace,
    out=None,
    stop: threading.Event | None = None,
) -> int:
    """解析済みサブコマンドを実行する。"""
    if args.command == "monitor":
        return run_monitor(client, args, out, stop)
    return run_param(client, args, out)


def build_parser() -> argparse.ArgumentParser:
    """sakura-tuneのCLI引数を定義する。"""
    parser = argparse.ArgumentParser(prog="sakura-tune")
    parser.add_argument("--host", required=True, help="ESP32 IPv4")
    parser.add_argument("--port", type=int, required=True, help="ESP32 UDP port")
    parser.add_argument(
        "--source-port",
        type=int,
        required=True,
        help="ESP32が信頼する送信元port",
    )
    add_commands(parser)
    return parser


def main(argv: list[str] | None = None) -> int:
    """sakura-tuneのエントリーポイント。失敗時は終了コード1。"""
    parser = build_parser()
    args = parser.parse_args(argv)
    check_args(parser, args)
    transport = UdpTransport(args.host, args.port, args.source_port)
    try:
        client = ServiceClient(transport)
        return execute(client, args)
    except (ServiceError, ValueError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1
    finally:
        transport.close()


if __name__ == "__main__":
    raise SystemExit(main())
