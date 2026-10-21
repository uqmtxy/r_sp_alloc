#include <cstdint>
#include <cstdio>
#include <cstdlib>
struct alignas(64) Node { std::uint64_t next; };
int main() {
    constexpr std::uint64_t count = 1ULL << 21;
    auto* nodes = static_cast<Node*>(std::aligned_alloc(64, count * sizeof(Node)));
    if (!nodes) return 1;
    for (std::uint64_t i = 0; i < count; ++i) nodes[i].next = (i + 1048583) & (count - 1);
    std::uint64_t cursor = 0;
    for (std::uint64_t i = 0; i < 8 * count; ++i) cursor = nodes[cursor].next;
    std::printf("128 MiB dependent load traversal: final index %llu\n", static_cast<unsigned long long>(cursor));
    std::free(nodes);
}
