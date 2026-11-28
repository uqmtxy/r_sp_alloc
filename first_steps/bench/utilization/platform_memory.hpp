#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <stdexcept>
#include <string>
#include <unistd.h>

#ifdef __APPLE__
#include <mach/mach.h>
#include <malloc/malloc.h>
#include <pthread/qos.h>
#else
#include <malloc.h>
#include <sched.h>
#endif

namespace utilization {

struct ProcessSnapshot {
    size_t rss = 0;
    size_t pss = 0;
    size_t private_bytes = 0;
    size_t hwm = 0;
};

inline ProcessSnapshot take_process_snapshot() {
    ProcessSnapshot s;
#ifdef __APPLE__
    task_vm_info_data_t vm{};
    mach_msg_type_number_t count = TASK_VM_INFO_COUNT;
    if (task_info(mach_task_self(), TASK_VM_INFO, reinterpret_cast<task_info_t>(&vm), &count) != KERN_SUCCESS)
        throw std::runtime_error("task_info(TASK_VM_INFO) failed");
    s.rss = static_cast<size_t>(vm.resident_size);
    s.private_bytes = static_cast<size_t>(vm.phys_footprint);
    s.pss = static_cast<size_t>(vm.phys_footprint);
    s.hwm = static_cast<size_t>(
        vm.ledger_phys_footprint_peak > 0 ? vm.ledger_phys_footprint_peak : vm.resident_size_peak);
#else
    std::ifstream file("/proc/self/smaps_rollup");
    std::string line;
    while (std::getline(file, line)) {
        size_t kb = 0;
        if (sscanf(line.c_str(), "Rss: %zu kB", &kb) == 1) s.rss = kb * 1024;
        if (sscanf(line.c_str(), "Pss: %zu kB", &kb) == 1) s.pss = kb * 1024;
        if (sscanf(line.c_str(), "Private_Clean: %zu kB", &kb) == 1 ||
            sscanf(line.c_str(), "Private_Dirty: %zu kB", &kb) == 1) s.private_bytes += kb * 1024;
    }
    std::ifstream status("/proc/self/status");
    while (std::getline(status, line)) {
        size_t kb = 0;
        if (sscanf(line.c_str(), "VmHWM: %zu kB", &kb) == 1) s.hwm = kb * 1024;
    }
    if (!s.rss || !s.hwm) throw std::runtime_error("cannot read /proc process residency");
#endif
    return s;
}

inline void trim_system_allocator() {
#ifdef __APPLE__
    malloc_zone_pressure_relief(nullptr, 0);
#else
    malloc_trim(0);
#endif
}

inline bool configure_thread_affinity_or_qos(int core) {
#ifdef __APPLE__
    (void)core;
    pthread_set_qos_class_self_np(QOS_CLASS_USER_INITIATED, 0);
    return false;  // macOS does not support user-space hard CPU pinning
#else
    cpu_set_t mask;
    CPU_ZERO(&mask);
    CPU_SET(core, &mask);
    if (sched_setaffinity(0, sizeof(mask), &mask) != 0)
        throw std::runtime_error("sched_setaffinity failed");
    return true;
#endif
}

inline size_t os_page_size() {
    return static_cast<size_t>(sysconf(_SC_PAGESIZE));
}

}  // namespace utilization
