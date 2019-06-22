#ifndef AFINA_NETWORK_MT_NONBLOCKING_CONNECTION_H
#define AFINA_NETWORK_MT_NONBLOCKING_CONNECTION_H

#include <cstring>

#include <sys/epoll.h>
#include <vector>

#include <spdlog/logger.h>
#include <protocol/Parser.h>

#include <afina/execute/Command.h>




namespace Afina {
namespace Network {
namespace MTnonblock {

class Connection {
public:
  Connection(int s, std::shared_ptr<Afina::Storage> ps,
    std::shared_ptr<spdlog::logger> pl) : _socket(s), pStorage(std::move(ps)),
    _logger(std::move(pl)) {
      std::memset(&_event, 0, sizeof(struct epoll_event));
      _event.data.ptr = this; // ссылка которую мы тащим с собой
      _logger->info("New connection on socket {}", _socket);
  }

  inline bool isAlive() const { return flag; }

    void Start();

protected:
    void OnError();
    void OnClose();
    void DoRead();
    void DoWrite();

private:
    friend class Worker;
    friend class ServerImpl;

    bool flag = true;
    int _socket;
    struct epoll_event _event;


    std::mutex _mutex;
    int now_pos_bytes = 0;
    char client_buffer[4096];
    std::size_t arg_remains;
    Protocol::Parser parser;
    std::string argument_for_command;
    std::unique_ptr<Execute::Command> command_to_execute;
    std::shared_ptr<Afina::Storage> pStorage;
    std::shared_ptr<spdlog::logger> _logger;
    std::vector<std::string> bufer;

    int pos = 0;
};

} // namespace MTnonblock
} // namespace Network
} // namespace Afina

#endif // AFINA_NETWORK_MT_NONBLOCKING_CONNECTION_H
