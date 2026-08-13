#pragma once
#include <mutex>

inline std::mutex& getLibRawMutex() {
    static std::mutex s_librawMutex;
    return s_librawMutex;
}
