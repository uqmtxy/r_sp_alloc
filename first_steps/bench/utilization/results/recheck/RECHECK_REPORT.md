# Independent fresh-process rerun

46 runs completed; all full and retained outputs bit-exact. New randomized order, five fresh processes per ordinary single/camera configuration; two 1000-frame endurance runs; nine synthetic stress runs. Same harness: repeatability evidence, not independent implementation validation.

| Regime / policy | Active peak RSS median [min,max] MiB | Live idle private median MiB | Diagnostic FPS median |
|---|---:|---:|---:|
| single_bfc | 122.77 [122.67, 123.16] | 118.96 | 6.62 |
| single_bfc_trim | 113.45 [113.42, 113.84] | 109.65 | 6.62 |
| single_direct_trim | 73.27 [73.12, 73.39] | 34.20 | 5.88 |
| single_pool_eager_trim | 73.62 [73.61, 73.74] | 34.42 | 5.90 |
| cameras4_bfc_trim | 394.04 [393.67, 394.89] | 390.16 | 21.19 |
| cameras4_direct_trim | 236.27 [228.76, 236.59] | 90.42 | 19.06 |
| cameras4_pool_eager_trim | 233.85 [222.69, 234.04] | 87.95 | 19.29 |

## Evaluation

RAM savings reproduce on this Xeon D-1548/4 KiB-page fixed-shape YOLO workload. Direct allocation plus glibc trimming is the simplest validated option; the custom pool has no established decisive single-stream advantage over that tuned baseline. Four-camera savings persist while sessions remain alive.

Timing includes output verification and memory telemetry. Diagnostic FPS cannot establish production throughput or p99; no new cache-counter, energy, syscall, or page-fault measurements were collected. Modern allocator mechanisms are relevant, but this Broadwell-DE machine cannot validate generalization to current-generation CPUs, NUMA systems, huge-page configurations, dynamic shapes, or other models.

Selection is deliberately limited to previously promising candidates plus default and trimmed BFC controls. This confirmatory rerun does not estimate every allocator or eliminate shared-harness bias. Synthetic fragmentation tests have one run per setting and are an adaptation, not exact ISMM reproduction. RSS includes shared mappings; private bytes and pool-local residency have different denominators.

Useful for memory-constrained long-lived inference services. Deployment should depend on acceptable latency/throughput under the target workload. No universal custom-allocator superiority or cache-locality improvement is supported.
