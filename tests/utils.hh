#pragma once

#include <algorithm>
#include <chrono>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace testing {
    static inline void Assert(bool condition, const std::string message) {
        if (!condition) {
            std::stringstream ss;
            ss << "Assertion failed: " << message << std::endl;
            std::cout << ss.str();
            throw std::runtime_error(message);
        }
    }

    class MultiTimer {
    public:
        MultiTimer(const MultiTimer &) = delete;

        MultiTimer() : name(), print(false) {}
        MultiTimer(std::string name, bool print = true) : name(name), print(print) {
            if (print) {
                std::stringstream ss;
                ss << "[" << name << "] Start" << std::endl;
                std::cout << ss.str();
            }
        }

        void AddValue(std::chrono::nanoseconds value) {
            values.emplace_back(value);
        }

        ~MultiTimer() {
            if (print) {
                std::stringstream ss;
                if (values.size() > 1) {
                    std::chrono::nanoseconds total(0);
                    for (auto value : values) {
                        total += value;
                    }
                    std::sort(values.begin(), values.end(), std::less<std::chrono::nanoseconds>());

                    ss << "[" << name << "] Min: " << (values[0].count() / 1000.0)
                       << " usec, Avg: " << (total.count() / values.size() / 1000.0)
                       << " usec, P95: " << (values[(size_t)((double)values.size() * 0.95) - 1].count() / 1000.0)
                       << " usec, P99: " << (values[(size_t)((double)values.size() * 0.99) - 1].count() / 1000.0)
                       << " usec, Total: " << (total.count() / 1000000.0) << " ms" << std::endl;
                } else if (values.size() == 1) {
                    ss << "[" << name << "] End: " << (values[0].count() / 1000000.0) << " ms" << std::endl;
                } else {
                    ss << "[" << name << "] No timers completed" << std::endl;
                }
                std::cout << ss.str();
            }
        }

    private:
        std::string name;
        bool print;
        std::vector<std::chrono::nanoseconds> values;
    };

    class Timer {
    public:
        Timer(const Timer &) = delete;

        Timer(std::string name) : name(name) {
            std::stringstream ss;
            ss << "[" << name << "] Start" << std::endl;
            std::cout << ss.str();
            start = std::chrono::high_resolution_clock::now();
        }
        Timer(MultiTimer &parent) : parent(&parent) {
            start = std::chrono::high_resolution_clock::now();
        }

        ~Timer() {
            auto end = std::chrono::high_resolution_clock::now();
            if (parent != nullptr) {
                parent->AddValue(end - start);
            } else if (!name.empty()) {
                std::stringstream ss;
                ss << "[" << name << "] End: " << ((end - start).count() / 1000000.0) << " ms" << std::endl;
                std::cout << ss.str();
            }
        }

        Timer &operator=(MultiTimer &newParent) {
            this->~Timer();
            this->name = "";
            this->parent = &newParent;
            start = std::chrono::high_resolution_clock::now();

            return *this;
        }

    private:
        std::string name;
        std::chrono::high_resolution_clock::time_point start;
        MultiTimer *parent = nullptr;
    };
} // namespace testing
