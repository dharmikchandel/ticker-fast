# Results

## Ring buffer throughput: memory order, alignment, cached indices

Benchmark: `mechanical_sympathy_bench` (Release build). One producer thread and one consumer
thread move 50M `MarketTick`s (64 bytes each) through a `RingBuffer<MarketTick, 4096>` as fast
as they can. The consumer checks every sequence number. Each of the 8 toggle combinations is run
5 times, interleaved, after one discarded warm-up pass. Numbers are millions of ticks per second.

Machine:

- CPU: 13th Gen Intel Core i5-1345U (hybrid: 2 P-cores with HT + 8 E-cores, 12 logical CPUs)
- Producer pinned to CPU 0, consumer to CPU 2 (two different P-cores)
- OS: Ubuntu, Linux 7.0.0-31-generic, native (not VM/WSL2)
- Compiler: GCC 15.2.0, `-O3`
- Laptop on AC power, `powersave` governor, frequency scaling left on

Session 1:

| acq/rel | aligned | cached | mean | best | worst |
|---|---|---|---|---|---|
| off | off | off | 12.44 | 12.47 | 12.39 |
| on | off | off | 17.96 | 18.60 | 15.71 |
| off | on | off | 10.29 | 10.59 | 9.21 |
| off | off | on | 10.12 | 10.43 | 9.02 |
| on | on | off | 17.54 | 17.77 | 16.75 |
| on | off | on | 17.40 | 17.52 | 17.31 |
| off | on | on | 9.97 | 10.02 | 9.94 |
| on | on | on | 19.11 | 19.56 | 17.56 |

Session 2 (same binary, a few minutes later):

| acq/rel | aligned | cached | mean | best | worst |
|---|---|---|---|---|---|
| off | off | off | 8.98 | 9.19 | 8.78 |
| on | off | off | 18.45 | 19.19 | 17.45 |
| off | on | off | 11.03 | 11.26 | 10.57 |
| off | off | on | 8.98 | 9.20 | 8.81 |
| on | on | off | 17.18 | 17.53 | 16.54 |
| on | off | on | 18.37 | 18.57 | 17.69 |
| off | on | on | 11.00 | 11.42 | 10.22 |
| on | on | on | 21.05 | 21.31 | 20.45 |

What holds up across both sessions:

- Acquire/release is the big win: roughly 1.5-2x over `seq_cst`. On x86 a `seq_cst` store
  compiles to `xchg` (a full barrier), while a release store is a plain `mov`.
- All three changes together is the fastest variant in both sessions (19.1 and 21.1 M/s mean).
- Alignment and cached indices on their own are within the noise here. With `seq_cst` the
  baseline itself moved from 12.4 to 9.0 M/s between sessions, which is bigger than any effect
  those two changes had. The full barrier on every store seems to dominate the cost, so the
  cache-line effects only start to show once it is gone.
- This is a flooding throughput test, not latency. The ring is mostly full, so both threads
  work on nearby slots constantly. Latency numbers come later with a fixed-rate publisher.
