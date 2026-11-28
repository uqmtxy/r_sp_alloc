# Pre-Registered Experiment Protocol: 2026-11-28_macbook_m2pro_auto_idle

- **Git SHA:** `91300b3231928a29423aaaeb4348b1ec19baa898`
- **Research Question:** Can an autonomous inter-burst idle detector match explicit pause-boundary bounded retention without pause callbacks on Apple M2 Pro (16 KiB pages), while avoiding false triggers during continuous streams and short 80 ms jitter?
- **Hypothesis:** IdleReclamationController (idle_threshold_ms=250) with a screened resident free-page budget will fire 0 times during continuous runs and 80 ms jitter, reduce idle physical footprint by >=45% vs BFC during >=1 s idle pauses, keep continuous FPS >=0.97x BFC, and keep burst/mixed median p99 <=1.05x BFC with max_abs_error == 0.
- **Planned Trials:** 5 fresh processes per configuration in paired/shuffled order
- **Acceptance Gates:** max_abs_error == 0; idle_private_bytes <= 0.50 * BFC; continuous_fps >= 0.97 * BFC; burst_p99 <= 1.05 * BFC

## Validation Before Promotion
1. Run `python3 scripts/research/validate_run.py first_steps/bench/utilization/results/2026-11-28_macbook_m2pro_auto_idle --require-protocol --min-trials 5`
2. Preserve negative/failed trials in this folder without rewriting `PROTOCOL.md`.
3. Request an independent audit (`.agents/skills/independent-audit/SKILL.md`) before adding to `context/EVIDENCE.json`.
