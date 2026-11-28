# Pre-Registered Experiment Protocol: 2026-11-28_macbook_m2pro_stochastic_ski

- **Git SHA:** `91300b3231928a29423aaaeb4348b1ec19baa898`
- **Research Question:** Under a bimodal and heavy-tailed inter-burst gap schedule (30-1500 ms gaps), does the calibrated ski-rental break-even threshold tau* = 250 ms achieve an empirical competitive ratio <= 2.0 against an offline clairvoyant oracle while strictly beating tau=0 (explicit) and tau=inf (retain)?
- **Hypothesis:** pool_auto at tau* = 250 ms avoids refaults on sub-threshold gaps (<250 ms) while reclaiming long idle gaps (>=250 ms), achieving competitive ratio <= 2.0 and lower total ski-rental cost than both tau=0 and tau=inf with max_abs_error == 0.
- **Planned Trials:** 5 fresh processes per configuration in paired/shuffled order
- **Acceptance Gates:** 1) max_abs_error == 0 across all 40 runs; 2) empirical competitive ratio ALG(250 ms)/OPT <= 2.0; 3) ALG(250 ms) < ALG(tau=0) and ALG(250 ms) < ALG(tau=inf); 4) validate_run.py --require-protocol --min-trials 5 passes.

## Validation Before Promotion
1. Run `python3 scripts/research/validate_run.py first_steps/bench/utilization/results/2026-11-28_macbook_m2pro_stochastic_ski --require-protocol --min-trials 5`
2. Preserve negative/failed trials in this folder without rewriting `PROTOCOL.md`.
3. Request an independent audit (`.agents/skills/independent-audit/SKILL.md`) before adding to `context/EVIDENCE.json`.
