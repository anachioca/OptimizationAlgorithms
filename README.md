# Rectangle Packing Optimization

A solver for the **rectangle bin-packing problem**: given N rectangles with integer side lengths and an integer box side length L, place all rectangles into as few L × L square **boxes** as possible. Rectangles may be rotated 90°, must be axis-aligned, and may share only edges or corners with each other (interiors disjoint). Implements **Greedy** and **Local Search** as required by the course assignment.

## Quick start

```bash
make
./bin/test-env            # quick test (a few seconds)
./bin/test-env --full     # full test suite (writes full_test_log.csv)
./bin/sfml-gui            # interactive GUI
```

Dependencies: `g++` with C++17, `libsfml-dev`.

## How the project meets the specification

| Requirement | Where it lives |
|---|---|
| Generic algorithm implementation (problem-agnostic) | `include/Framework.hpp` — templated `LocalSearch<S>`, `GreedyAlgorithm<E, S>`, `Neighborhood<S>`, `SelectionStrategy<E>` |
| Local Search starts from decidedly bad solutions | See *Bad starts* below — one tailored to each neighborhood |
| Two fundamentally different greedy selection strategies | `AreaStrategy`, `MaxSideStrategy` in `GreedyStrategies.hpp` |
| Geometry-based neighborhood | `GeometryNeighborhood.hpp` |
| Rule-based (permutation) neighborhood | `PermutationNeighborhood.hpp` |
| Overlap-allowing cooling neighborhood (overlap-free at end) | `OverlapNeighborhood.hpp` + post-processing in callers |
| Instance generator (uniform, independent, parameterized) | `InstanceGenerator.hpp` |
| GUI with parameterized random instances + repeatable algorithm runs | `src/gui_main.cpp` + `GUI.hpp` + `Visualizer.hpp` |
| Step-skipping visualization | per-frame logic in `gui_main.cpp` |
| Colorblind-aware palette | Wong palette in `Visualizer.hpp` |
| Test environment (quick + full modes) | `src/test_env.cpp` |
| CPU thread time logging | `clock_gettime(CLOCK_THREAD_CPUTIME_ID, …)` in `test_env.cpp` |
| Up to 1000 rectangles in ~10 s | See *Performance* below |

## File layout

```
include/
  Framework.hpp              Generic templates: Solution, Neighborhood,
                             Algorithm, SelectionStrategy, LocalSearch,
                             GreedyAlgorithm. Knows nothing problem-specific.
  RectanglePacking.hpp       Rectangle, Box, PackingSolution. Problem geometry
                             and placement primitive. Knows nothing about
                             which algorithm uses it.
  InstanceGenerator.hpp      Random rectangle-set generator.
  GreedyStrategies.hpp       Selection strategies + placement functions.
  GeometryNeighborhood.hpp   Neighborhood: move rectangles between boxes.
  PermutationNeighborhood.hpp Neighborhood: 2-swap and 1-insert over an
                             ordering, materialised into a packing.
  OverlapNeighborhood.hpp    Neighborhood: overlap-tolerant cooling.
  GUI.hpp                    Reusable SFML widgets (Button, NumericInput).
  Visualizer.hpp             Draws a PackingSolution.

src/
  gui_main.cpp               SFML GUI entry point.
  test_env.cpp               Batch test environment.

Makefile                     `make` builds bin/test-env and bin/sfml-gui.
```

## The generic framework (`Framework.hpp`)

The algorithm implementations are templated and contain **no** problem-specific code — no rectangles, no boxes, no overlap, no fill ratios. They operate through the following abstract interfaces:

- **`Solution`** — one optimisation problem solution. Methods: `objectiveValue() → double` (the value to minimise), `clone() → unique_ptr<Solution>`.
- **`Neighborhood<S>`** — defines the move set for local search. Single method: `findBetterNeighbor(current) → unique_ptr<S> or nullptr`. The neighborhood owns the notion of "better": it may compute candidates against a relaxed or heuristic score that differs from `objectiveValue()`. The spec explicitly permits this for local search.
- **`SelectionStrategy<E>`** — orders the input elements for greedy. Single method: `sort(elements)` in place.
- **`LocalSearch<S>`** — generic local search. Holds a current solution and a neighborhood; `performStep()` asks the neighborhood for a candidate and adopts whatever it returns; `solve()` loops until `findBetterNeighbor` returns `nullptr`. Supports an optional `setStepCallback(...)` hook used by overlap cooling to update neighborhood parameters between steps.
- **`GreedyAlgorithm<E, S>`** — generic greedy. Holds an element list, a selection strategy, an empty starting solution, and a `PlacementFunc` function pointer `void(S&, E)`. `solve()` sorts via the strategy and applies the placement function to each element.

