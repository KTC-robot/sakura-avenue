"""サービスパケットの要求・ACK待ち・ログ受信。"""

import logging
import random
import threading
import time
from collections.abc import Iterator
from .network import UdpTransport
from .service_protocol import (
    Ack,
    Gains,
    LogSample,
    ServiceType,
    Status,
    decode_response,
    encode_log_start,
    encode_param_set,
    encode_simple,
)

log = logging.getLogger(__name__)
ACK_TIMEOUT_S = 0.5
ACK_RETRIES = 3


class ServiceError(Exception):
    """ESP32がエラーstatusを返した、または応答が無かった場合。"""


class ServiceClient:
    """UdpTransport上でESP32のサービスを呼ぶ。1要求ずつ同期的に処理する。"""

    def __init__(self, transport: UdpTransport):
        """req_idは再起動後の衝突を避けるため乱数から始める。"""
        self.transport = transport
        self._req_id = random.randrange(0x10000)

    def _next_id(self) -> int:
        self._req_id = (self._req_id + 1) & 0xFFFF
        return self._req_id

    def _request(self, build) -> Ack:
        """要求を送り、同じreq_idのACKを待つ。時間切れは同じ要求を再送する。

        Raises:
            ServiceError: 再送後も応答が無い場合、またはstatusがOKでない場合。
        """
        req_id = self._next_id()
        data = build(req_id)
        for _ in range(ACK_RETRIES):
            self.transport.send_raw(data)
            deadline = time.monotonic() + ACK_TIMEOUT_S
            while (remaining := deadline - time.monotonic()) > 0:
                raw = self.transport.recv(remaining)
                reply = decode_response(raw) if raw else None
                if isinstance(reply, Ack) and reply.req_id == req_id:
                    if reply.status != Status.OK:
                        raise ServiceError(reply.status.name)
                    return reply
        raise ServiceError("no response (timeout)")

    def get(self) -> Gains:
        """現在のゲインを取得する。"""
        return self._request(lambda r: encode_simple(ServiceType.PARAM_GET, r)).gains

    def set(self, gains: Gains) -> Gains:
        """ゲインを設定する(DISARM時のみ)。適用後の値を返す。"""
        return self._request(lambda r: encode_param_set(r, gains)).gains

    def save(self) -> Gains:
        """現在のゲインをNVSへ保存する。"""
        return self._request(lambda r: encode_simple(ServiceType.PARAM_SAVE, r)).gains

    def reset(self) -> Gains:
        """既定値へ戻し保存値を消去する(DISARM時のみ)。"""
        return self._request(lambda r: encode_simple(ServiceType.PARAM_RESET, r)).gains

    def stop_log(self) -> None:
        """ログ停止を要求する。応答が無くてもlease切れで止まる。"""
        try:
            self._request(lambda r: encode_simple(ServiceType.LOG_STOP, r))
        except ServiceError:
            log.warning("log stop not acknowledged; ESP32 stops at lease expiry")

    def samples(
        self, rate_hz: int, lease_ms: int = 1000, stop: threading.Event | None = None
    ) -> Iterator[LogSample]:
        """ログを開始して受信したLogSampleを順に返す。

        leaseの半分ごとに開始要求を再送して延長する。ジェネレーターを閉じると
        (または例外時・stopが立った時に)LOG_STOPを送る。
        """
        self._request(lambda r: encode_log_start(r, rate_hz, lease_ms))
        renew = time.monotonic() + lease_ms / 2000
        try:
            while not (stop and stop.is_set()):
                if time.monotonic() >= renew:
                    # 応答は読み捨てる。lease延長のため再送のみ行う。
                    self.transport.send_raw(
                        encode_log_start(self._next_id(), rate_hz, lease_ms)
                    )
                    renew = time.monotonic() + lease_ms / 2000
                raw = self.transport.recv(0.2)
                reply = decode_response(raw) if raw else None
                if isinstance(reply, LogSample):
                    yield reply
        finally:
            self.stop_log()
