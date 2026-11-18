/**
 * @file udp_socket.hpp
 * @brief Wi-Fi設定から独立したノンブロッキングUDP受信を提供する。
 */
#pragma once
#include "network/receiver.hpp"
namespace sakura {
// UDPベンチアダプター。Wi-Fi設定は分離し、app_mainでは開始しない。
class UdpSocket {
  int fd_ = -1;

public:
  ~UdpSocket();
  UdpSocket() = default;
  UdpSocket(const UdpSocket &) = delete;
  UdpSocket &operator=(const UdpSocket &) = delete;
  /**
   * @brief 指定ローカルIPv4とポートへbindする。
   * @return 開始成功ならtrue。
   */
  bool begin(const char *local_ip, uint16_t port);
  /**
   * @brief 受信期限を監視し、最大8 datagramを処理する。
   * @param now 単調時計[ms]。
   * @param healthy 全体の健全性。
   * @return ソケット処理を継続できる場合true。
   */
  bool poll(Receiver &receiver, uint64_t now, bool healthy);
  /**
   * @brief 指定先へ1 datagramを送る(状態通知用)。
   * @return 送信できた場合true。失敗しても制御はFAULTにしない。
   */
  bool send_to(const char *ip, uint16_t port, const uint8_t *data, size_t size);
};
} // namespace sakura