All problem-side state (rectangles, overlap tolerances, density bonuses, placement geometry) is kept outside this file behind the abstract interfaces.

## The optimisation problem (`RectanglePacking.hpp`)

**`Rectangle`** — `id`, `width`, `height`, `rotated`, position `(x, y)`, `boxIndex`. `getW()`/`getH()` swap dimensions when `rotated` is set so all callers see rotation transparently. `area()` is rotation-invariant.

**`Box`** — one L × L square plus the rectangles placed inside it.
- `canPlace(rect, x, y, maxOverlapPercent, blockingIdx*)` — checks bounds and per-pair overlap against a tolerance. Overlap is defined per the spec as `intersection_area / max(area_r1, area_r2)`. The `maxOverlapPercent` parameter is just data — the box exposes it because overlap geometry *is* part of the problem; *deciding* the tolerance is a search-side concern handled by the overlap neighborhood.
- `addRectangle(rect, maxOverlapPercent = 0)` — places via **Bottom-Left fit with coordinate jumping**. Candidate positions are restricted to `0` or the right/top edge of an existing rectangle (the BL optimum always touches some edge). When `canPlace` fails because of a blocker, the inner scan jumps past the blocker's right edge in O(1) rather than re-checking each intermediate coordinate. The first valid `(x, y)` wins.
- `getOverlapViolation(maxOverlapPercent)` — sums per-pair overlap ratios above the threshold. Used only by the overlap neighborhood's scoring.

**`PackingSolution`** — implements `Solution`. Holds the box list and an `unplacedRectangles` safety bucket. The objective is pure:

```cpp
boxes.size() + unplacedRectangles.size() * 1000.0
```

The number of boxes used, with a hard penalty for any rectangle the placement primitive gave up on (which should never trigger for instances where every rectangle fits inside one box). `placeRectangle(rect)` is the standard insertion: try existing boxes unrotated, then rotated, then open a new box.

## The Greedy algorithm and its selection strategies (`GreedyStrategies.hpp`)

`GreedyAlgorithm<Rectangle, PackingSolution>` is instantiated with one of the strategies below and `placePacking` as the placement function.

- **`AreaStrategy`** — sort by area descending. Standard FFD heuristic: place hard-to-fit big rectangles into empty boxes first, smaller ones fill the gaps.
- **`MaxSideStrategy`** — sort by `max(width, height)` descending. Catches awkward narrow rectangles (e.g. 50 × 1) that area alone underweights — such a rectangle constrains the full width of a box even though its area is small.

These are the two "fundamentally different" strategies the spec requires: one optimises for total occupancy, the other for dimensional constraint.

Two additional helpers are used to build deliberately bad starts for local search (see below):
- **`SmallestFirstStrategy`** — area ascending. Provably worst-of-the-sort orderings on this problem.
- **`BadStartingStrategy`** — empty `sort()`. Leaves rectangles in their generation order; paired with `placeOnePerBox` for the worst-possible packing.

Two placement functions:
- **`placePacking`** — calls `placeRectangle`. The real greedy placement.
- **`placeOnePerBox`** — opens a fresh box for every rectangle. N rectangles → N boxes. Used to seed the geometry LS with maximum room to consolidate.

## Local-search neighborhoods

Each neighborhood follows the same pattern: a private scoring function — `geometryScore`, `permutationScore`, or `OverlapNeighborhood::score` — computes a comparable value that includes a small density bonus (`fill_ratio² × 0.1`) to break plateaus where box count is unchanged. `LocalSearch` does not call `objectiveValue()` for comparisons; the neighborhood owns its scoring. This is exactly the freedom the spec grants to neighborhoods.

### Geometry-based — `GeometryNeighborhood.hpp`

**Move set.** Pick a rectangle from box `b1`, remove it (deleting `b1` if it's now empty, adjusting `b2`'s index if `b2 > b1`), try to place it in some other box `b2` — unrotated first, then rotated. Accept if `geometryScore(neighbor) < geometryScore(current)`. No new box is ever opened by this neighborhood; consolidation is driven by the density bonus rewarding moves that drain sparse boxes.

