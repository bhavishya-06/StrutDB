#include "core/graph.hpp"
#include "execution/executor.hpp"
#include "network/server.hpp"
#include "scripting/lua_engine.hpp"

#include <atomic>
#include <csignal>
#include <cstring>
#include <iostream>
#include <memory>
#include <unistd.h>

namespace {

// Global flag for shutdown request (signal-safe)
std::atomic<bool> g_shutdown_requested(false);

// Signal handler for SIGINT and SIGTERM
void signal_handler(int signal) {
    (void)signal;  // Unused parameter
    g_shutdown_requested.store(true);
}

// Register signal handlers
void register_signal_handlers() {
    struct sigaction sa;
    std::memset(&sa, 0, sizeof(sa));
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    if (sigaction(SIGINT, &sa, nullptr) == -1) {
        std::cerr << "Failed to register SIGINT handler: " << strerror(errno) << std::endl;
        exit(1);
    }

    if (sigaction(SIGTERM, &sa, nullptr) == -1) {
        std::cerr << "Failed to register SIGTERM handler: " << strerror(errno) << std::endl;
        exit(1);
    }
}

}  // anonymous namespace

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;

    std::cout << "StrutDB v0.1.0 - High-performance in-memory graph database" << std::endl;
    std::cout << "Starting server on port 6379..." << std::endl;

    // Register signal handlers for graceful shutdown
    register_signal_handlers();

    try {
        // Create core components
        auto graph = std::make_shared<strutdb::core::Graph>();
        auto lua_engine = std::make_shared<strutdb::scripting::LuaScriptEngine>(graph);
        auto executor = std::make_shared<strutdb::execution::Executor>(graph);

        // Create and start network server
        strutdb::network::Server server(graph, executor, lua_engine);

        if (!server.start(6379)) {
            std::cerr << "Failed to start server on port 6379" << std::endl;
            return 1;
        }

        std::cout << "Server listening on port 6379" << std::endl;
        std::cout << "Press Ctrl+C to stop" << std::endl;

        // Run event loop until shutdown is requested
        server.run();

        // Graceful shutdown
        std::cout << "\nShutting down gracefully..." << std::endl;
        server.stop();

        std::cout << "Server stopped successfully" << std::endl;

    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    } catch (...) {
        std::cerr << "Unknown fatal error" << std::endl;
        return 1;
    }

    return 0;
}
