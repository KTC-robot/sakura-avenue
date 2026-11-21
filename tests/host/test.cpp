#include "control/chassis.hpp"
#include "control/vehicle_config.hpp"
#include "device/c620.hpp"
#include "network/control_loop.hpp"
#include "network/receiver.hpp"
#include "protocol/protocol.hpp"
#include "vectors.hpp"
#include <cassert>
#include <cmath>
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
// 実機の車体設定(vehicle_config.hpp)の回帰テスト。
void vehicle_config_test() {
  using namespace sakura::vehicle;
  Omni omni(wheels()); // rank 3を満たし、例外にならない
  Limits lim = kLimits;
  // 正規化1.0の各軸が、モーター最大RPM以内に収まる(スケールで潰れない)。
  float peak = 0;
  for (DriveCommand c : {DriveCommand{lim.vx, 0, 0}, DriveCommand{0, lim.vy, 0},
                         DriveCommand{0, 0, lim.wz}})
    for (float v : omni.motor_rpm(c))
      peak = std::max(peak, std::abs(v));
  assert(peak <= lim.max_motor_rpm * 1.05f);
  // 全開で最高速の80%が出る。過速度の閾値は最高速以上、電流上限は変えない。
  assert(near(lim.max_motor_rpm, kMotorMaxRpm * 0.8f));
  assert(peak >= lim.max_motor_rpm * 0.95f);
  assert(kOverspeedRpm >= kMotorMaxRpm);
  // 符号の構造: 前進は前の2輪(ID3,ID1)が同符号、後の2輪(ID2,ID4)が逆符号。
  auto f = omni.motor_rpm({0.08f, 0, 0}); // index = ID-1
  assert(f[2] * f[0] > 0 && f[1] * f[3] > 0 && f[2] * f[1] < 0);
  assert(near(std::abs(f[0]), std::abs(f[1])));
  // 旋回は4輪が同符号で等しい大きさ(接線駆動)。
  auto y = omni.motor_rpm({0, 0, 0.32f});
  for (int i = 1; i < 4; i++)
    assert(y[i] * y[0] > 0 && near(std::abs(y[i]), std::abs(y[0])));
  // 左移動は対角の輪が同符号。
  auto l = omni.motor_rpm({0, 0.08f, 0});
  assert(l[0] * l[1] < 0 && l[0] * l[3] > 0 && l[2] * l[1] > 0);
  // 全IDがちょうど1度ずつ使われる。
  bool used[4] = {};
  for (auto &s : kSlots)
    used[s.id - 1] = true;
  assert(used[0] && used[1] && used[2] && used[3]);
}
// ハートビート(状態通知)の固定バイト列。protocol/specification.md と同じ値。
void status_test() {
  std::array<uint8_t, status_size> b{};
  encode_status(State::Armed,
                status_wifi | status_can | status_feedback | status_interlock,
                0x01020304U, b);
  const uint8_t armed[] = {'S', 'T', 1, 1, 0x0f, 0, 1, 2, 3, 4};
  for (size_t i = 0; i < b.size(); i++)
    assert(b[i] == armed[i]);
  encode_status(State::Fault, status_wifi, 0, b);
  const uint8_t fault[] = {'S', 'T', 1, 3, 0x01, 0, 0, 0, 0, 0};
  for (size_t i = 0; i < b.size(); i++)
    assert(b[i] == fault[i]);
}
int main() {
  status_test();
  vehicle_config_test();
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
  std::cout << "All host control checks passed\n";
}
