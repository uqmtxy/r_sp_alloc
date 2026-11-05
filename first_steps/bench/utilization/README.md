# Utilization experiments

Sole optimization objective: less physical resident RAM for the same correct FP32
YOLO26n inference. Start with the [recorded plan](../../utilization_improvement_plan.md).
The existing 5be613f rebench is preserved.

```bash
bash first_steps/bench/utilization/build.sh
python3 -u first_steps/bench/utilization/run_validation.py
python3 first_steps/bench/utilization/analyze_validation.py
```

The validation runner executes configurations sequentially. It stops on an
accepted-run failure, checks complete output tensors including outputs retained
across later frames, and records commands and hashes. It can resume compatible
completed runs. Clear the validated directory into a separate archive before
changing the source/binary or protocol and running a new experiment.

Supported modes: `bfc`, `direct`, `pool_retain`, `pool_initial_trim`,
`pool_boundary`, `pool_eager`, `pool_page`. `--trim 1` calls glibc `malloc_trim(0)`
after outputs are released. `--cameras 4` samples before session and thread
teardown. `--pattern 0` is a separate diagnostic regime; the validation runner
keeps patterns enabled for every accepted configuration.

Allocator-local utilization measures outstanding allocator-request bytes backed
by resident pages. It does not measure tensor last-use liveness hidden within
ORT memory-pattern buffers. Whole-process RSS and private resident bytes are
reported alongside it. Free-hole residency comes from mincore, not a constant.

The page reuse resource packs objects to 64 bytes, tracks actual free calls,
merges adjacent holes, and advises only wholly free page interiors. Metadata is
outside the advised mapping. The page-alignment mode is a comparison, not the
preferred design. Advice failures and allocator contract errors fail the run.

Run the focused memory-safety/reclamation test:

```bash
g++ -O1 -g -std=c++17 -fsanitize=address,undefined \
  first_steps/bench/utilization/resource_test.cpp -o /tmp/utilization_resource_test
/tmp/utilization_resource_test
```

The original pipeline now also accepts `--trim`:

```bash
first_steps/bench/yolo26n_pipeline/yolo26_bench \
  --model first_steps/bench/yolo26n_pipeline/models/yolo26n.onnx \
  --mode direct_tensor --trim --frames 1000 --warmup 50 --core 2 --json
```

Trimming is optional and may trade CPU time and repeated page faults for lower
memory consumption. The full-output validation harness is the correctness
reference; the older pipeline's first-output touch remains a lighter telemetry
check. See [rejected approaches](REJECTED_APPROACHES.md) for preserved failures.
