# Pre-Registered Experiment Protocol: 2026-11-28_macbook_m2pro_multicam_handoff

- **Git SHA:** `91300b3231928a29423aaaeb4348b1ec19baa898`
- **Research Question:** Under back-to-back multi-camera handoff (N=8, K=2, burst_gap_ms=0) vs sparse bursts (burst_gap_ms=350), does pool_hybrid dominate both pool_auto_0 (eliminating the overlap peak under gap=0) and pool_coop (reclaiming idle streams under gap=350), and how do both compare against BFC, direct_malloc, and direct_jemalloc?
- **Hypothesis:** pool_hybrid achieves peak phys_footprint <= pool_coop < pool_auto_0 < pool_retain < bfc under back-to-back handoff (gap=0) while achieving mean/final phys_footprint < pool_coop under sparse bursts (gap=350), with fewer minor faults than direct_jemalloc and max_abs_error == 0.
- **Planned Trials:** 5 fresh processes per configuration in paired/shuffled order
- **Acceptance Gates:** 1) max_abs_error == 0 across all 50 runs; 2) gap=0 peak phys_footprint(pool_hybrid) < pool_auto_0 < pool_retain < bfc; 3) gap=350 mean/final phys_footprint(pool_hybrid) < pool_coop; 4) validate_run.py --require-protocol --min-trials 5 passes.

## Validation Before Promotion
1. Run `python3 scripts/research/validate_run.py first_steps/bench/utilization/results/2026-11-28_macbook_m2pro_multicam_handoff --require-protocol --min-trials 5`
2. Preserve negative/failed trials in this folder without rewriting `PROTOCOL.md`.
3. Request an independent audit (`.agents/skills/independent-audit/SKILL.md`) before adding to `context/EVIDENCE.json`.
