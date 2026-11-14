"""sakura-pilot用の対話コンソール。操縦中にログ表示・ゲイン変更を行う。

標準入力から1行ずつsakura-tuneと同じサブコマンド(monitor / param ...)を受け付ける。
操縦と同じソケット(送信元port)を共有するので、sakura-tuneとの同時起動は不要。
"""

import argparse
import shlex
import sys
import threading
from .service_client import ServiceClient, ServiceError
from .tune import add_commands, check_args, execute

HELP = (
    "commands: monitor [--rate N --count N --duration S --csv F] | "
    "param get | param set [--kp X --ki X --i-limit X --i-max X --save] | "
    "param save | param reset | help | quit\n"
    "gain changes are accepted only while DISARM; monitor stops with 'stop'"
)


class _Parser(argparse.ArgumentParser):
    """エラーでプロセスを終了させず例外にする。"""

    def error(self, message):
        raise ValueError(message)

    def exit(self, status=0, message=None):
        raise ValueError(message.strip() if message else "")


def build_console_parser() -> argparse.ArgumentParser:
    """consoleの1行を解析するparser。"""
    parser = _Parser(prog="", add_help=False)
    add_commands(parser)
    return parser


class Console(threading.Thread):
    """標準入力を読むデーモンスレッド。monitorは別スレッドで動かす。

    monitorとparamは同じソケットでACK/ログを受けるため同時に実行しない。
    """

    def __init__(self, client: ServiceClient, lines=None, out=None):
        super().__init__(daemon=True)
        self.client = client
        self.out = out or sys.stdout
        self._lines = lines if lines is not None else sys.stdin
        self._monitor: threading.Thread | None = None
        self._monitor_stop = threading.Event()

    def monitoring(self) -> bool:
        """monitorが実行中か。"""
        return self._monitor is not None and self._monitor.is_alive()

    def stop_monitor(self) -> None:
        """monitorを止めてLOG_STOPを送るまで待つ。"""
        self._monitor_stop.set()
        if self._monitor is not None:
            self._monitor.join(5)

    def close(self) -> None:
        """終了処理。monitorを止める。"""
        self.stop_monitor()

    def _run_monitor(self, args: argparse.Namespace) -> None:
        try:
            execute(self.client, args, self.out, self._monitor_stop)
        except (ServiceError, ValueError, OSError) as error:
            print(f"error: {error}", file=self.out)

    def handle_line(self, line: str) -> bool:
        """1行を処理する。quitならFalse。"""
        words = shlex.split(line)
        if not words:
            return True
        if words[0] in ("quit", "exit"):
            return False
        if words[0] == "help":
            print(HELP, file=self.out)
            return True
        if words[0] == "stop":
            self.stop_monitor()
            return True
        parser = build_console_parser()
        try:
            args = parser.parse_args(words)
            check_args(parser, args)
            if self.monitoring():
                raise ValueError("monitor running; type 'stop' first")
            if args.command == "monitor":
                self._monitor_stop = threading.Event()
                self._monitor = threading.Thread(
                    target=self._run_monitor, args=(args,), daemon=True
                )
                self._monitor.start()
            else:
                execute(self.client, args, self.out)
        except (ServiceError, ValueError) as error:
            print(f"error: {error}", file=self.out)
        return True

    def run(self) -> None:
        print(HELP, file=self.out)
        for raw in self._lines:
            try:
                if not self.handle_line(raw.strip()):
                    break
            except ValueError as error:  # shlexの引用符エラー
                print(f"error: {error}", file=self.out)
        self.stop_monitor()
