#pragma once

#include "core/graph.hpp"
#include "execution/executor.hpp"
#include "network/resp.hpp"
#include "scripting/lua_engine.hpp"

#include <memory>
#include <unordered_map>
#include <vector>

namespace strutdb::network {

// Client session with I/O buffers
class ClientSession {
public:
    explicit ClientSession(int fd);
    ~ClientSession();

    ClientSession(const ClientSession&)            = delete;
    ClientSession& operator=(const ClientSession&) = delete;
    ClientSession(ClientSession&&)                 = delete;
    ClientSession& operator=(ClientSession&&)      = delete;

    int                     fd() const { return fd_; }
    std::vector<std::uint8_t>& input_buffer() { return input_buffer_; }
    std::vector<std::uint8_t>& output_buffer() { return output_buffer_; }
    RespParser&             parser() { return parser_; }

    void close();

private:
    int                           fd_;
    std::vector<std::uint8_t>    input_buffer_;
    std::vector<std::uint8_t>    output_buffer_;
    RespParser                    parser_;
    static constexpr std::size_t  kMaxBufferSize = 16 * 1024 * 1024;  // 16MB
};

// TCP server with epoll event loop
class Server {
public:
    Server(std::shared_ptr<core::Graph>         graph,
           std::shared_ptr<execution::Executor> executor,
           std::shared_ptr<scripting::LuaScriptEngine> lua_engine);

    ~Server();

    Server(const Server&)            = delete;
    Server& operator=(const Server&) = delete;
    Server(Server&&)                 = delete;
    Server& operator=(Server&&)      = delete;

    // Start the server on the specified port (default 6379)
    [[nodiscard]] bool start(int port = 6379);

    // Run the event loop (blocking)
    void run();

    // Stop the server
    void stop();

private:
    // Initialize epoll and listener socket
    [[nodiscard]] bool initialize(int port);

    // Accept new connections
    void handle_accept();

    // Handle read event from client
    void handle_read(int fd);

    // Handle write event to client
    void handle_write(int fd);

    // Handle error/hangup event from client
    void handle_error(int fd);

    // Process a parsed command
    void process_command(ClientSession& session, const Command& cmd);

    // Register client with epoll
    [[nodiscard]] bool register_client(int fd);

    // Deregister and close client
    void close_client(int fd);

    // Set socket to non-blocking mode
    [[nodiscard]] bool set_non_blocking(int fd);

    int                                          epoll_fd_;
    int                                          listen_fd_;
    bool                                         running_;
    std::unordered_map<int, std::unique_ptr<ClientSession>> clients_;
    std::shared_ptr<core::Graph>                 graph_;
    std::shared_ptr<execution::Executor>         executor_;
    std::shared_ptr<scripting::LuaScriptEngine>  lua_engine_;
    static constexpr int                         kMaxEvents = 1024;
};

}  // namespace strutdb::network
