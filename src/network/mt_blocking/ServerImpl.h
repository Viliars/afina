#ifndef AFINA_NETWORK_MT_BLOCKING_SERVER_H
#define AFINA_NETWORK_MT_BLOCKING_SERVER_H

#include <atomic>
#include <thread>

#include <afina/network/Server.h>

#include <mutex>
#include <condition_variable>
#include <set>


namespace spdlog {
class logger;
}

namespace Afina {
namespace Network {
namespace MTblocking {

/**
 * # Network resource manager implementation
 * Server that is spawning a separate thread for each connection
 */
class ServerImpl : public Server {
public:
    ServerImpl(std::shared_ptr<Afina::Storage> ps, std::shared_ptr<Logging::Service> pl);
    ~ServerImpl();

    // See Server.h
    void Start(uint16_t port, uint32_t n_accept, uint32_t n_workers) override;

    // See Server.h
    void Stop() override;

    // See Server.h
    void Join() override;

protected:
    /**
     * Method is running in the connection acceptor thread
     */
    void OnRun();

private:
    uint32_t _max_workers;
    uint32_t _now_workers = 0;
    std::mutex _mutex;
    std::mutex _mutex2;

    void GoJoniGo(int client_socket);

    std::condition_variable _alive;
    std::mutex _mutex_for_stop;

    friend bool _cond(ServerImpl* ser);

   // Logger instance
    std::shared_ptr<spdlog::logger> _logger;

    // Atomic flag to notify threads when it is time to stop. Note that
    // flag must be atomic in order to safely publisj changes cross thread
    // bounds
    std::atomic<bool> running;
    
    std::set<int> client_sockets;

    // Server socket to accept connections on
    int _server_socket;

    // Thread to run network on
    std::thread _thread;
};

} // namespace MTblocking
} // namespace Network
} // namespace Afina

#endif // AFINA_NETWORK_MT_BLOCKING_SERVER_H
