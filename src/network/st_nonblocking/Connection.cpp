#include "Connection.h"

#include <iostream>
#include <sys/uio.h>



namespace Afina {
namespace Network {
namespace STnonblock {

// See Connection.h
void Connection::Start() {
  _event.events = EPOLLRDHUP | EPOLLERR; // если вдруг ошибки или соединение закрылось
  _event.events |= EPOLLIN; // хотим сначала читать
  _event.data.fd = _socket; // чтоб знать какой сокет\
  _logger->info("Start on socket {}", _socket);
}

// See Connection.h
void Connection::OnError() {
  flag = false;
  _logger->error("Error on socket {}", _socket);
}

// See Connection.h
void Connection::OnClose() {
  flag = false;
  _logger->info("Close on socket {}", _socket);
}

// See Connection.h
void Connection::DoRead() {
    _logger->debug("Do read on {} socket", _socket);
    try {
        int got_bytes = -1;
        while ((got_bytes = read(_socket, client_buffer + already_read_bytes,
                                 sizeof(client_buffer) - already_read_bytes)) > 0) {
            already_read_bytes += got_bytes;
            _logger->debug("Got {} bytes from socket", got_bytes);

            // Single block of data read from the socket could trigger inside actions a multiple times,
            // for example:
            // - read#0: [<command1 start>]
            // - read#1: [<command1 end> <argument> <command2> <argument for command 2> <command3> ... ]
            while (already_read_bytes > 0) {
                _logger->debug("Process {} bytes", already_read_bytes);
                // There is no command yet
                if (!command_to_execute) {
                    std::size_t parsed = 0;
                    if (parser.Parse(client_buffer, already_read_bytes, parsed)) {
                        // There is no command to be launched, continue to parse input stream
                        // Here we are, current chunk finished some command, process it
                        _logger->debug("Found new command: {} in {} bytes", parser.Name(), parsed);
                        command_to_execute = parser.Build(arg_remains);
                        if (arg_remains > 0) {
                            arg_remains += 2;
                        }
                    }

                    // Parsed might fails to consume any bytes from input stream. In real life that could happens,
                    // for example, because we are working with UTF-16 chars and only 1 byte left in stream
                    if (parsed == 0) {
                        break;
                    } else {
                        std::memmove(client_buffer, client_buffer + parsed, already_read_bytes - parsed);
                        already_read_bytes -= parsed;
                    }
                }

                // There is command, but we still wait for argument to arrive...
                if (command_to_execute && arg_remains > 0) {
                    _logger->debug("Fill argument: {} bytes of {}", already_read_bytes, arg_remains);
                    // There is some parsed command, and now we are reading argument
                    std::size_t to_read = std::min(arg_remains, std::size_t(already_read_bytes));
                    argument_for_command.append(client_buffer, to_read);

                    std::memmove(client_buffer, client_buffer + to_read, already_read_bytes - to_read);
                    arg_remains -= to_read;
                    already_read_bytes -= to_read;
                }

                // Thre is command & argument - RUN!
                if (command_to_execute && arg_remains == 0) {
                    _logger->debug("Start command execution");

                    std::string result;
                    command_to_execute->Execute(*pStorage, argument_for_command, result);

                    // Save response
                    result += "\r\n";

                    bool add_EPOLLOUT = bufer.empty();

                    bufer.push_back(result);
                    _event.events |= EPOLLOUT;

                    // Prepare for the next command
                    command_to_execute.reset();
                    argument_for_command.resize(0);
                    parser.Reset();
                }
            } // while (read_bytes)
        }

        flag = false;
        if (got_bytes == 0) {
            _logger->debug("Connection closed");
        } else {
            throw std::runtime_error(std::string(strerror(errno)));
        }
    } catch (std::runtime_error &ex) {
        _logger->error("Failed to process connection on descriptor {}: {}", _socket, ex.what());
    }
}

// See Connection.h
void Connection::DoWrite() {
    _logger->info("Write on socket {}", _socket);
//struct iovec {
//void *iov_base; /* начальный адрес буфера */
//size_t iov_len; /* размер буфера */
//};

  //TODO может падать если надо отправить кучу сообщений
    struct iovec iovecs[bufer.size()];
    for (int i = 0; i < bufer.size(); i++) {
        iovecs[i].iov_len = bufer[i].size();
        iovecs[i].iov_base = &(bufer[i][0]);
    }
    // void* to char*  and + pos
    iovecs[0].iov_base = static_cast<char *>(iovecs[0].iov_base) + pos;
    iovecs[0].iov_len -= pos;

    // ssize_t writev(int filedes, const struct iovec *iov, int iovcnt);
    int writed_bytes = writev(_socket, iovecs, bufer.size());
    if (writed_bytes <= 0) {
        flag = false;
        throw std::runtime_error("Failed to send response");
    }

    pos += writed_bytes;
    //TODO заменить на итератор
    int i = 0;
    while ((i < bufer.size()) && ((pos >= iovecs[i].iov_len))) {
        i++;
        pos -= iovecs[i].iov_len;
    }

    bufer.erase(bufer.begin(), bufer.begin() + i);
    if (bufer.empty())
        _event.events = EPOLLRDHUP | EPOLLERR | EPOLLIN;
}

} // namespace STnonblock
} // namespace Network
} // namespace Afina
