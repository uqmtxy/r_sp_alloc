# Activity-aware reclamation: screening and repeated burst tests

Protocol: five fresh processes per finalist, 120 frames, ten warmup frames, four 1-second pauses after 30-frame bursts. Full and retained outputs verified exactly. Timing excludes verification and proc sampling. Reclamation during pauses is excluded from active FPS; resume faults are included. Byte-seconds are endpoint-sampled estimates, not integrated physical residency.

| Policy | Median FPS | Median p99 ms | Median idle private MiB | Estimated private byte-seconds |
|---|---:|---:|---:|---:|
| bfc | 6.405 | 268.918 | 118.703 | 2829520000.000 |
| pool_idle | 6.308 | 298.870 | 34.344 | 1597120000.000 |

Status: promising screening evidence; latency acceptance not established. Four-camera staggered activity, endurance, actual pressure, bounded retention and delayed automatic reclamation remain untested. Current implementation reclaims at an explicit scheduled pause; it is not an idle detector. P99 from short runs is exploratory. Do not claim a production throughput or universal architecture advantage.
