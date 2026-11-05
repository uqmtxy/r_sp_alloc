# One objective: improve physical RAM utilization

Status: completed successfully on the supplied fixed-shape YOLO26n workload. Historical rebench at commit 5be613f is preserved. See [validated results](bench/utilization/results/UTILIZATION_REPORT.md).

## Objective and acceptance rule (recorded before new experiments)

Execute the same FP32 YOLO26n model, input shapes, graph optimizations, thread
count, and output requirements using less resident physical memory. FPS, cache
misses, zero faults, and zero syscalls are diagnostics, not optimization goals.
No quantization, smaller inputs, fewer cameras, swapping, or changed predictions.

Success requires all of the following:

1. Complete output tensors match a conventional ORT reference for distinct inputs,
   including output tensors intentionally kept alive across subsequent frames.
2. At least 25% less resident RAM than the previous PARE-TA implementation while
   sessions and cameras remain alive, reproducible in five fresh process runs.
3. Also compare with default BFC and direct glibc/jemalloc/mimalloc. Report any
   disadvantage relative to the strongest conventional baseline explicitly.
   A claim of beating conventional allocators requires a separate >=10% gain
   against the best measured baseline in the claimed operating phase.
4. Increased measured allocator-payload utilization or reduced measured resident
   free bytes under equivalent allocation demand. Actual page residency is read
   with mincore; virtual reservation is never reported as physical consumption.
5. Report startup peak, active inference footprint, idle footprint before teardown,
   and teardown separately. A teardown-only improvement does not pass.

Allocation-payload utilization = resident bytes belonging to currently outstanding
allocator requests / resident bytes in allocator-owned mappings. It is not a
measurement of last-use tensor liveness inside ORT's memory-pattern blocks.
Process-wide private resident bytes and RSS must corroborate allocator-local data;
allocator metadata and measurement overhead count in the process footprint.

## Paper reread and corrections

Primary texts retrieved and read on 2026-11-05:

- Pasha Fistanto, Sören Tempel, Christian Dietrich, PLOS 2026:
  https://www.ibr.cs.tu-bs.de/vss/Publications/2026/fistanto_26_lost_bytes.pdf
  DOI 10.1145/3831586.3838144.
- Nicolas van Kempen, Emery D. Berger, ISMM 2026:
  https://arxiv.org/html/2605.17119v1
  DOI 10.1145/3814942.3816132.

PLOS sections 3–5 classify free holes and intersect them with resident pages.
Adjacent free ranges are merged into H_min to expose returnable whole-page bodies.
Body bytes still resident are waste too. Page-aligned allocation alone proves
neither low waste nor reclamation. The paper's 75% example concerns accumulated
hole size, not a universal 75% reduction of application RSS. Its design is future
work; our implementation needs its own evidence.

ISMM section 2.2.2 explicitly identifies deferred region deallocation as a source
of increased memory consumption. Section 3 profiles allocation sizes and peak live
object count before adversarial preconditioning. Table 3 uses occupancies 0.33,
0.66, and 0.80 for Adv1, Adv3, and Adv10. The previous suite's fixed 0.66 occupancy,
hardcoded N_peak=20000, and broad allocation-size distribution are adaptations,
not an exact reproduction. Section 5 limits its evaluation to single-threaded
applications. Cache-locality benefits do not establish improved memory utilization.

The existing bibliography misattributes both papers. Correct the two entries;
do not silently relabel historical measurements as paper-faithful reproductions.

## Execution order and alternative approaches

| Order | Approach | Memory waste addressed | Evidence needed |
|---|---|---|---|
| 0 | Instrument true Alloc/Free lifetimes and page residency | Hardcoded zeros and misleading teardown samples | Live-request ledger, resident free-hole classification, process snapshots |
| 1 | One buffer for synchronous inference; compact internal alignment | Unnecessary double buffering and padding | Full-output equivalence, retained-output safety; reject unsafe reset |
| 2 | Individual free, best-fit reuse, eager adjacent-hole coalescing | Holding dead allocations until a frame boundary | Same ORT requests serviced with less resident backing |
| 3 | Return only completely free page bodies with MADV_DONTNEED | Physically retained free pages | mincore before/after; no live or metadata page may be discarded |
| 4 | Separate initialization, persistent data, and reusable transient storage | Initialization high-water marks and lifetime mixing | Before/after-load and between-frame measurements |
| 5 | Tune ORT BFC growth and shrinkage as a competing approach | Geometric arena growth and stale chunks | Identical model/output requirements and live-session RSS |
| 6 | Shared model/session resources for concurrent cameras if needed | Duplicated immutable state | Full output verification per camera and PSS/private accounting |

First implement a separate utilization harness so historical runs stay intact.
Pool mappings have page-aligned boundaries; objects require only the alignment
actually needed by ORT (64 bytes). Free-range metadata lives outside purgeable
pages. Coalesce neighbors before finding whole-page interiors. Never reset a
mapping while ORT or a caller retains a pointer. No unverified frame reset is
eligible for the final implementation.

## Experimental protocol

1. Small deterministic allocation tests: mixed sizes, both-side coalescing,
   partial-page live neighbors, full-body reclamation, retained outputs, and bounds.
2. Short real-model screening across alternatives, with memory patterns held
   constant. Run a second, explicitly labeled memory-pattern-disabled diagnostic
   if needed to expose individual tensor lifetimes; do not mix these regimes.
3. Five fresh-process repetitions for finalists and conventional baselines in
   alternating order. Preserve every failure and rejected approach.
4. One 1000-frame endurance pass for the chosen implementation and its comparator.
   Check complete outputs and bounded memory growth across the run.
5. Four concurrent streams, Adv0 and stress conditions, sampled with sessions alive.
   Clearly label any synthetic-preconditioning adaptation. Record useful anchor
   bytes separately from allocator overhead; do not count anchors as fragmentation.
6. Report measured byte categories, allocator coverage limits, repeated-run ranges,
   model/binary/source hashes, and exact commands. Preserve the previous results.

## Decision discipline

Keep a candidate only when correct inference and measured physical utilization
improve. A faster allocator with a larger resident footprint is rejected for this
goal. Repeated faults or extra reclamation calls are acceptable tradeoffs and are
reported. If the strongest baseline wins a phase, state that result and continue
with the next memory-waste mechanism rather than redefining success.

## Executed outcome

All 71 accepted runs passed exact full-output comparisons, including retained
outputs: 45 repeated single-stream runs, two 1000-frame endurance runs, 15 repeated
four-camera runs, and nine explicitly adapted synthetic stress runs. Sanitized
resource tests verified reclamation, accounting conservation, and live-neighbor
preservation. Native `--trim` smoke tests and invalid-input failure checks passed.

Median active peak RSS: default BFC 122.86 MiB, direct glibc plus trimming 73.39 MiB,
custom eager pool plus trimming 73.74 MiB. The custom pool has a lower startup peak
than direct glibc trimming; neither policy dominates every operating phase.
Four live camera sessions: trimmed BFC 394.66 MiB peak versus the custom pool's
234.09 MiB; live-session idle private memory 390.80 versus 88.05 MiB.

Measured idle pool payload utilization rose from 20.7955% to 99.9903%; resident
free-body bytes fell from 37,343,232 to zero. Total measured resident waste in the
owned mapping is 952 bytes. These are allocator-local observations, not a claim
that 99.99% of all process or system RAM holds useful tensors.

The utilization goal is achieved for this workload. Custom-allocator superiority
against the strongest tuned conventional baseline remains unproven. Raw records,
failures, source/model/binary/reference hashes, and commands are preserved.
