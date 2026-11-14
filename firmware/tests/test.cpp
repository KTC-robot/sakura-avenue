#include "c620.hpp"
#include "control.hpp"
#include "control_loop.hpp"
#include "protocol.hpp"
#include "receiver.hpp"
#include "service.hpp"
#include "vectors.hpp"
#include <cassert>
#include <map>
#include <string>
#include <vector>
#include "service_vectors.hpp"
#include <iostream>
#include <limits>
using namespace sakura;
bool near(float a, float b) { return std::abs(a - b) < 0.02f; }
// Synthetic geometry ONLY. Four drive directions 0, pi/2, pi, -pi/2.
std::array<WheelConfig, 4> fixture() {
  return {{{0, -1, 0, .1, 2, 1, 2},
           {1, 0, 1.57079632679f, .1, 2, 1, 0},
           {0, 1, 3.14159265359f, .1, 2, -1, 3},
           {-1, 0, -1.57079632679f, .1, 2, 1, 1}}};
}
int main() {
  auto wheels = fixture();
  Omni omni(wheels);
  auto fwd = omni.motor_rpm({1, 0, 0});
  float k = 60 / (2 * 3.14159265359f) * 20;
  assert(near(fwd[2], k) && near(fwd[3], k) && near(fwd[0], 0) &&
         near(fwd[1], 0));
  auto left = omni.motor_rpm({0, 1, 0});
  assert(near(left[0], k) && near(left[1], -k));
  auto yaw = omni.motor_rpm({0, 0, 1});
  assert(near(yaw[0], k) && near(yaw[1], k) && near(yaw[2], k) &&
         near(yaw[3], -k));
  for (int bad = 0; bad < 6; bad++) {
    auto w = fixture();
    if (bad == 0)
      w[0].radius_m = 0;
    if (bad == 1)
      w[0].gear_ratio = -1;
    if (bad == 2)
      w[0].motor_index = w[1].motor_index;
    if (bad == 3)
      for (auto &x : w)
        x.drive_angle_rad = 0;
    if (bad == 4)
      w[0].motor_sign = 0;
    if (bad == 5)
      w[0].x_m = std::numeric_limits<float>::quiet_NaN();
    bool threw = false;
    try {
      Omni invalid(w);
    } catch (const std::invalid_argument &) {
      threw = true;
    }
    assert(threw);
  }
  SpeedController pi;
  assert(pi.update(100, 0, .1f).currentCommand == 102);
  auto sat = pi.update(10000, 0, 1);
  assert(sat.currentCommand == 3000 && near(sat.integral, 10));
  pi.reset();
  assert(pi.update(-10000, 0, 1).currentCommand == -3000);
  assert(pi.update(0, 0, 1).integral == 0);
  assert(pi.update(NAN, 0, .1).currentCommand == 0);
  Chassis chassis(fixture(), {1, 1, 1, 100, 50, .08});
  auto currents = chassis.update({1, 0, 0}, {}, .1, true);
  assert(currents[2] > 0);
  assert(near(chassis.targets()[2], 5));
  currents = chassis.update({1, 0, 0}, {}, .1, false);
  for (auto c : currents)
    assert(c == 0);
  for (float t : chassis.targets())
    assert(t == 0);
  auto frame = current_frame({30000, -30000, 1, -1});
  assert(frame.id == 0x200 && frame.size == 8);
  assert(frame.data[0] == 0x0b && frame.data[1] == 0xb8 &&
         frame.data[2] == 0xf4 && frame.data[3] == 0x48);
  Feedback fb;
  assert(!fb.fresh(0));
  for (int i = 0; i < 4; i++) {
    CanFrame f;
    f.id = 0x201 + i;
    f.size = 8;
    f.data[2] = 0xff;
    f.data[3] = 0x9c;
    assert(fb.receive(f, 0));
    assert(fb.rpm[i] == -100);
  }
  assert(fb.fresh(100) && !fb.fresh(101));
  frame.extended = true;
  assert(!fb.receive(frame, 0));
  size_t vector_index = 0;
  for (auto bytes : golden) {
    Packet p;
    assert(decode(bytes.data(), bytes.size(), p));
    const auto &want = expected[vector_index++];
    assert(p.sequence == want.sequence && p.action == want.action &&
           p.command.vx == want.command.vx && p.command.vy == want.command.vy &&
           p.command.wz == want.command.wz);
    std::array<uint8_t, 20> out{};
    assert(encode(want, out) && bytes == out);
    assert(!decode(bytes.data(), 19, p));
    bytes[2] = 2;
    assert(!decode(bytes.data(), 20, p));
  }
  Safety safety;
  assert(!safety.accept({0, Action::Arm, {}}, 0, true));
  assert(!safety.accept({1, Action::Disarm, {}}, 1, true));
  assert(safety.accept({2, Action::Arm, {}}, 2, true));
  safety.tick(183, true);
  assert(safety.state == State::Fault);
  assert(!safety.accept({3, Action::Arm, {}}, 184, true));
  assert(!safety.accept({4, Action::Disarm, {}}, 185, true));
  assert(safety.accept({5, Action::Arm, {}}, 186, true));
  assert(!safety.accept({5, Action::Drive, {1, 0, 0}}, 187, true));
  assert(safety.state == State::Fault);
  safety.accept({6, Action::Disarm, {}}, 188, true);
  assert(safety.accept({7, Action::Arm, {}}, 189, true));
  safety.accept({8, Action::Stop, {}}, 190, true);
  assert(safety.state == State::Stop);
  assert(!safety.accept({9, Action::Arm, {}}, 191, true));
  safety.tick(192, false);
  assert(safety.state == State::Fault);
  Safety wrap;
  wrap.accept({0xffffffffU, Action::Disarm, {}}, 0, true);
  assert(wrap.accept({0, Action::Arm, {}}, 1, true));
  Receiver rx("127.0.0.1", 1234);
  assert(!rx.receive("127.0.0.2", 1234, golden[0].data(), 20, 0, true));
  rx.receive("127.0.0.1", 1234, golden[0].data(), 20, 0, true);
  assert(rx.receive("127.0.0.1", 1234, golden[1].data(), 20, 1, true));
  rx.receive("127.0.0.1", 1234, golden[2].data(), 20, 2, true);
  assert(rx.demand.vx == 1);
  rx.tick(183, true);
  assert(neutral(rx.demand));

  Receiver integrated("127.0.0.1", 1234);
  Feedback feedback;
  for (int i = 0; i < 4; i++) {
    CanFrame f;
    f.id = 0x201 + i;
    f.size = 8;
    feedback.receive(f, 0);
  }
  Chassis body(fixture(), {1, 1, 1, 100, 10000, .08});
  ControlLoop loop(integrated, body, feedback);
  auto deliver = [&](Packet p, uint64_t now) {
    std::array<uint8_t, 20> b{};
    assert(encode(p, b));
    return integrated.receive("127.0.0.1", 1234, b.data(), 20, now, true);
  };
  deliver({0, Action::Disarm, {}}, 0);
  assert(deliver({1, Action::Arm, {}}, 1));
  deliver({2, Action::Drive, {1, 0, 0}}, 2);
  auto moving = loop.step(3, .005, true, true, true);
  assert(moving.data[5] != 0);
  assert(near(body.targets()[2], 50));
  // Feedback loss must immediately bypass slew and reset integral/targets.
  auto stopped = loop.step(101, .005, true, true, true);
  for (auto b : stopped.data)
    assert(b == 0);
  for (float t : body.targets())
    assert(t == 0);
  assert(integrated.safety.state == State::Fault);
  assert(!deliver({3, Action::Arm, {}}, 102));
  loop.transport_fault();
  assert(neutral(integrated.demand));
  Chassis scaled(fixture(), {1, 1, 1, 100, 100000, .08});
  scaled.update({1, 1, 0}, {}, 1, true);
  for (float t : scaled.targets())
    assert(std::abs(t) <= 100.001f);
  Packet malformed;
  auto bad = golden[2];
  bad[8] = 0x7f;
  bad[9] = 0xc0;
  bad[10] = 0;
  bad[11] = 0;
  assert(!decode(bad.data(), 20, malformed));

  // ---- サービス: ゴールデンベクタ ----
  {
    auto dec = [&](const char *name, ServiceRequest &r) {
      const auto &v = service_golden.at(name);
      return decode_service(v.data(), v.size(), r);
    };
    ServiceRequest r;
    assert(dec("log_start", r) && r.type == ServiceType::LogStart &&
           r.req_id == 1 && r.rate_hz == 20 && r.lease_ms == 1000);
    assert(dec("log_stop", r) && r.type == ServiceType::LogStop && r.req_id == 2);
    assert(dec("param_get", r) && r.type == ServiceType::ParamGet);
    assert(dec("param_set", r) && r.type == ServiceType::ParamSet &&
           r.req_id == 4 && r.gains.kp == 1.5f && r.gains.ki == 0.25f &&
           r.gains.integral_limit == 4000 && r.gains.max_current == 2000);
    assert(dec("param_save", r) && r.type == ServiceType::ParamSave);
    assert(dec("param_reset", r) && r.type == ServiceType::ParamReset);
    // 長さ違い・magic/version違いは拒否
    auto bad = service_golden.at("param_set");
    assert(!decode_service(bad.data(), bad.size() - 1, r));
    bad[2] = 2;
    assert(!decode_service(bad.data(), bad.size(), r));
    uint8_t out[service_max_size];
    SpeedGains g{1.5f, 0.25f, 4000, 2000};
    size_t n = encode_ack(4, ServiceStatus::Ok, g, out);
    const auto &ack = service_golden.at("ack_ok");
    assert(n == ack.size() && std::equal(ack.begin(), ack.end(), out));
    LogSample s;
    s.sequence = 7;
    s.time_ms = 1234;
    s.state = 1;
    s.fresh_mask = 11;
    s.motor = {{{100, -10, 8191, 30, 120, -300, 50},
                {-200, 20, 0, 31, -220, 400, -60},
                {0, 0, 1, 0, 0, 0, 0},
                {32767, -32768, 65535, 255, 32767, 3000, -32768}}};
    n = encode_sample(s, out);
    const auto &smp = service_golden.at("log_sample");
    assert(n == smp.size() && std::equal(smp.begin(), smp.end(), out));
  }
  // ---- ゲイン検証 ----
  {
    SpeedGains g;
    assert(gains_valid(g));
    for (int k = 0; k < 8; k++) {
      SpeedGains b;
      if (k == 0) b.kp = NAN;
      if (k == 1) b.ki = -0.1f;
      if (k == 2) b.kp = KP_CEILING + 0.1f;
      if (k == 3) b.ki = KI_CEILING + 0.1f;
      if (k == 4) b.max_current = 3001;
      if (k == 5) b.integral_limit = INFINITY;
      if (k == 6) b.integral_limit = INTEGRAL_LIMIT_CEILING + 1;
      if (k == 7) b.max_current = -1;
      assert(!gains_valid(b));
    }
    // ゲイン変更は挙動へ反映され、積分と目標をリセットする。
    Chassis c(fixture(), {1, 1, 1, 100, 50, .08});
    c.update({1, 0, 0}, {}, .1, true);
    assert(c.results()[2].integral != 0);
    SpeedGains half{0.5f, 0.0f, 8000, 3000};
    assert(c.set_gains(half) && c.gains() == half);
    assert(c.results()[2].integral == 0 && c.targets()[2] == 0);
    auto cur = c.update({1, 0, 0}, {}, .1, true); // error=5 → 0.5*5=2.5→3
    assert(cur[2] == 3 || cur[2] == 2);
    SpeedGains bad{NAN, 0, 0, 0};
    assert(!c.set_gains(bad) && c.gains() == half);
    // 電流上限は実際の指令を制限する。
    assert(c.set_gains({1.0f, 0.2f, 8000, 100}));
    Chassis c2(fixture(), {1, 1, 1, 1e6f, 1e9f, .08});
    c2.set_gains({1.0f, 0.2f, 8000, 100});
    assert(c2.update({1, 0, 0}, {}, 1, true)[2] == 100);
  }
  // ---- ServiceHandler ----
  {
    struct MockStore : ParamStore {
      SpeedGains saved{};
      bool has = false, fail = false;
      bool load(SpeedGains &g) override { if (!has) return false; g = saved; return true; }
      bool save(const SpeedGains &g) override { if (fail) return false; saved = g; has = true; return true; }
      bool erase() override { has = false; return !fail; }
    } store;
    Receiver rx2("127.0.0.1", 1234);
    Chassis ch(fixture(), {1, 1, 1, 100, 10000, .08});
    Feedback fb2;
    ServiceHandler svc(rx2, ch, fb2, store);
    uint8_t out[service_max_size];
    auto call = [&](const char *name, uint64_t now) {
      const auto &v = service_golden.at(name);
      size_t n = svc.handle(v.data(), v.size(), now, out);
      assert(n == ack_size);
      return static_cast<ServiceStatus>(out[6]);
    };
    // Disarm: SET適用
    assert(call("param_set", 0) == ServiceStatus::Ok);
    assert(ch.gains() == SpeedGains({1.5f, 0.25f, 4000, 2000}));
    assert(getf(out + 7) == 1.5f && get16(out + 4) == 4);
    // SAVE → 保存、ブート時のload_savedで復元
    assert(call("param_save", 1) == ServiceStatus::Ok && store.has);
    ch.set_gains(SpeedGains{});
    assert(svc.load_saved() && ch.gains().kp == 1.5f);
    // 保存値が不正なら既定値のまま
    ch.set_gains(SpeedGains{});
    store.saved.kp = NAN;
    assert(!svc.load_saved() && ch.gains() == SpeedGains{});
    store.saved.kp = 1.5f;
    // RESET: 既定値へ戻し保存を消去
    assert(call("param_set", 2) == ServiceStatus::Ok);
    assert(call("param_reset", 3) == ServiceStatus::Ok && !store.has &&
           ch.gains() == SpeedGains{});
    // 保存失敗はStorageError
    store.fail = true;
    assert(call("param_save", 4) == ServiceStatus::StorageError);
    store.fail = false;
    // Disarm以外ではSET/RESETを拒否し、値は変えない
    assert(call("param_set", 5) == ServiceStatus::Ok);
    rx2.safety.accept({0, Action::Disarm, {}}, 5, true);
    rx2.safety.accept({1, Action::Arm, {}}, 6, true);
    assert(rx2.safety.state == State::Armed);
    SpeedGains before = ch.gains();
    ch.set_gains(SpeedGains{}); // 後続の確認用に既定へ
    before = ch.gains();
    assert(call("param_set", 7) == ServiceStatus::NotDisarmed);
    assert(call("param_reset", 7) == ServiceStatus::NotDisarmed);
    assert(ch.gains() == before);
    // GET/LOGはArmed中も可。サービス要求はFAULTにしない。
    assert(call("param_get", 8) == ServiceStatus::Ok);
    assert(rx2.safety.state == State::Armed);
    // 不正なSET値はInvalidValue。不正パケットはFAULTにならない。
    {
      auto v = service_golden.at("param_set");
      v[6] = 0x7f; v[7] = 0xc0; v[8] = 0; v[9] = 0; // kp = NaN
      rx2.safety.fault();
      rx2.safety.accept({2, Action::Disarm, {}}, 9, true);
      assert(rx2.safety.state == State::Disarm);
      size_t n = svc.handle(v.data(), v.size(), 9, out);
      assert(n == ack_size && out[6] == uint8_t(ServiceStatus::InvalidValue));
      assert(ch.gains() == SpeedGains{});
      uint8_t junk[3] = {'S', 'C', 1};
      assert(svc.handle(junk, 3, 9, out) == 0);
      uint8_t unk[6] = {'S', 'C', 1, 99, 0, 9};
      assert(svc.handle(unk, 6, 9, out) == ack_size &&
             out[6] == uint8_t(ServiceStatus::UnknownType));
      uint8_t shortp[7] = {'S', 'C', 1, 4, 0, 9, 0};
      assert(svc.handle(shortp, 7, 9, out) == ack_size &&
             out[6] == uint8_t(ServiceStatus::BadRequest));
      assert(rx2.safety.state == State::Disarm);
    }
    // ログ: 要求時のみ。開始前は無送信。
    uint8_t buf[service_max_size];
    assert(!svc.logging() && svc.sample(100, buf) == 0);
    assert(call("log_start", 100) == ServiceStatus::Ok && svc.logging()); // 20Hz, lease1000
    assert(svc.sample(100, buf) == sample_size);
    assert(get16(buf + 4) == 0 && read32(buf + 6) == 0 && read32(buf + 10) == 100);
    assert(svc.sample(120, buf) == 0);       // 50ms未満は送らない
    assert(svc.sample(150, buf) == sample_size && read32(buf + 6) == 1);
    assert(svc.sample(1101, buf) == 0 && !svc.logging()); // lease切れで停止
    assert(call("log_start", 2000) == ServiceStatus::Ok);
    assert(call("log_stop", 2001) == ServiceStatus::Ok && !svc.logging());
    assert(svc.sample(2100, buf) == 0);
    // rate/leaseの範囲外は拒否
    {
      auto v = service_golden.at("log_start");
      v[6] = 0;
      assert(svc.handle(v.data(), v.size(), 3000, out) == ack_size &&
             out[6] == uint8_t(ServiceStatus::InvalidValue) && !svc.logging());
      v[6] = ServiceHandler::max_rate_hz + 1;
      svc.handle(v.data(), v.size(), 3000, out);
      assert(out[6] == uint8_t(ServiceStatus::InvalidValue));
    }
    // 受信フィードバックがログに載る（fresh_maskと値）
    for (int i = 0; i < 4; i++) {
      if (i == 2) continue;
      CanFrame f;
      f.id = 0x201 + i;
      f.size = 8;
      f.data[2] = 0xff; f.data[3] = 0x9c; // -100rpm
      fb2.receive(f, 4000);
    }
    assert(call("log_start", 4000) == ServiceStatus::Ok);
    assert(svc.sample(4000, buf) == sample_size);
    assert(buf[15] == 0b1011 && buf[14] == uint8_t(State::Disarm));
    assert(int16_t(get16(buf + 16)) == -100);
  }
  std::cout << "All host control checks passed\n";
}
