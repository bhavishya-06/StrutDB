#include "network/server.hpp"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <stdexcept>
#include <system_error>

namespace strutdb::network {

ClientSession::ClientSession(int fd)
    : fd_(fd) {
    input_buffer_.reserve(4096);
    output_buffer_.reserve(4096);
}

ClientSession::~ClientSession() {
    close();
}

void ClientSession::close() {
    if (fd_ != -1) {
        ::close(fd_);
        fd_ = -1;
    }
    input_buffer_.clear();
    output_buffer_.clear();
}

Server::Server(std::shared_ptr<core::Graph>                graph,
               std::shared_ptr<execution::Executor>        executor,
               std::shared_ptr<scripting::LuaScriptEngine> lua_engine)
    : epoll_fd_(-1)
    , listen_fd_(-1)
    , running_(false)
    , graph_(std::move(graph))
    , executor_(std::move(executor))
    , lua_engine_(std::move(lua_engine)) {}

Server::~Server() {
    stop();
}

bool Server::start(int port) {
    if (!initialize(port)) {
        return false;
    }
    running_ = true;
    return true;
}

void Server::run() {
    if (!running_) {
        return;
    }

    std::vector<struct epoll_event> events(kMaxEvents);

    while (running_) {
        int nfds = epoll_wait(epoll_fd_, events.data(), kMaxEvents, -1);
        if (nfds == -1) {
            if (errno == EINTR) {
                continue;  // Interrupted by signal, continue
            }
            throw std::system_error(errno, std::generic_category(), "epoll_wait failed");
        }

        for (int i = 0; i < nfds; ++i) {
            struct epoll_event& ev = events[i];

            if (ev.data.fd == listen_fd_) {
                // New connection
                handle_accept();
            } else {
                // Client event
                if (ev.events & (EPOLLERR | EPOLLHUP)) {
                    // Error or hangup
                    handle_error(ev.data.fd);
                } else if (ev.events & EPOLLIN) {
                    // Read event
                    handle_read(ev.data.fd);
                } else if (ev.events & EPOLLOUT) {
                    // Write event
                    handle_write(ev.data.fd);
                }
            }
        }
    }
}

void Server::stop() {
    running_ = false;

    // Close all client sessions
    clients_.clear();

    // Close listener socket
    if (listen_fd_ != -1) {
        ::close(listen_fd_);
        listen_fd_ = -1;
    }

    // Close epoll fd
    if (epoll_fd_ != -1) {
        ::close(epoll_fd_);
        epoll_fd_ = -1;
    }
}

bool Server::initialize(int port) {
    // Create epoll instance
    epoll_fd_ = epoll_create1(0);
    if (epoll_fd_ == -1) {
        return false;
    }

    // Create listener socket
    listen_fd_ = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if (listen_fd_ == -1) {
        ::close(epoll_fd_);
        epoll_fd_ = -1;
        return false;
    }

    // Set SO_REUSEADDR
    int opt = 1;
    if (setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1) {
        ::close(listen_fd_);
        ::close(epoll_fd_);
        listen_fd_ = -1;
        epoll_fd_ = -1;
        return false;
    }

    // Bind to address
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (bind(listen_fd_, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) == -1) {
        ::close(listen_fd_);
        ::close(epoll_fd_);
        listen_fd_ = -1;
        epoll_fd_ = -1;
        return false;
    }

    // Listen
    if (listen(listen_fd_, 128) == -1) {
        ::close(listen_fd_);
        ::close(epoll_fd_);
        listen_fd_ = -1;
        epoll_fd_ = -1;
        return false;
    }

    // Add listener to epoll
    struct epoll_event ev;
    ev.events = EPOLLIN;  // Level-triggered for listener
    ev.data.fd = listen_fd_;
    if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, listen_fd_, &ev) == -1) {
        ::close(listen_fd_);
        ::close(epoll_fd_);
        listen_fd_ = -1;
        epoll_fd_ = -1;
        return false;
    }

    return true;
}

void Server::handle_accept() {
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);

    while (true) {
        int client_fd = accept(listen_fd_,
                               reinterpret_cast<struct sockaddr*>(&client_addr),
                               &client_len);
        if (client_fd == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // No more connections
                break;
            }
            // Error
            return;
        }

        // Set non-blocking
        if (!set_non_blocking(client_fd)) {
            ::close(client_fd);
            continue;
        }

        // Create client session
        auto session = std::make_unique<ClientSession>(client_fd);

        // Register with epoll (level-triggered for clients)
        if (!register_client(client_fd)) {
            ::close(client_fd);
            continue;
        }

        // Add to clients map
        clients_[client_fd] = std::move(session);
    }
}

