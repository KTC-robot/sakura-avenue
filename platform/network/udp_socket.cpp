#include "network/udp_socket.hpp"
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
bool UdpSocket::send_to(const char *ip, uint16_t port, const uint8_t *data,
                        size_t size) {
  if (fd_ < 0 || !ip || port == 0 || !data)
    return false;
  sockaddr_in dst{};
  dst.sin_family = AF_INET;
  dst.sin_port = htons(port);
  if (inet_pton(AF_INET, ip, &dst.sin_addr) != 1)
    return false;
  return sendto(fd_, data, size, 0, reinterpret_cast<sockaddr *>(&dst),
                sizeof(dst)) == static_cast<int>(size);
}
bool UdpSocket::poll(Receiver &receiver, uint64_t now, bool healthy) {
  receiver.tick(now, healthy && fd_ >= 0);
  if (fd_ < 0)
    return false;
  // Bounded work prevents an input flood from starving the safety/control tick.
  for (int i = 0; i < 8; i++) {
    uint8_t bytes[packet_size + 1];
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
    receiver.receive(ip, ntohs(source.sin_port), bytes, static_cast<size_t>(n),
                     now, healthy);
  }
  return true;
}
} // namespace sakura
