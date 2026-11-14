/**
 * @file service.hpp
 * @brief ログ配信とPIゲイン設定の要求処理。安全状態・制御周期には干渉しない。
 */
#pragma once
#include "c620.hpp"
#include "param_store.hpp"
#include "receiver.hpp"
#include "service_protocol.hpp"
namespace sakura {
/**
 * @brief サービス要求を処理する。
 * - ログは要求時のみ。LogStartのleaseが切れると送信を止める。
 * - ゲインの変更（Set/Reset）はSafetyがDisarmの時だけ受理する。
 * - 不正な要求はACKのstatusで返し、FAULTにはしない。
 * @note 暗号学的認証・リプレイ保護は無い。Set/Save/Resetは冪等なので再送は安全。
 */
class ServiceHandler {
  Receiver &receiver_;
  Chassis &chassis_;
  Feedback &feedback_;
  ParamStore &store_;
  bool logging_ = false;
  uint8_t rate_hz_ = 0;
  uint64_t lease_until_ = 0, next_due_ = 0;
  uint32_t sample_seq_ = 0;

public:
  static constexpr uint8_t max_rate_hz = 50;
  static constexpr uint16_t max_lease_ms = 5000;
  ServiceHandler(Receiver &r, Chassis &c, Feedback &f, ParamStore &s)
      : receiver_(r), chassis_(c), feedback_(f), store_(s) {}
  bool logging() const { return logging_; }
  /**
   * @brief 起動時に保存値を読む。無い・不正なら既定値のまま。
   * @return 保存値を適用したらtrue。
   */
  bool load_saved() {
    SpeedGains g;
    if (store_.load(g) && gains_valid(g))
      return chassis_.set_gains(g);
    return false;
  }
  /**
   * @brief 要求1件を処理してACKを作る。
   * @param out service_max_size以上の領域。
   * @return ACKのバイト数。形式不正でreq_idも得られない場合は0（無応答）。
   */
  size_t handle(const uint8_t *b, size_t n, uint64_t now, uint8_t *out) {
    ServiceRequest r;
    if (!decode_service(b, n, r)) {
      // 形式不正でもreq_idが読めれば通知する。ヘッダー自体が不正なら無視。
      if (n >= service_header_size && b[0] == 'S' && b[1] == 'C')
        return encode_ack(get16(b + 4),
                          (b[2] == 1 && b[3] >= 1 && b[3] <= 6)
                              ? ServiceStatus::BadRequest
                              : ServiceStatus::UnknownType,
                          chassis_.gains(), out);
      return 0;
    }
    ServiceStatus st = ServiceStatus::Ok;
    bool disarm = receiver_.safety.state == State::Disarm;
    switch (r.type) {
    case ServiceType::LogStart:
      if (r.rate_hz == 0 || r.rate_hz > max_rate_hz || r.lease_ms == 0 ||
          r.lease_ms > max_lease_ms) {
        st = ServiceStatus::InvalidValue;
        break;
      }
      if (!logging_)
        next_due_ = now; // 新規開始は直ちに1件送る。更新(lease延長)では乱さない。
      logging_ = true;
      rate_hz_ = r.rate_hz;
      lease_until_ = now + r.lease_ms;
      break;
    case ServiceType::LogStop:
      logging_ = false;
      break;
    case ServiceType::ParamGet:
      break;
    case ServiceType::ParamSet:
      if (!disarm)
        st = ServiceStatus::NotDisarmed;
      else if (!chassis_.set_gains(r.gains))
        st = ServiceStatus::InvalidValue;
      break;
    case ServiceType::ParamSave:
      if (!store_.save(chassis_.gains()))
        st = ServiceStatus::StorageError;
      break;
    case ServiceType::ParamReset:
      if (!disarm)
        st = ServiceStatus::NotDisarmed;
      else {
        chassis_.set_gains(SpeedGains{});
        if (!store_.erase())
          st = ServiceStatus::StorageError;
      }
      break;
    default:
      st = ServiceStatus::UnknownType;
    }
    return encode_ack(r.req_id, st, chassis_.gains(), out);
  }
  /**
   * @brief 送信期限が来ていればログ1件を作る。lease切れで停止する。
   * @param out service_max_size以上の領域。
   * @return バイト数。送信不要なら0。
   */
  size_t sample(uint64_t now, uint8_t *out) {
    if (!logging_)
      return 0;
    if (now > lease_until_) {
      logging_ = false;
      return 0;
    }
    if (now < next_due_)
      return 0;
    next_due_ = now + 1000 / rate_hz_;
    LogSample s;
    s.sequence = sample_seq_++;
    s.time_ms = static_cast<uint32_t>(now);
    s.state = static_cast<uint8_t>(receiver_.safety.state);
    auto targets = chassis_.targets();
    const auto &res = chassis_.results();
    auto i16 = [](float v) {
      return static_cast<int16_t>(std::clamp(std::lround(v), -32768L, 32767L));
    };
    for (int i = 0; i < 4; i++) {
      if (feedback_.fresh(i, now))
        s.fresh_mask |= 1 << i;
      s.motor[i] = {feedback_.rpm[i],        feedback_.current[i],
                    feedback_.angle[i],      feedback_.temperature[i],
                    i16(targets[i]),         res[i].currentCommand,
                    i16(res[i].integral)};
    }
    return encode_sample(s, out);
  }
};
} // namespace sakura
