#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <chrono>
#include <thread>
#include <vector>
#include <sys/syscall.h>
#include <sys/uio.h>
#include <sys/mman.h>
#include <unistd.h>

#ifndef SYS_pidfd_open
#if defined(__x86_64__)
#define SYS_pidfd_open 434
#elif defined(__aarch64__)
#define SYS_pidfd_open 434
#endif
#endif

#ifndef SYS_process_madvise
#if defined(__x86_64__)
#define SYS_process_madvise 440
#elif defined(__aarch64__)
#define SYS_process_madvise 440
#endif
#endif

int main(int argc, char** argv) {
    if (argc < 4) {
        std::fprintf(stderr, "Usage: %s <target_pid> <hex_addr> <length_bytes>\n", argv[0]);
        return 1;
    }

    pid_t target_pid = static_cast<pid_t>(std::atoi(argv[1]));
    uintptr_t target_addr = std::strtoull(argv[2], nullptr, 16);
    size_t length = std::strtoull(argv[3], nullptr, 10);

    // Open pidfd for target process
    int pidfd = syscall(SYS_pidfd_open, target_pid, 0);
    if (pidfd < 0) {
        std::perror("pidfd_open failed");
        return 1;
    }

    struct iovec vec;
    vec.iov_base = reinterpret_cast<void*>(target_addr);
    vec.iov_len = length;

#ifndef MADV_PAGEOUT
#define MADV_PAGEOUT 21
#endif

#ifndef MADV_COLD
#define MADV_COLD 20
#endif

    // Execute out-of-band proactive memory reclamation via process_madvise
    auto t0 = std::chrono::high_resolution_clock::now();
    // Use MADV_PAGEOUT to immediately reclaim anonymous physical page frames
    ssize_t ret = syscall(SYS_process_madvise, pidfd, &vec, 1, MADV_PAGEOUT, 0);
    if (ret < 0 && errno == EINVAL) {
        // Fallback to MADV_COLD
        ret = syscall(SYS_process_madvise, pidfd, &vec, 1, MADV_COLD, 0);
    }
    auto t1 = std::chrono::high_resolution_clock::now();

    close(pidfd);

    if (ret < 0) {
        std::perror("process_madvise failed");
        return 1;
    }

    double elapsed_us = std::chrono::duration<double, std::micro>(t1 - t0).count();
    std::printf("{\"status\": \"success\", \"pid\": %d, \"bytes_reclaimed\": %zu, \"elapsed_us\": %.2f}\n",
                target_pid, static_cast<size_t>(ret), elapsed_us);

    return 0;
}