**Score**: `boxes - density·0.1 + unplaced·1000`.

**Variants**:
- `GeometryNeighborhood` — exhaustive triple loop over `(b1, rect, b2)`. First-improvement. Used only at small N (≤ 200 in the test environment).
- `RandomizedGeometryNeighborhood` — 500 random `(b1, rect, b2)` attempts per step. The production variant.

**Bad start**: `placeOnePerBox` → every rectangle in its own dedicated box, N boxes total. Maximum room to consolidate; every move that puts two rectangles into the same box is an immediate score improvement.

### Rule-based (permutation) — `PermutationNeighborhood.hpp`

The "solution" here is a `vector<Rectangle>` — the *order* in which rectangles will be inserted by `placeRectangle`. The packing exists only transiently, rebuilt each time the order is scored.

**Move set** (`RandomizedSwapNeighborhood`): each attempt randomly chooses one of two move types:
- **2-swap** — exchange the rectangles at two positions.
- **1-insert** — remove the rectangle at position `i` and re-insert at position `j`. Can move a rectangle a long distance through the permutation in one step without disturbing the rectangles in between.

**Score** (`permutationScore`): materialise the implied packing, then `boxes - density·0.1 + unplaced·1000`.

**Tunings applied** (this is the most performance-sensitive neighborhood — every score evaluation rebuilds the full packing):

1. **Sparse-box biased source selection.** A helper `analyze()` at the top of each step does one packing pass and records, for each rectangle, which box it landed in, plus which box ended up sparsest. The permutation positions of rectangles in the sparsest box are collected as candidates. When the move's source position `i` is drawn, with probability 0.7 it comes from this candidate list; otherwise uniform. Directly implements the spec's hint: *"Rechtecke in relativ leeren Boxen anderswo in der Permutation zu platzieren."*
2. **Mixed move set.** Swap and insert are picked 50/50 per attempt. Swap is local in permutation distance; insert is long-range. Each is good for different kinds of structural change.
3. **Focused attempt budget.** `min(150, 40 + N/10)` attempts per step. Each attempt is biased to a position likely to improve the score, so coverage of useful moves is reasonable.
4. **`analyze()` returns the score alongside the box assignments**, saving the redundant call that would otherwise be needed for the baseline.

**Systematic variant**: `SystematicSwapNeighborhood` enumerates every `(i, j)` swap with `i < j` and returns the first improvement. O(N²) evaluations per step, each rebuilding the packing — used only at very small N (≤ 50 in the test environment).

**Bad start**: `SmallestFirstStrategy` — rectangles sorted by area ascending. Small rectangles claim corners first, leaving awkward gaps that big rectangles can't fit into → many extra boxes. Deterministic, so benchmarks are repeatable, and demonstrably worse than a random shuffle.

### Overlap-tolerant cooling — `OverlapNeighborhood.hpp`

The most conceptually different neighborhood: the others search inside the feasible region; this one starts deliberately *outside* it and walks toward feasibility as overlap tolerance is tightened.

**Move set.** Same as `RandomizedGeometryNeighborhood`, but every `canPlace` / `addRectangle` call uses the current `maxOverlapPercent`, so partial overlaps are legal during the search. Also includes a "open a new box" fallback when a relocation can't fit even with overlap allowed — essential during cooling, since rectangles that fitted under relaxed rules will no longer fit when the tolerance drops.

**Score** (`OverlapNeighborhood::score`):

```
boxes
  - density · 0.1
  + unplaced · 1000
  + violation · 5000
```

where `violation` sums per-pair overlap ratios that exceed the *current* tolerance. The 5000 weight is large enough that one outstanding violation outweighs any realistic number of extra boxes — so as the tolerance drops, the search is forced to resolve overlaps even at the cost of opening new boxes.

**Cooling schedule**:
- **GUI**: a step callback on `LocalSearch` reduces `maxOverlapPercent` by 0.1 each step (1.0 → 0.0 over ten cool-downs). The search continues at each level until no improvement is found, then the callback drives the next tightening.
- **Test environment**: an outer loop in `test_env.cpp` runs a fresh `LocalSearch` at each tolerance level (1.0, 0.75, 0.5, 0.25, 0.0), with a bounded step budget per level.
- At tolerance 0, both callers post-process: rectangles in any still-violating box are extracted and re-inserted via `placeRectangle` (which is strict). Final solution is guaranteed overlap-free.

