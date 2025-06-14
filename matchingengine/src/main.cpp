#include <iostream>
#include <thread>
#include <vector>
#include <csignal>
#include <atomic>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/rotating_file_sink.h>

// Command line parsing
#include <cxxopts.hpp>

// Core includes
#include "core/matching_engine.hpp"
#include "network/feed_handler.hpp"
#include "monitoring/metrics_server.hpp"
#include "utils/time_utils.hpp"

std::atomic<bool> g_running{true};

void signal_handler(int signal)
{
    spdlog::info("Received signal {}, shutting down...", signal);
    g_running = false;
}

void setup_logging(const std::string &log_level)
{
    // Console sink
    auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    console_sink->set_level(spdlog::level::info);

    // File sink with rotation
    auto file_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
        "logs/matching_engine.log", 1048576 * 100, 10); // 100MB x 10 files
    file_sink->set_level(spdlog::level::debug);

    // Create logger with both sinks
    std::vector<spdlog::sink_ptr> sinks{console_sink, file_sink};
    auto logger = std::make_shared<spdlog::logger>("main", sinks.begin(), sinks.end());

    // Set log level
    if (log_level == "debug")
    {
        logger->set_level(spdlog::level::debug);
    }
    else if (log_level == "info")
    {
        logger->set_level(spdlog::level::info);
    }
    else if (log_level == "warn")
    {
        logger->set_level(spdlog::level::warn);
    }
    else if (log_level == "error")
    {
        logger->set_level(spdlog::level::err);
    }

    // Register as default logger
    spdlog::set_default_logger(logger);
    spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%t] %v");
}

int main(int argc, char *argv[])
{
    try
    {
        // Parse command line options
        cxxopts::Options options("matching_engine",
                                 "High-performance matching engine demonstrating A/B feed arbitrage");

        options.add_options()("s,symbols", "Comma-separated list of symbols",
                              cxxopts::value<std::string>()->default_value("AAPL,GOOGL,MSFT"))("r,order-rate", "Orders per second",
                                                                                               cxxopts::value<int>()->default_value("1000000"))("j,enable-jitter", "Enable jitter injection",
                                                                                                                                                cxxopts::value<bool>()->default_value("true"))("a,feed-a-latency", "Feed A base latency in microseconds",
                                                                                                                                                                                               cxxopts::value<int>()->default_value("50"))("b,feed-b-latency", "Feed B base latency in microseconds",
                                                                                                                                                                                                                                           cxxopts::value<int>()->default_value("150"))("p,prometheus-port", "Prometheus metrics port",
                                                                                                                                                                                                                                                                                        cxxopts::value<int>()->default_value("8000"))("w,websocket-port", "WebSocket market data port",
                                                                                                                                                                                                                                                                                                                                      cxxopts::value<int>()->default_value("8080"))("l,log-level", "Log level (debug,info,warn,error)",
                                                                                                                                                                                                                                                                                                                                                                                    cxxopts::value<std::string>()->default_value("info"))("c,cpu-affinity", "Enable CPU affinity",
                                                                                                                                                                                                                                                                                                                                                                                                                                          cxxopts::value<bool>()->default_value("true"))("h,help", "Print help");

        auto result = options.parse(argc, argv);

        if (result.count("help"))
        {
            std::cout << options.help() << std::endl;
            return 0;
        }

        // Setup logging
        setup_logging(result["log-level"].as<std::string>());

        // Log startup info
        spdlog::info("=== MicroMatch Matching Engine Starting ===");
        spdlog::info("Demonstrating sub-microsecond market data jitter and A/B feed arbitrage");

        // Parse symbols
        std::vector<std::string> symbols;
        std::string symbol_str = result["symbols"].as<std::string>();
        size_t pos = 0;
        while ((pos = symbol_str.find(',')) != std::string::npos)
        {
            symbols.push_back(symbol_str.substr(0, pos));
            symbol_str.erase(0, pos + 1);
        }
        if (!symbol_str.empty())
        {
            symbols.push_back(symbol_str);
        }

        spdlog::info("Trading symbols: {}", fmt::join(symbols, ", "));
        spdlog::info("Order rate: {} orders/second", result["order-rate"].as<int>());
        spdlog::info("Feed A latency: {} μs", result["feed-a-latency"].as<int>());
        spdlog::info("Feed B latency: {} μs", result["feed-b-latency"].as<int>());

        // Setup signal handlers
        std::signal(SIGINT, signal_handler);
        std::signal(SIGTERM, signal_handler);

        // Calibrate TSC
        spdlog::info("Calibrating TSC frequency...");
        double tsc_freq = micromatch::utils::TimeUtils::calibrate_tsc_frequency();
        spdlog::info("TSC frequency: {:.2f} GHz", tsc_freq / 1e9);

        // CPU affinity
        if (result["cpu-affinity"].as<bool>())
        {
            cpu_set_t cpuset;
            CPU_ZERO(&cpuset);
            CPU_SET(0, &cpuset); // Pin to CPU 0
            if (pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset) == 0)
            {
                spdlog::info("Set CPU affinity to core 0");
            }
        }

        // Start metrics server
        spdlog::info("Starting Prometheus metrics server on port {}",
                     result["prometheus-port"].as<int>());
        // TODO: Initialize metrics server

        // Create matching engine
        spdlog::info("Initializing matching engine...");
        // TODO: Initialize matching engine

        // Create feed handlers
        spdlog::info("Initializing A/B feed handlers...");
        // TODO: Initialize feed handlers

        // Main loop
        spdlog::info("Matching engine running. Press Ctrl+C to stop.");

        while (g_running)
        {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            // TODO: Print statistics
        }

        spdlog::info("Shutting down...");
    }
    catch (const std::exception &e)
    {
        spdlog::error("Fatal error: {}", e.what());
        return 1;
    }

    return 0;
}