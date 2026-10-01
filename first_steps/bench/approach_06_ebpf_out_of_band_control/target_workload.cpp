#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <chrono>
#include <thread>
#include <fstream>
#include <sys/mman.h>
#include <unistd.h>

long get_current_rss_kb() {
    std::ifstream status("/proc/self/status");
    std::string line;
    while (std::getline(status, line)) {
        if (line.rfind("VmRSS:", 0) == 0) {
            long val = 0;
            std::sscanf(line.c_str(), "VmRSS: %ld kB", &val);
            return val;
        }
    }
    return 0;
}

int main(int argc, char** argv) {
    bool await_reclaim = (argc > 1 && std::string(argv[1]) == "await_reclaim");
    size_t alloc_bytes = 32 * 1024 * 1024; // 32 MB

    // Allocate anonymous memory and touch it to force physical page frames
    void* mem = mmap(nullptr, alloc_bytes, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (mem == MAP_FAILED) return 1;

    std::memset(mem, 0x55, alloc_bytes);

    long initial_rss = get_current_rss_kb();
    pid_t my_pid = getpid();

    // Export PID and address for supervisor
    std::printf("TARGET_INFO: PID=%d ADDR=%p LEN=%zu INITIAL_RSS_KB=%ld\n",
                my_pid, mem, alloc_bytes, initial_rss);
    std::fflush(stdout);

    if (await_reclaim) {
        // Sleep to let supervisor execute process_madvise out-of-band
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    long final_rss = get_current_rss_kb();
    std::printf("FINAL_INFO: FINAL_RSS_KB=%ld\n", final_rss);

    munmap(mem, alloc_bytes);
    return 0;
}