**Bad start**: all rectangles stacked at `(0, 0)` inside a single box with overlap 1.0 — the objective is trivially 1 box, but only because the constraint is fully relaxed. A handful of empty boxes are pushed in alongside to give the neighborhood somewhere to spread rectangles into as the tolerance drops.

## GUI (`gui_main.cpp`, `GUI.hpp`, `Visualizer.hpp`)

SFML-based interactive interface, split across three files with non-overlapping responsibilities:

- **`GUI.hpp`** — reusable widgets: `Button` (label + click handler), `NumericInput` (label + clickable text box + ±5 buttons, bound to a caller's `int&`). Pure UI; no knowledge of the packing problem.
- **`Visualizer.hpp`** — given a `PackingSolution`, draws the boxes and rectangles. Boxes laid out in a `ceil(√N) × ceil(N/cols)` grid scaled to fit the available area (so the layout reflows as box count changes during a run). Rectangles drawn in the **Wong palette** — a 7-colour set designed to remain distinguishable under red-green colour blindness (the spec specifically warns against red/green) — with seven additional tints, assigned by `rect.id % 14`. Same ID always gets the same colour, so a single rectangle is visually trackable as it moves between boxes.
- **`gui_main.cpp`** — orchestrator. Owns the instance parameters (number of rectangles, side bounds, L), the current solution, and at most one active algorithm. Wires button clicks to algorithm runs, runs the per-frame step-and-redraw loop, and renders the stats text (box count, objective, elapsed wall time).

**Step skipping** (spec requirement: don't overwhelm the viewer with micro-changes):
- Geometry / overlap LS — up to 15 steps per frame; display updates only when box count changes, every 20 frames as a heartbeat, or on convergence.
- Permutation LS — one step per frame; every improvement is shown (each step can change the packing significantly because the whole order changes meaning).

**Overlap cooling continues across plateaus** — finding no improvement at the current tolerance does *not* stop the algorithm; the callback ticks the tolerance down and the search continues. Stops only when no improvement is found *and* the tolerance has reached zero.

## Test environment (`src/test_env.cpp`)

Parameterised by a list of tuples `(numInstances, numRects, minW, maxW, minH, maxH, L)`. For each tuple, generates that many instances via `InstanceGenerator` and runs every algorithm on every instance. Records final box count and CPU thread time (`CLOCK_THREAD_CPUTIME_ID`, per the spec); writes per-algorithm averages to a CSV file and a formatted table to stdout.

**Two invocations**:
- `./bin/test-env` — quick mode. Small tuples (`{2, 50, ...}`, `{1, 100, ...}`); finishes in seconds. For demonstration during assessment.
- `./bin/test-env --full` — full mode. `{5, 100, ...}, {3, 200, ...}, {3, 500, ...}, {1, 1000, ...}`. Produces a meaningful comparison across sizes.

**Algorithm gating** for the slow variants (the unrestricted algorithms run at every size):
- `LS-Random-Swap` — ≤ 500 rectangles. The permutation LS, even with the tunings above, has per-step cost that grows roughly as O(N × cost_of_placement) because every evaluation rebuilds the full packing; at 1000 rectangles a single step can exceed the time budget.
- `LS-Systematic-Geometry` — ≤ 200 rectangles. Exhaustive triple loop.
- `LS-Systematic-Swap` — ≤ 50 rectangles. O(N²) evaluations per step.

Always-on algorithms (Greedy variants, `LS-Random-Geometry`, `LS-Overlap-Cooling`) run on every tuple including the 1000-rectangle case.

## Performance

Measured on the full test (see `full_test_log.csv`):

- **Greedy variants** — sub-second at all sizes, including N = 1000.
- **`LS-Random-Geometry`** and **`LS-Overlap-Cooling`** — well inside 10 seconds at N = 1000, returning packings competitive with or better than greedy.
- **`LS-Random-Swap`** — inside 10 seconds at N ≤ 500.

This meets the spec's "1000 rectangles in 10 seconds" target for the algorithms that scale to that size, with quality that doesn't visibly improve under further iteration.
