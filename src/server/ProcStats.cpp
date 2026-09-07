#include "server/ProcStats.hpp"

#include <cstdlib>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>

#include <dirent.h>
#include <unistd.h>

bool ProcStats::readCpuTicksAndThreads(
    unsigned long long& ticks,
    std::size_t& threads
) {
    std::ifstream file("/proc/self/stat");

    if(!file.is_open()) {
        return false;
    }

    const std::string content(
        (std::istreambuf_iterator<char>(file)),
        std::istreambuf_iterator<char>()
    );

    /*
     * Field 2 is the executable name in parentheses and may itself contain
     * spaces and parentheses, so the only safe anchor is the last ')'.
     * The first token after it is field 3, which makes field N token N-2.
     */
    const std::size_t close_paren = content.rfind(')');

    if(close_paren == std::string::npos) {
        return false;
    }

    std::istringstream stream(
        content.substr(close_paren + 1)
    );

    unsigned long long utime = 0;
    unsigned long long stime = 0;
    unsigned long long thread_count = 0;

    std::string token;

    for(int field = 3; field <= 20; ++field) {

        if(!(stream >> token)) {
            return false;
        }

        if(field == 14) {
            utime = std::strtoull(token.c_str(), nullptr, 10);
        } else if(field == 15) {
            stime = std::strtoull(token.c_str(), nullptr, 10);
        } else if(field == 20) {
            thread_count = std::strtoull(token.c_str(), nullptr, 10);
        }
    }

    ticks = utime + stime;
    threads = static_cast<std::size_t>(thread_count);

    return true;
}


std::size_t ProcStats::readRssBytes() {
    std::ifstream file("/proc/self/status");

    if(!file.is_open()) {
        return 0;
    }

    std::string line;

    while(std::getline(file, line)) {

        if(line.rfind("VmRSS:", 0) != 0) {
            continue;
        }

        std::istringstream stream(line.substr(6));

        unsigned long long kilobytes = 0;

        if(!(stream >> kilobytes)) {
            return 0;
        }

        return static_cast<std::size_t>(kilobytes) * 1024;
    }

    return 0;
}


std::size_t ProcStats::countOpenFds() {
    DIR* directory = opendir("/proc/self/fd");

    if(directory == nullptr) {
        return 0;
    }

    std::size_t count = 0;

    while(true) {

        const struct dirent* entry = readdir(directory);

        if(entry == nullptr) {
            break;
        }

        if(
            entry->d_name[0] == '.' &&
            (
                entry->d_name[1] == '\0' ||
                (entry->d_name[1] == '.' && entry->d_name[2] == '\0')
            )
        ) {
            continue;
        }

        ++count;
    }

    closedir(directory);

    /*
     * The descriptor opendir() itself holds is included in the listing and is
     * gone by the time the caller sees this number.
     */
    return count > 0 ? count - 1 : 0;
}


ProcStats::Sample ProcStats::sample() {
    std::lock_guard<std::mutex> lock(mutex);

    Sample result;

    result.rss_bytes = readRssBytes();
    result.open_fds = countOpenFds();

    unsigned long long ticks = 0;
    std::size_t threads = 0;

    if(!readCpuTicksAndThreads(ticks, threads)) {
        return result;
    }

    result.threads = threads;

    const auto now = std::chrono::steady_clock::now();

    if(has_previous) {

        const double elapsed_seconds =
            std::chrono::duration<double>(
                now - previous_time
            ).count();

        const long ticks_per_second = sysconf(_SC_CLK_TCK);

        if(
            elapsed_seconds > 0.0 &&
            ticks_per_second > 0 &&
            ticks >= previous_cpu_ticks
        ) {

            const double used_seconds =
                static_cast<double>(ticks - previous_cpu_ticks)
                / static_cast<double>(ticks_per_second);

            result.cpu_percent =
                (used_seconds / elapsed_seconds) * 100.0;
        }
    }

    previous_cpu_ticks = ticks;
    previous_time = now;
    has_previous = true;

    return result;
}
