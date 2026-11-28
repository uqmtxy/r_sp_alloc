# Bounded-Retention Page-Grid Arenas and Cooperative Multi-Stream Arbitration for Dedicated Neural Inference

**Author:** Erdeni Khatunov (Student ID `126033990029`, Group `GE6001-03300-M03`, Shanghai Jiao Tong University)  
**LaTeX Source & Compiled PDF:** [main.tex](latex/main.tex), [main.pdf](latex/main.pdf)  
**Overleaf Bundle:** [overleaf_paper_bundle.zip](overleaf_paper_bundle.zip)  
**Theoretical Verification:** [THEORY_VERIFICATION.json](../../context/THEORY_VERIFICATION.json) (`all_passed = true`, Theorems 1–5)

---

## Abstract

Dedicated deep-learning inference pipelines such as ONNX Runtime executing YOLO26n cache large intermediate tensor arenas across inference bursts. While caching maximizes intra-burst throughput, unreturned free pages inflate process physical memory (`RSS` / `phys_footprint`) by 2–3x during idle gaps and across staggered multi-camera deployments. Conversely, naive per-frame OS unbacking (`MADV_DONTNEED` or allocator trimming) introduces thousands of minor page faults per burst, and on macOS Apple Silicon (`16 KiB` pages), POSIX `MADV_DONTNEED` and `MADV_FREE` are kernel no-ops that leave physical footprint unchanged.

We present a cross-OS page-grid arena architecture combining:
1. **`PageReuseResource`**: Exact-boundary allocator tracking `live`, `head`, `body`, `tail`, and `sub-page` residency across `4 KiB` Linux (`MADV_DONTNEED`) and `16 KiB` macOS XNU (`mmap(MAP_FIXED)`).
2. **`IdleReclamationController`**: 2-competitive online idle-threshold watchdog with frame-boundary instrumentation (`mark_frame_start`/`mark_frame_end`).
3. **`GlobalBudgetArbiter` & `pool_hybrid`**: Cooperative multi-stream arbiter shedding oldest-idle streams synchronously at burst handoff, unified with per-stream online idle timers (`pool_hybrid`).

---

## Formal Theorems (All Verified in `context/THEORY_VERIFICATION.json`)

1. **Theorem 1 (Page-Residency Conservation & Boundary Waste):** $R = R_{\text{live}} + R_{\text{head}} + R_{\text{tail}} + R_{\text{body}} + R_{\text{sub}}$, and `purge_to_budget(B)` preserves $R_{\text{live}}$, enforces $R_{\text{body}} \le B$, and leaves $< 2mP$ unpurgeable boundary bytes. Verified via `resource_test` (`16 KiB` PASS) and M2 Pro `screen_0` vs `screen_-1` (`106.02 -> 42.31 MiB`, `63.70 MiB` reclaimed).
2. **Theorem 2 (2-Competitive Online Idle Reclamation):** Ski-rental break-even threshold $\tau^*$ achieves competitive ratio $\le 2$ with $0$ false reclaims when $t_{\text{frame}} < \tau^*$. Verified across 15 confirmed M2 Pro `pool_auto` trials (`0` false reclaims) AND 40 stochastic ski-rental runs (`max_abs_error == 0`: $\tau^*=250\text{ ms}$ achieves `1.59x` $\le 2.00\times$ vs `2.72x` retain; $\tau \in \{50, 150\}\text{ ms}$ achieve `1.13x, 1.37x` vs `1.52x` explicit).
3. **Theorem 3 (Multi-Stream Staggered Duty-Cycle Scaling):** $R_{\text{coop}}(N, K) \approx R_{\text{base}}(N) + K \cdot M_{\text{arena}}$. Verified across 45 M2 Pro runs (`max_abs_error == 0`): `N=4, K=1` predicted `126.74 MiB` vs measured `126.10 MiB` (`0.51%` error); `N=8, K=2` predicted `225.57 MiB` vs measured `224.75 MiB` (`0.36%` error).
4. **Theorem 4 (Linear Fault–Budget Tradeoff Law):** $F(B) = F_0 + W (M_{\text{arena}} - B)^+ / P$. Verified via OLS regression on CloudLab `4 KiB` (`-2816.7` faults/MiB, $R^2 = 0.9999998$) and Apple M2 Pro `16 KiB` (`-759.2` faults/MiB, $R^2 = 0.999875$).
5. **Theorem 5 (Hybrid Cooperative + Online Idle Dominance):** `pool_hybrid` strictly eliminates `pool_auto_0`'s overlap peak under `gap=0 ms` (`225.84` vs `289.04 MiB` peak) AND reclaims quiescent waves under `gap=350 ms` (`216.78` vs `223.29 MiB` mean; `164.51` vs `225.49 MiB` final).

