/**
 * @file service_protocol.hpp
 * @brief ログ要求・PIゲイン設定用サービスパケット（magic "SC"）の符号化。
 * 走行用20バイトv1（magic "SA"）とは完全に別系統で、Safetyには渡さない。
 * 仕様の基準は protocol/specification.md。
 */
#pragma once
#include "protocol.hpp"
#include "speed_controller.hpp"
namespace sakura {
constexpr size_t service_header_size = 6; ///< 'S','C',ver,type,req_id(u16)
constexpr size_t service_max_size = 128;  ///< 受信バッファ上限。
enum class ServiceType : uint8_t {
  LogStart = 1,   ///< rate_hz(u8), lease_ms(u16)
  LogStop = 2,    ///< payloadなし
  ParamGet = 3,   ///< payloadなし
  ParamSet = 4,   ///< kp, ki, integral_limit, max_current (f32 x4)
  ParamSave = 5,  ///< payloadなし。現在のRAM値をNVSへ保存
  ParamReset = 6, ///< payloadなし。既定値へ戻しNVSを消去
  Ack = 0x81,     ///< status(u8) + 現在のゲインf32 x4
  LogSample = 0x82,
};
enum class ServiceStatus : uint8_t {
  Ok = 0,
  BadRequest = 1,
  NotDisarmed = 2,
  InvalidValue = 3,
  StorageError = 4,
  UnknownType = 5,
};
struct ServiceRequest {
  ServiceType type = ServiceType::LogStop;
  uint16_t req_id = 0;
  uint8_t rate_hz = 0;
  uint16_t lease_ms = 0;
  SpeedGains gains{};
};
/**
 * @brief ログ1件分の4モーターの値。整数は欠落検知・表示用に丸めた値。
 */
struct LogMotor {
  int16_t rpm = 0, current = 0;
  uint16_t angle = 0;
  uint8_t temperature = 0;
  int16_t target_rpm = 0, command = 0, integral = 0;
};
struct LogSample {
  uint32_t sequence = 0, time_ms = 0;
  uint8_t state = 0;      ///< State列挙値（0=Disarm,1=Armed,2=Stop,3=Fault）
  uint8_t fresh_mask = 0; ///< bit i = モーターiのフィードバックが期限内
  std::array<LogMotor, 4> motor{};
};
constexpr size_t ack_size = service_header_size + 1 + 16;
constexpr size_t sample_size = service_header_size + 4 + 4 + 1 + 1 + 4 * 13;
inline void put16(uint8_t *b, uint16_t v) {
  b[0] = v >> 8;
  b[1] = v & 255;
}
inline uint16_t get16(const uint8_t *b) { return uint16_t(b[0] << 8 | b[1]); }
inline void putf(uint8_t *b, float v) {
  uint32_t bits;
  std::memcpy(&bits, &v, 4);
  write32(b, bits);
}
inline float getf(const uint8_t *b) {
  uint32_t bits = read32(b);
  float v;
  std::memcpy(&v, &bits, 4);
  return v;
}
inline void put_header(uint8_t *b, ServiceType t, uint16_t req_id) {
  b[0] = 'S';
  b[1] = 'C';
  b[2] = 1;
  b[3] = static_cast<uint8_t>(t);
  put16(b + 4, req_id);
}
/**
 * @brief 要求をdecodeする。ゲイン値の範囲検証はここでは行わない。
 * @return 形式（magic・version・種別・長さ）が正しければtrue。
 */
inline bool decode_service(const uint8_t *b, size_t n, ServiceRequest &r) {
  if (n < service_header_size || b[0] != 'S' || b[1] != 'C' || b[2] != 1)
    return false;
  r.req_id = get16(b + 4);
  size_t payload = n - service_header_size;
  const uint8_t *p = b + service_header_size;
  switch (b[3]) {
  case 1:
    if (payload != 3)
      return false;
    r.type = ServiceType::LogStart;
    r.rate_hz = p[0];
    r.lease_ms = get16(p + 1);
    return true;
  case 2:
  case 3:
  case 5:
  case 6:
    if (payload != 0)
      return false;
    r.type = static_cast<ServiceType>(b[3]);
    return true;
  case 4:
    if (payload != 16)
      return false;
    r.type = ServiceType::ParamSet;
    r.gains = {getf(p), getf(p + 4), getf(p + 8), getf(p + 12)};
    return true;
  default:
    return false;
  }
}
inline size_t encode_ack(uint16_t req_id, ServiceStatus s, const SpeedGains &g,
                         uint8_t *out) {
  put_header(out, ServiceType::Ack, req_id);
  out[6] = static_cast<uint8_t>(s);
  putf(out + 7, g.kp);
  putf(out + 11, g.ki);
  putf(out + 15, g.integral_limit);
  putf(out + 19, g.max_current);
  return ack_size;
}
inline size_t encode_sample(const LogSample &s, uint8_t *out) {
  put_header(out, ServiceType::LogSample, 0);
  uint8_t *p = out + service_header_size;
  write32(p, s.sequence);
  write32(p + 4, s.time_ms);
  p[8] = s.state;
  p[9] = s.fresh_mask;
  p += 10;
  for (const auto &m : s.motor) {
    put16(p, static_cast<uint16_t>(m.rpm));
    put16(p + 2, static_cast<uint16_t>(m.current));
    put16(p + 4, m.angle);
    p[6] = m.temperature;
    put16(p + 7, static_cast<uint16_t>(m.target_rpm));
    put16(p + 9, static_cast<uint16_t>(m.command));
    put16(p + 11, static_cast<uint16_t>(m.integral));
    p += 13;
  }
  return sample_size;
}
} // namespace sakura
