# Workload-Specific Memory Allocation Lab

This directory is an isolated Docker-based Linux lab for allocator experiments.

It gives us a repeatable place to compare:

- glibc malloc
- jemalloc
- tcmalloc
- mimalloc
- later: custom pool, slab, arena, region, and trace-replay allocators

## Build

```bash
docker compose build
```

## Open the container

```bash
docker compose run --rm allocator-lab
```

## Run one allocator

Inside the container:

```bash
./scripts/run_one.sh glibc
./scripts/run_one.sh jemalloc
./scripts/run_one.sh tcmalloc
./scripts/run_one.sh mimalloc
```

## Run all allocators

```bash
./scripts/run_all.sh
```

Results are written under `results/`.

## Workload knobs

The runner accepts environment variables:

```bash
DURATION=120 THREADS=8 LIVE_OBJECTS=500000 MIN_SIZE=16 MAX_SIZE=8192 CHURN_BATCH=1024 ./scripts/run_all.sh
```

Current workload model:

- mixed allocation sizes
- long-running churn
- stable live object population
- per-thread allocation streams
- configurable object size and churn rate

This is phase 1 scaffolding. The next serious step is adding trace capture/replay and specialized allocator variants.