void Server::handle_read(int fd) {
    auto it = clients_.find(fd);
    if (it == clients_.end()) {
        return;
    }

    auto& session = it->second;
    auto& input_buffer = session->input_buffer();

    // Read data
    std::uint8_t temp_buffer[4096];
    while (true) {
        ssize_t n = recv(fd, temp_buffer, sizeof(temp_buffer), 0);
        if (n == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // No more data
                break;
            }
            // Error
            handle_error(fd);
            return;
        } else if (n == 0) {
            // Client closed connection
            handle_error(fd);
            return;
        }

        // Append to input buffer
        if (input_buffer.size() + n > ClientSession::kMaxBufferSize) {
            // Buffer overflow
            handle_error(fd);
            return;
        }
        input_buffer.insert(input_buffer.end(), temp_buffer, temp_buffer + n);
    }

    // Parse commands
    std::size_t consumed = 0;
    while (true) {
        std::size_t cmd_consumed = 0;
        if (session->parser().parse(input_buffer, cmd_consumed)) {
            // Command complete
            Command cmd = session->parser().get_command();
            process_command(*session, cmd);
            session->parser().reset();

            // Consume parsed bytes
            input_buffer.erase(input_buffer.begin(), input_buffer.begin() + cmd_consumed);
            consumed += cmd_consumed;
        } else {
            // Incomplete command, wait for more data
            break;
        }
    }
}

void Server::handle_write(int fd) {
    auto it = clients_.find(fd);
    if (it == clients_.end()) {
        return;
    }

    auto& session = it->second;
    auto& output_buffer = session->output_buffer();

    if (output_buffer.empty()) {
        // Nothing to write, disable EPOLLOUT
        struct epoll_event ev;
        ev.events = EPOLLIN;  // Level-triggered read
        ev.data.fd = fd;
        epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, fd, &ev);
        return;
    }

    // Write data
    while (!output_buffer.empty()) {
        ssize_t n = send(fd, output_buffer.data(), output_buffer.size(), 0);
        if (n == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // Would block, wait for next EPOLLOUT
                break;
            }
            // Error
            handle_error(fd);
            return;
        } else if (n == 0) {
            // Should not happen
            handle_error(fd);
            return;
        }

        // Remove written bytes
        output_buffer.erase(output_buffer.begin(), output_buffer.begin() + n);
    }

    // If buffer is now empty, disable EPOLLOUT
    if (output_buffer.empty()) {
        struct epoll_event ev;
        ev.events = EPOLLIN;  // Level-triggered read
        ev.data.fd = fd;
        epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, fd, &ev);
    }
}

void Server::handle_error(int fd) {
    close_client(fd);
}

