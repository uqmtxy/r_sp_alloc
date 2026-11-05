# Screening failures and rejections

Raw stderr/stdout in results/screen preserves the exploratory failures.

- The initial `legacy_pare` wrapper variant crashed under the new full-output
  validation workload. Its old frame-reset design is not accepted for this goal.
  The wrapper failure alone does not diagnose every use of the historical binary.
  Historical constant-input endpoint-touch results remain preserved separately.
- The initial `bfc_exact` experimental environment-arena configuration crashed
  with an ORT `RegionFor` error. Retaining the environment-owned arena did not
  resolve it. This experiment is rejected on this ORT build; it is not a supported
  mode of the final harness and contributes no successful utilization evidence.
- A one-buffer unconditional frame reset cannot satisfy the held-output contract.
  It is excluded; the implemented pool reuses memory only following actual Free.
- Page alignment for every object increased measured resident padding relative
  to 64-byte packing and is not selected.
- Initialization-only trimming did not eliminate recurring steady-state free
  resident pages. Boundary/eager reclamation are tested separately.

The final supported harness excludes the two crashing exploratory modes.
All accepted results are rerun using the final source and binary, with hashes.
