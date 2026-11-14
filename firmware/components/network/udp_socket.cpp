#include "udp_socket.hpp"
#include "lwip/sockets.h"
#include <cerrno>
namespace sakura {
UdpSocket::~UdpSocket() {
  if (fd_ >= 0)
    close(fd_);
}
bool UdpSocket::begin(const char *ip, uint16_t port) {
  if (fd_ >= 0 || !ip || port == 0)
    return false;
  sockaddr_in local{};
  local.sin_family = AF_INET;
  local.sin_port = htons(port);
  if (inet_pton(AF_INET, ip, &local.sin_addr) != 1 ||
      local.sin_addr.s_addr == INADDR_ANY)
    return false;
  int fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (fd < 0)
    return false;
  if (bind(fd, reinterpret_cast<sockaddr *>(&local), sizeof(local)) != 0 ||
      fcntl(fd, F_SETFL, O_NONBLOCK) != 0) {
    close(fd);
    return false;
  }
  fd_ = fd;
  return true;
}
bool UdpSocket::poll(Receiver &receiver, uint64_t now, bool healthy,
                     ServiceHandler *service) {
  receiver.tick(now, healthy && fd_ >= 0);
  if (fd_ < 0)
    return false;
  // Bounded work prevents an input flood from starving the safety/control tick.
  for (int i = 0; i < 8; i++) {
    // 走行v1(20B)の過大検出とサービス(最大128B)を兼ねたバッファ。
    uint8_t bytes[service_max_size + 1];
    sockaddr_in source{};
    socklen_t length = sizeof(source);
    int n = recvfrom(fd_, bytes, sizeof(bytes), 0,
                     reinterpret_cast<sockaddr *>(&source), &length);
    if (n < 0) {
      if (errno == EAGAIN || errno == EWOULDBLOCK)
        return true;
      receiver.safety.fault();
      receiver.demand = {};
      return false;
    }
    char ip[INET_ADDRSTRLEN];
    if (!inet_ntop(AF_INET, &source.sin_addr, ip, sizeof(ip)))
      continue;
    // サービスパケット("SC")は走行パケットと別経路。Safetyには渡さず、
    // 信頼済みPeer以外は黙って無視する。
    if (n >= 2 && bytes[0] == 'S' && bytes[1] == 'C') {
      uint8_t reply[service_max_size];
      if (service && receiver.is_peer(ip, ntohs(source.sin_port))) {
        size_t m = service->handle(bytes, static_cast<size_t>(n), now, reply);
        if (m)
          sendto(fd_, reply, m, 0, reinterpret_cast<sockaddr *>(&source),
                 length);
      }
      continue;
    }
    receiver.receive(ip, ntohs(source.sin_port), bytes, static_cast<size_t>(n),
                     now, healthy);
  }
  return true;
}
void UdpSocket::send_log(const Receiver &receiver, ServiceHandler &service,
                         uint64_t now) {
  if (fd_ < 0)
    return;
  uint8_t out[service_max_size];
  size_t n = service.sample(now, out);
  if (!n)
    return;
  sockaddr_in peer{};
  peer.sin_family = AF_INET;
  peer.sin_port = htons(receiver.peer_port());
  if (inet_pton(AF_INET, receiver.peer_ip().c_str(), &peer.sin_addr) != 1)
    return;
  sendto(fd_, out, n, 0, reinterpret_cast<sockaddr *>(&peer), sizeof(peer));
}
} // namespace sakura