void Server::process_command(ClientSession& session, const Command& cmd) {
    auto& output_buffer = session.output_buffer();

    if (cmd.name == "PING") {
        // PING command
        auto response = RespFormatter::format_simple_string("PONG");
        output_buffer.insert(output_buffer.end(), response.begin(), response.end());
    } else if (cmd.name == "SET") {
        // SET command: SET key value
        if (cmd.args.size() < 2) {
            auto error = RespFormatter::format_error("ERR wrong number of arguments for 'SET'");
            output_buffer.insert(output_buffer.end(), error.begin(), error.end());
            return;
        }

        // Extract key and value
        std::string key;
        std::string value;

        if (std::holds_alternative<std::string>(cmd.args[0])) {
            key = std::get<std::string>(cmd.args[0]);
        } else if (std::holds_alternative<RespBulkString>(cmd.args[0])) {
            auto& bulk = std::get<RespBulkString>(cmd.args[0]);
            key = std::string(bulk.begin(), bulk.end());
        }

        if (std::holds_alternative<std::string>(cmd.args[1])) {
            value = std::get<std::string>(cmd.args[1]);
        } else if (std::holds_alternative<RespBulkString>(cmd.args[1])) {
            auto& bulk = std::get<RespBulkString>(cmd.args[1]);
            value = std::string(bulk.begin(), bulk.end());
        }

        // Set in graph KV store
        core::PropertyValue prop_value(value);
        graph_->set(key, prop_value);

        auto response = RespFormatter::format_simple_string("OK");
        output_buffer.insert(output_buffer.end(), response.begin(), response.end());

    } else if (cmd.name == "GET") {
        // GET command: GET key
        if (cmd.args.size() < 1) {
            auto error = RespFormatter::format_error("ERR wrong number of arguments for 'GET'");
            output_buffer.insert(output_buffer.end(), error.begin(), error.end());
            return;
        }

        std::string key;
        if (std::holds_alternative<std::string>(cmd.args[0])) {
            key = std::get<std::string>(cmd.args[0]);
        } else if (std::holds_alternative<RespBulkString>(cmd.args[0])) {
            auto& bulk = std::get<RespBulkString>(cmd.args[0]);
            key = std::string(bulk.begin(), bulk.end());
        }

        // Get from graph KV store
        auto result = graph_->get(key);
        if (result) {
            if (std::holds_alternative<std::string>(*result)) {
                auto response = RespFormatter::format_bulk_string(std::get<std::string>(*result));
                output_buffer.insert(output_buffer.end(), response.begin(), response.end());
            } else {
                auto response = RespFormatter::format_null_bulk_string();
                output_buffer.insert(output_buffer.end(), response.begin(), response.end());
            }
        } else {
            auto response = RespFormatter::format_null_bulk_string();
            output_buffer.insert(output_buffer.end(), response.begin(), response.end());
        }

    } else if (cmd.name == "DEL") {
        // DEL command: DEL key
        if (cmd.args.size() < 1) {
            auto error = RespFormatter::format_error("ERR wrong number of arguments for 'DEL'");
            output_buffer.insert(output_buffer.end(), error.begin(), error.end());
            return;
        }

        std::string key;
        if (std::holds_alternative<std::string>(cmd.args[0])) {
            key = std::get<std::string>(cmd.args[0]);
        } else if (std::holds_alternative<RespBulkString>(cmd.args[0])) {
            auto& bulk = std::get<RespBulkString>(cmd.args[0]);
            key = std::string(bulk.begin(), bulk.end());
        }

        // Delete from graph KV store
        bool deleted = graph_->del(key);

        auto response = RespFormatter::format_integer(deleted ? 1 : 0);
        output_buffer.insert(output_buffer.end(), response.begin(), response.end());

    } else if (cmd.name == "GRAPH.QUERY") {
        // GRAPH.QUERY command: GRAPH.QUERY "cypher_query"
        if (cmd.args.size() < 1) {
            auto error = RespFormatter::format_error("ERR wrong number of arguments for 'GRAPH.QUERY'");
            output_buffer.insert(output_buffer.end(), error.begin(), error.end());
            return;
        }

        std::string cypher;
        if (std::holds_alternative<std::string>(cmd.args[0])) {
            cypher = std::get<std::string>(cmd.args[0]);
        } else if (std::holds_alternative<RespBulkString>(cmd.args[0])) {
            auto& bulk = std::get<RespBulkString>(cmd.args[0]);
            cypher = std::string(bulk.begin(), bulk.end());
        }

        // Parse and execute Cypher query
        // TODO: Implement Cypher parser integration
        auto error = RespFormatter::format_error("ERR GRAPH.QUERY not yet implemented");
        output_buffer.insert(output_buffer.end(), error.begin(), error.end());

    } else if (cmd.name == "EVAL") {
        // EVAL command: EVAL "lua_script"
        if (cmd.args.size() < 1) {
            auto error = RespFormatter::format_error("ERR wrong number of arguments for 'EVAL'");
            output_buffer.insert(output_buffer.end(), error.begin(), error.end());
            return;
        }

        std::string script;
        if (std::holds_alternative<std::string>(cmd.args[0])) {
            script = std::get<std::string>(cmd.args[0]);
        } else if (std::holds_alternative<RespBulkString>(cmd.args[0])) {
            auto& bulk = std::get<RespBulkString>(cmd.args[0]);
            script = std::string(bulk.begin(), bulk.end());
        }

        // Execute Lua script
        auto result = lua_engine_->eval(script);
        if (result.success) {
            auto response = RespFormatter::format_simple_string(result.output);
            output_buffer.insert(output_buffer.end(), response.begin(), response.end());
        } else {
            auto error = RespFormatter::format_error("ERR " + result.error_message);
            output_buffer.insert(output_buffer.end(), error.begin(), error.end());
        }

    } else {
        // Unknown command
        auto error = RespFormatter::format_error("ERR unknown command '" + cmd.name + "'");
        output_buffer.insert(output_buffer.end(), error.begin(), error.end());
    }

    // Enable EPOLLOUT to send response
    struct epoll_event ev;
    ev.events = EPOLLIN | EPOLLOUT;  // Level-triggered read + write
    ev.data.fd = session.fd();
    epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, session.fd(), &ev);
}

bool Server::register_client(int fd) {
    struct epoll_event ev;
    ev.events = EPOLLIN;  // Level-triggered read
    ev.data.fd = fd;
    return epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, fd, &ev) == 0;
}

void Server::close_client(int fd) {
    // Deregister from epoll
    epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, nullptr);

    // Remove from clients map (automatically closes fd via ClientSession destructor)
    clients_.erase(fd);
}

bool Server::set_non_blocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) {
        return false;
    }
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK) != -1;
}

}  // namespace strutdb::network
