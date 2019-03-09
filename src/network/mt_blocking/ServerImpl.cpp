#include "ServerImpl.h"

#include <cassert>
#include <cstring>
#include <iostream>
#include <memory>
#include <stdexcept>

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <spdlog/logger.h>

#include <afina/Storage.h>
#include <afina/execute/Command.h>
#include <afina/logging/Service.h>

#include "protocol/Parser.h"

namespace Afina {
namespace Network {
namespace MTblocking {

// See Server.h
ServerImpl::ServerImpl(std::shared_ptr<Afina::Storage> ps, std::shared_ptr<Logging::Service> pl) : Server(ps, pl) {}

// See Server.h
ServerImpl::~ServerImpl() {}

// See Server.h
void ServerImpl::Start(uint16_t port, uint32_t n_accept, uint32_t n_workers)
{
    // MY
    _max_workers = n_workers;
    // --------
    _logger = pLogging->select("network");
    _logger->info("Start mt_blocking network service");

    sigset_t sig_mask;
    sigemptyset(&sig_mask);
    sigaddset(&sig_mask, SIGPIPE);
    if (pthread_sigmask(SIG_BLOCK, &sig_mask, NULL) != 0) {
        throw std::runtime_error("Unable to mask SIGPIPE");
    }

    struct sockaddr_in server_addr;
    std::memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;         // IPv4
    server_addr.sin_port = htons(port);       // TCP port number
    server_addr.sin_addr.s_addr = INADDR_ANY; // Bind to any address

    _server_socket = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (_server_socket == -1) {
        throw std::runtime_error("Failed to open socket");
    }

    int opts = 1;
    if (setsockopt(_server_socket, SOL_SOCKET, SO_REUSEADDR, &opts, sizeof(opts)) == -1) {
        close(_server_socket);
        throw std::runtime_error("Socket setsockopt() failed");
    }

    if (bind(_server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
        close(_server_socket);
        throw std::runtime_error("Socket bind() failed");
    }

    if (listen(_server_socket, 5) == -1) {
        close(_server_socket);
        throw std::runtime_error("Socket listen() failed");
    }
    running.store(true);
    _thread = std::thread(&ServerImpl::OnRun, this);
}

void ServerImpl::Stop()
{
    running.store(false);
    shutdown(_server_socket, SHUT_RDWR);
}

// See Server.h
void ServerImpl::Join()
{
    assert(_thread.joinable());
    _thread.join();
    std::unique_lock<std::mutex> _lock(_mutex_for_stop);
    _alive.wait(_lock, [this] { return this->_now_workers == 0; });
    close(_server_socket);
}

// See Server.h
void ServerImpl::OnRun()
{
    while (running.load())
    {
        _logger->debug("waiting for connection...");

        // The call to accept() blocks until the incoming connection arrives
        int client_socket;
        struct sockaddr client_addr;
        socklen_t client_addr_len = sizeof(client_addr);
        if ((client_socket = accept(_server_socket, (struct sockaddr *)&client_addr, &client_addr_len)) == -1) {
            continue;
        }

        // Got new connection
        if (_logger->should_log(spdlog::level::debug))
        {
            std::string host = "unknown", port = "-1";

            char hbuf[NI_MAXHOST], sbuf[NI_MAXSERV];
            if (getnameinfo(&client_addr, client_addr_len, hbuf, sizeof(hbuf), sbuf, sizeof(sbuf),
                            NI_NUMERICHOST | NI_NUMERICSERV) == 0)
            {
                host = hbuf;
                port = sbuf;
            }
            _logger->debug("Accepted connection on descriptor {} (host={}, port={})\n", client_socket, host, port);
        }

        // Configure read timeout
        {
            struct timeval tv;
            tv.tv_sec = 5; // TODO: make it configurable
            tv.tv_usec = 0;
            setsockopt(client_socket, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tv, sizeof tv);
        }

        // TODO: Start new thread and process data from/to connection
        {
            std::lock_guard<std::mutex> lock(_mutex);
            if (_now_workers < _max_workers)
            {
                    _now_workers++;
                    std::thread(&ServerImpl::GoJoniGo, this, client_socket).detach();
            }
        }
        close(client_socket);
    }

    // Cleanup on exit...
    _logger->warn("Network stopped");
}


void ServerImpl::GoJoniGo(int client_socket) {
    // Here is connection state
    // - parser: parse state of the stream
    // - command_to_execute: last command parsed out of stream
    // - arg_remains: how many bytes to read from stream to get command argument
    // - argument_for_command: buffer stores argument
    std::size_t arg_remains;
    // Standart Parser from Protocol::parser
    // Commdnds:
    // bool Parse(const std::string &input, size_t &parsed)
    // bool Parse(const std::string &input, size_t &parsed)
    // std::unique_ptr<Execute::Command> Build(size_t &body_size) const
    // void Reset();
    Protocol::Parser parser;
    std::string argument_for_command;
    std::unique_ptr<Execute::Command> command_to_execute;
    // Process new connection:
    // - read commands until socket alive
    // - execute each command
    // - send response
    try {
        int readed_bytes = -1;
        char client_buffer[4096];
        // TODO: в st_block после завершения работы сервера уже
        // подключённый пользователь может продолжать слать ещё команды, как я понял.
        // в моём варианте mt_block при завершении работы сервера корректно
        // довыполняется текущая команда, после чего соединение клиента
        // закрываем
        while ((readed_bytes = read(client_socket, client_buffer, sizeof(client_buffer))) > 0)
        {
            // !!! Получаем readed_bytes от сокета
            _logger->debug("Got {} bytes from socket", readed_bytes);
            while (readed_bytes > 0)
            {
                _logger->debug("Process {} bytes", readed_bytes);
                // Если еще не запарсилась командая
                if (!command_to_execute)
                {
                    std::size_t parsed = 0;

                    // bool Parse(const std::string &input, size_t &parsed)
                    // bool Parse(const char *input, const size_t size, size_t &parsed);
                    if (parser.Parse(client_buffer, readed_bytes, parsed))
                    {
                        // Найдена новая команда parser.Name
                        command_to_execute = parser.Build(arg_remains);
                        if (arg_remains > 0)
                        {
                            arg_remains += 2;
                        }
                    }
                    // Обрабатываем корректно, когда нашли команду в n байтах
                    // Но т.к изначально подали new_bytes= > n
                    // то надо вернуть корретку
                    if (parsed == 0)
                    {
                        break;
                    }
                    else
                    {
                        std::memmove(client_buffer, client_buffer + parsed, readed_bytes - parsed);
                        readed_bytes -= parsed;
                    }
                }

                // There is command, but we still wait for argument to arrive...
                if (command_to_execute && arg_remains > 0) {
                    _logger->debug("Fill argument: {} bytes of {}", readed_bytes, arg_remains);
                    // There is some parsed command, and now we are reading argument
                    std::size_t to_read = std::min(arg_remains, std::size_t(readed_bytes));
                    argument_for_command.append(client_buffer, to_read);

                    std::memmove(client_buffer, client_buffer + to_read, readed_bytes - to_read);
                    arg_remains -= to_read;
                    readed_bytes -= to_read;
                }

                // There is command & argument - RUN!
                if (command_to_execute && arg_remains == 0) {
                    _logger->debug("Start command execution");

                    std::string result;
                    command_to_execute->Execute(*pStorage, argument_for_command, result);
                    // Send response
                    result += "\r\n";
                    if (send(client_socket, result.data(), result.size(), 0) <= 0) {
                        throw std::runtime_error("Failed to send response");
                    }

                    // Prepare for the next command
                    command_to_execute.reset();
                    argument_for_command.resize(0);
                    parser.Reset();
                }
            } // while (readed_bytes)
        }
        if (readed_bytes == 0)
        {
            _logger->debug("Connection closed");
        }
        else
        {
            throw std::runtime_error(std::string(strerror(errno)));
        }
      }
      catch (std::runtime_error &ex)
      {
          _logger->error("Failed to process connection on descriptor {}: {}", client_socket, ex.what());
      }
    close(client_socket);

// ----------------------------
std::lock_guard<std::mutex> lock(_mutex);
_now_workers--;
_alive.notify_one();
}

} // namespace MTblocking
} // namespace Network
} // namespace Afina
