# Pre-Registered Experiment Protocol: 2026-11-28_macbook_m2pro_multicam

- **Git SHA:** `91300b3231928a29423aaaeb4348b1ec19baa898`
- **Research Question:** Can cooperative global-budget arbitration across staggered multi-camera YOLO26n streams (N=4 K=1 and N=8 K=2, 25% duty cycle) reduce mean and peak phys_footprint proportionally to active duty cycle on macOS 16 KiB pages while preserving p99 latency and bit-exact FP32 outputs?
- **Hypothesis:** Shedding oldest-idle streams synchronously at burst handoff via GlobalBudgetArbiter reduces mean phys_footprint by >=40% vs BFC/retain, eliminates the idle-timer lag of per-stream auto-idle reclamation, and keeps p99 latency within 10% of retain with max_abs_error == 0.
- **Planned Trials:** 5 fresh processes per configuration in paired/shuffled order
- **Acceptance Gates:** 1) max_abs_error == 0 across all 45 runs; 2) pool_coop mean phys_footprint <= 0.60x pool_retain and bfc for N=4 K=1 and N=8 K=2; 3) pool_coop p99 latency <= 1.10x pool_retain; 4) validate_run.py --require-protocol --min-trials 5 passes.

## Validation Before Promotion
1. Run `python3 scripts/research/validate_run.py first_steps/bench/utilization/results/2026-11-28_macbook_m2pro_multicam --require-protocol --min-trials 5`
2. Preserve negative/failed trials in this folder without rewriting `PROTOCOL.md`.
3. Request an independent audit (`.agents/skills/independent-audit/SKILL.md`) before adding to `context/EVIDENCE.json`.
