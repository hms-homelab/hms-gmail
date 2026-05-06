#pragma once
#include <functional>
#include <string>
#include <sstream>
#include <thread>
#include <atomic>
#include <stdexcept>
#include <ctime>
#include <chrono>
#include <algorithm>

// Minimal cron scheduler supporting 5-field expressions.
// Only numeric values and '*' are supported per field.
// Fires the callback on a detached thread; stop() prevents future fires.
class CronScheduler {
public:
    using Callback = std::function<void()>;

    CronScheduler(const std::string& cron_expr, Callback cb)
        : cb_(std::move(cb)), running_(true) {
        parse(cron_expr);
        std::thread([this]{ loop(); }).detach();
    }

    void stop() { running_ = false; }

private:
    Callback  cb_;
    std::atomic<bool> running_;
    int minute_ = 0, hour_ = 0, dom_ = -1, month_ = -1, dow_ = -1;

    void parse(const std::string& expr) {
        int fields[5];
        int n = sscanf(expr.c_str(), "%d %d %d %d %d",
                       &fields[0], &fields[1], &fields[2], &fields[3], &fields[4]);
        // Use -1 for '*' (any). Count how many were parsed.
        std::string tokens[5];
        std::istringstream ss(expr);
        for (int i = 0; i < 5; ++i) {
            if (!(ss >> tokens[i]))
                throw std::runtime_error("CronScheduler: bad expression '" + expr + "'");
        }
        minute_ = tokens[0] == "*" ? -1 : std::stoi(tokens[0]);
        hour_   = tokens[1] == "*" ? -1 : std::stoi(tokens[1]);
        dom_    = tokens[2] == "*" ? -1 : std::stoi(tokens[2]);
        month_  = tokens[3] == "*" ? -1 : std::stoi(tokens[3]);
        dow_    = tokens[4] == "*" ? -1 : std::stoi(tokens[4]);
        (void)n;
    }

    // Returns seconds until the next fire time (max 1 week ahead).
    time_t secondsUntilNext() const {
        time_t now = time(nullptr);
        // Round up to next minute boundary
        time_t t = (now / 60 + 1) * 60;

        for (int attempts = 0; attempts < 60 * 24 * 7 + 1; ++t) {
            struct tm* tm = gmtime(&t);
            if (minute_ != -1 && tm->tm_min  != minute_) { ++attempts; t += 59; continue; }
            if (hour_   != -1 && tm->tm_hour  != hour_)  { ++attempts; t += 59; continue; }
            if (dow_    != -1 && tm->tm_wday  != dow_)   { ++attempts; t += 59; continue; }
            if (dom_    != -1 && tm->tm_mday  != dom_)   { ++attempts; t += 59; continue; }
            if (month_  != -1 && tm->tm_mon+1 != month_) { ++attempts; t += 59; continue; }
            return t - now;
        }
        return 7 * 24 * 3600; // fallback: 1 week
    }

    void loop() {
        while (running_) {
            time_t secs = secondsUntilNext();
            // Sleep in 60s chunks so stop() is responsive
            while (secs > 0 && running_) {
                time_t chunk = std::min(secs, (time_t)60);
                std::this_thread::sleep_for(std::chrono::seconds(chunk));
                secs -= chunk;
            }
            if (running_) {
                try { cb_(); } catch (...) {}
            }
        }
    }
};
