# Final Research Completion Handoff — 2026-11-28

- **Host:** Apple M2 Pro (`Mac14,10`, Darwin `25.6.0`, `16 KiB` pages, `onnxruntime 1.32.0`, `jemalloc 5.3.1`, `clang++ 23.1.2`).
- **Completed Additions (Building on the earlier 91 M2 Pro runs -> 181 total validated runs, `max_abs_error == 0`):**
  1. **`pool_hybrid` & Multi-Allocator Back-to-Back Handoff (`2026-11-28_macbook_m2pro_multicam_handoff`, 50 runs):**
     - `gap=0 ms`: `pool_hybrid` `225.84 MiB` peak < `direct_jemalloc` `237.18 MiB` (`258,802` minor faults) < `direct_malloc` `268.10 MiB` < `pool_auto_0` `289.04 MiB` < `pool_retain` `415.10 MiB` < `bfc` `670.71 MiB`.
     - `gap=350 ms`: `pool_hybrid` decays to `164.51 MiB` final (`216.78 MiB` mean) vs `pool_coop` (`225.49 MiB` final, `223.29 MiB` mean).
  2. **Stochastic Ski-Rental Oracle (`2026-11-28_macbook_m2pro_stochastic_ski`, 40 runs):**
     - `pool_auto_250` ($\tau^*=250\text{ ms}$) achieves `1.5875x` empirical competitive ratio (`<= 2.00x` Theorem 2 bound) vs `pool_retain` (`2.7211x`); $\tau \in \{50, 150\}\text{ ms}$ achieve `1.1287x, 1.3739x` vs `pool_explicit` (`1.5218x`).
  3. **Theorems 1–5 (`context/THEORY_VERIFICATION.json`):** `all_passed == True`.
  4. **7 Figures, 8-Page Paper & Overleaf Bundle:** `first_steps/paper/latex/main.pdf` (8 pages), `first_steps/paper/overleaf_paper_bundle.zip` (standalone Overleaf build verified), `first_steps/paper/MANUSCRIPT.md` synced.
- **Pending Future Work:** Bare-metal CloudLab `4 KiB` replication of `auto_idle_bench`, `multicam_staggered_bench`, and `stochastic_ski_bench`.