---

## Key Experimental Results (Strictly Platform-Separated)

### Platform A: CloudLab `m510` Bare-Metal Linux (`4 KiB` pages)
- **Screening boundary:** On single-stream continuous inference, `PageReuseResource` achieves `99.9903%` allocator-local utilization, while `direct_trim` (`73.39 MiB`) edges out `pool_eager_trim` (`73.74 MiB`) on active peak RSS.
- **`24 MiB` Bounded Pool (`bounded/REPORT.md`):** Cuts burst idle private RSS to `0.4916x` and byte-seconds to `0.5604x` at `1.0108x` median burst `p99` latency; in 1,000-frame endurance (100 scheduled `1 s` pauses), cuts idle private RSS from `118.79 MiB` to `58.37 MiB` (`-50.9%`) at `5.751` vs `5.796 FPS`.

### Platform B: Apple M2 Pro macOS (`16 KiB` pages, 181 Validated Runs, `max_abs_error == 0`)
- **XNU VM Probe:** `MADV_DONTNEED` and `MADV_FREE` are no-ops (`32 -> 32 MiB`); `MADV_FREE_REUSABLE` leaves `mincore()` set (`32 MiB`); `mmap(MAP_FIXED)` drops both `mincore()` and `phys_footprint` (`32 -> 16 MiB`) with live neighbor intact.
- **Single-Stream `auto_idle_bench` (46 runs):** Drops idle `phys_footprint` from `105.75–106.02 MiB` (`BFC`) to `42.25–42.31 MiB` (`-60.0%`) with `0` false reclaims and `1.013x` burst `p99`.
- **Staggered Multi-Camera `multicam_staggered_bench` (45 runs):** `N=4, K=1` (`bfc` `332.17`, `pool_retain` `221.39`, `pool_auto_0` `122.78`, `pool_coop` `126.10 MiB`); `N=8, K=2` (`bfc` `635.77`, `pool_retain` `418.24`, `pool_auto_0` `216.50`, `pool_coop` `224.75 MiB`).
- **Back-to-Back Handoff & Multi-Allocator Baselines (50 runs, `N=8, K=2, --shed-warmup`):**
  - `gap=0 ms` peak `phys_footprint`: `pool_hybrid` `225.84 MiB` < `direct_jemalloc` `237.18 MiB` (`258,802` minor faults!) < `direct_malloc` `268.10 MiB` < `pool_auto_0` `289.04 MiB` < `pool_retain` `415.10 MiB` < `bfc` `670.71 MiB`. `pool_hybrid` incurs `8.0x` fewer faults (`32,412`) than `direct_jemalloc`.
  - `gap=350 ms`: `pool_hybrid` decays to `164.51 MiB` final vs `225.49 MiB` (`pool_coop`).
- **Stochastic Ski-Rental Oracle (40 runs):** `pool_auto_250` achieves `1.59x` empirical competitive ratio (`<= 2.00x` bound, `63.35 MiB` mean), while `500 ms` (`2.04x`), `1000 ms` (`2.59x`), `pool_retain` (`2.72x`), and `bfc` (`2.72x`) all exceed `2.00x`.

---

## Threats to Validity & Pending Work
- Bare-metal CloudLab `4 KiB` replication of `auto_idle_bench`, `multicam_staggered_bench`, and `stochastic_ski_bench` remains pending future work.
- All 7 figures (`first_steps/paper/figures/fig1`–`fig7`) are generated directly from verified JSONs by [generate_paper_figures.py](figures/generate_paper_figures.py).
