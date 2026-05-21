# Codebase Tour — Rectangle Packing Optimizer

## Problem Statement

Given N rectangles and a box side-length L, pack all rectangles into as few L×L square **boxes** as possible. Rectangles may be rotated 90°. No two rectangles may overlap (their interiors must be disjoint). Minimize the number of boxes used.

The project implements **Greedy** and **Local Search** on this problem. A core requirement from the spec is strict separation: algorithm code knows nothing about boxes or rectangles, and problem code knows nothing about which algorithm is using it.

---

## `Framework.hpp` — The Generic Glue

This file satisfies the "generic implementation" requirement. The algorithm implementations here contain zero problem-specific logic.

### `Solution` (abstract base, lines 11–16)

Any solution to any optimization problem. Two pure virtual methods:
- `objectiveValue() → double` — the value to minimize
- `clone() → unique_ptr<Solution>` — deep copy

Every concrete solution in the project inherits from this.

### `Neighborhood<S>` (template, lines 21–27)

Interface for a local search neighborhood. One pure virtual method:

```cpp
virtual std::unique_ptr<S> findBetterNeighbor(const S& current) = 0;
```

Returns a strictly better neighbor if one is found, `nullptr` otherwise. The template parameter `S` binds the neighborhood to a specific solution type, but `LocalSearch` doesn't care which one.

### `SelectionStrategy<E>` (template, lines 39–46)

Greedy sorting interface. One method: `sort(vector<E>&)`, in-place. `E` is the element type (in practice, `Rectangle`). Problem code and algorithm code meet here without either knowing details about the other.

### `LocalSearch<S>` (template, lines 52–87)

The complete generic local search. Holds a current solution and a neighborhood.

**`performStep()`** (line 68):
1. Fires the step callback (if set)
2. Asks the neighborhood for a better neighbor
3. Adopts the candidate if the neighborhood returned one
4. Returns `true` if an improvement was made, `false` otherwise (local optimum reached)

`LocalSearch` no longer second-guesses the neighborhood with its own `objectiveValue()` check. The neighborhood is contractually responsible for returning only candidates it considers strictly better — and "better" is allowed to be a relaxed or heuristic score that differs from `Solution::objectiveValue()`. The spec explicitly permits this (it talks about modifying the objective for a neighborhood); pushing it into the neighborhood keeps `LocalSearch` and the problem class fully generic.

**`solve()`** (line 83): loops `performStep()` until it returns `false`, then returns a clone of the final solution.

**`setStepCallback()`** (line 63): registers a `std::function<void(LocalSearch<S>&)>` that fires at the top of every step. This is the hook used by the GUI's overlap cooling schedule.

#### Deep dive: the step callback

The callback type is `std::function<void(LocalSearch<S>&)>`. The `&` is essential — it passes the **live algorithm object** by reference, so the callback can reach in and mutate internal state (neighborhood parameters, current solution).

The callback fires **before** the neighbor search, so the sequence per step is:

```
1. callback runs → tightens overlap threshold
2. neighborhood searches with the NEW (tighter) threshold
3. currentSolution replaced if neighbor is better
```

**In the GUI** (`gui_main.cpp:156–166`), the overlap cooling callback is now a single in-place update:

```cpp
ls->setStepCallback([](LocalSearch<PackingSolution>& lsInst) {
    auto* nh = dynamic_cast<OverlapNeighborhood*>(&lsInst.getNeighborhood());
    if (nh) {
        double current = nh->getMaxOverlap();
        if (current > 0.0) {
            double next = std::max(0.0, current - 0.1);
            nh->setMaxOverlap(next);
        }
    }
});
```

- **`dynamic_cast<OverlapNeighborhood*>`** — `getNeighborhood()` returns the abstract base type `Neighborhood<PackingSolution>&`. The cast recovers the concrete type to access `setMaxOverlap()`. Returns `nullptr` safely if the cast fails (wrong neighborhood type).
- **`nh->setMaxOverlap(next)`** — tightens what the neighborhood considers a legal placement. At 0.1 per step the threshold drops from 1.0 to 0.0 over ten steps; the algorithm continues searching for improvements at each level before the next cool-down.

Earlier revisions of this code had to sync the threshold onto the solution as well, because `PackingSolution::objectiveValue()` referenced it for its penalty term. That coupling was the generic-implementation violation called out in the project review: overlap tolerance is a search concept and had no business living on the problem class. Now the threshold belongs to the neighborhood alone — only one update per cool-down, no risk of the two views drifting out of sync.

**Why a callback instead of an outer loop?**
`test_env.cpp` runs a fresh `LocalSearch` per cooling phase in a bounded outer loop — fine for a batch benchmark. The GUI can't do that: it runs one step per frame to stay responsive. The callback injects per-step behavior into `LocalSearch` without the template needing to know what "cooling" means.

The callback fires identically whether you call `solve()` (batch) or drive `performStep()` manually (GUI). Same mechanism, two usage patterns.

### `GreedyAlgorithm<E, S>` (template, lines 93–117)

Generic greedy. Holds:
- A list of elements to process
- A `SelectionStrategy<E>` for ordering them
- An empty starting solution
- A **function pointer** `PlacementFunc` typed as `void (*)(S&, E)`

`solve()` sorts elements via the strategy, then calls the placement function on each one. The algorithm body has no knowledge of rectangles, boxes, or any problem specifics.

**Design choice — function pointer vs. virtual method**: placement is passed as a free function pointer rather than another virtual interface. This keeps the algorithm fully generic without an extra template parameter or abstract class. The tradeoff is that the placement function cannot capture state (it must decay to a raw pointer), which is acceptable here since placement only needs the solution and the element.

---

## `RectanglePacking.hpp` — The Problem Representation

Three classes: `Rectangle`, `Box`, and `PackingSolution`.

### `Rectangle` (lines 9–23)

Simple data struct: `id`, `width`, `height`, `rotated`, `x`, `y`, `boxIndex`.

`getW()` and `getH()` hide rotation from all callers — when `rotated` is true they swap. Everything in the codebase uses these accessors so rotation is transparent. `area()` always returns `width * height` regardless of rotation (area is rotation-invariant, which is correct).

### `Box` (lines 25–132)

Holds all `Rectangle`s placed inside one L×L square.

#### `getOverlapViolation()` (lines 32–53)

Iterates all pairs, computes intersection area, sums violations above the threshold:

```
overlapRatio = overlapArea / max(area_r1, area_r2)
violation   += overlapRatio - maxOverlapPercent   (only when ratio exceeds threshold)
```

The denominator is `max` of the two areas (not the union). Design choice: it measures how much of the *larger* rectangle is covered, penalizing a small rectangle buried inside a large one more severely than the symmetric case.

#### `canPlace()` (lines 55–81)

Two checks per call:
1. Bounds — rectangle must fit entirely within `[0, L) × [0, L)`
2. Per existing rectangle — intersection must not exceed `maxOverlapPercent`

The `blockingIdx` out-parameter tells the caller *which* existing rectangle caused the failure. `addRectangle` uses this to skip ahead in its search (see below). When `maxOverlapPercent <= 0.0`, any intersection fails immediately (strict mode).

#### `addRectangle()` — Bottom-Left with Coordinate Jumping (lines 83–131)

The core placement heuristic — the most algorithmically interesting part of the file.

**Candidate generation**: instead of scanning every pixel (O(L²)), only positions at the right or top edge of existing rectangles are considered. The optimal Bottom-Left position for a new rectangle always has its left or bottom edge touching either the box wall or another rectangle's edge. So candidates are:
- x: `{0} ∪ {r.x + r.getW() for each r in box}`
- y: `{0} ∪ {r.y + r.getH() for each r in box}`

Both lists are sorted and deduplicated.

**Scan order**: outer loop y (rows), inner loop x (columns) — Bottom-Left behaviour. The first valid `(x, y)` is taken immediately.

**Coordinate jumping** (lines 112–119) — key tuning:

```cpp
} else if (blockingIdx != -1) {
    int jumpTo = rectangles[blockingIdx].x + rectangles[blockingIdx].getW();
    while (ix + 1 < (int)xCoords.size() && xCoords[ix + 1] < jumpTo) {
        ix++;
    }
}
```

When `canPlace` fails and returns the blocking rectangle's index, all x-coordinates to the left of that rectangle's right edge are guaranteed to also be blocked by the same rectangle. The inner loop skips forward past the blocker's right edge in O(1) instead of rechecking each candidate — eliminating a large class of redundant `canPlace` calls.

### `PackingSolution`

Inherits from `Solution`. Holds a list of `Box`es and an `unplacedRectangles` fallback list. Nothing else — no overlap threshold, no search-side state.

#### `objectiveValue()`

The **pure problem objective**:

```cpp
return boxes.size() + unplacedRectangles.size() * 1000.0;
```

That's it. The number of boxes (which the spec wants minimised), plus a hard penalty for any rectangle the placement step gave up on (which should never trigger for instances where every rectangle fits inside an L×L box).

An earlier version of this class held an `maxOverlapAllowed` field and an objective that included a fill-ratio bonus and an overlap-violation penalty. That bundled search-strategy concerns into the problem class and broke the spec's "generic implementation" rule. Both the gradient heuristic (fill-ratio²) and the violation penalty now live where they belong:
- `geometryScore()` in `GeometryNeighborhood.hpp` — boxes + fill-ratio bonus.
- `permutationScore()` in `PermutationNeighborhood.hpp` — same shape, applied to the packing implied by the permutation.
- `OverlapNeighborhood::score()` — adds the overlap-violation penalty (×5000) against the neighborhood's own tolerance.

Each neighborhood compares candidates using its own score; `LocalSearch::performStep` adopts whatever the neighborhood returns without re-running `objectiveValue()`. The spec explicitly allows a neighborhood to use a modified objective, so this is exactly the architecture asked for.

#### `placeRectangle()` (lines 170–184)

Entry point used by both `GreedyAlgorithm` and permutation local search. Attempt order:
1. Existing box, unrotated
2. Existing box, rotated 90°
3. New box, unrotated
4. `unplacedRectangles` (fallback, should not occur if no side exceeds L)

`tryPlace()` iterates boxes in insertion order and takes the first fit — this is **First-Fit**, not Best-Fit. Combined with the sorting strategies in `GreedyStrategies.hpp`, this forms the standard First-Fit Decreasing heuristic.

---

## `GreedyStrategies.hpp` — Selection Strategies and Placement Functions

Short file, but every class is a deliberate design decision.

### The three real strategies

All implement `SelectionStrategy<Rectangle>` — one `sort()` method, in-place, no problem state.

**`AreaStrategy`** — sorts largest area first. Standard bin-packing heuristic: place the hard-to-fit rectangles early while boxes are still empty. Smaller rectangles act as filler.

**`MaxSideStrategy`** — sorts by `max(width, height)` descending. A 50×1 rectangle and a 7×7 have nearly the same area but the 50×1 is much harder to place because it constrains the full width of a box. This catches awkward cases that area alone misses.

**`SmallestFirstStrategy`** — sorts smallest area first. Intentionally bad: small rectangles placed first leave awkward gaps that large rectangles can't fill. Used as the **decidedly bad starting permutation** for the rule-based (permutation) local search in both the GUI and the test environment — the spec requires a deliberately poor start so improvements are clearly visible, and SmallestFirst is deterministic and provably worse than a random shuffle on this problem.

### `BadStartingStrategy`

Empty `sort()` body — leaves rectangles in random generation order. Combined with `placeOnePerBox` (below) to produce the worst possible start.

### The two placement functions

Free functions passed as function pointers to `GreedyAlgorithm`.

**`placePacking`** — calls `sol.placeRectangle(rect)`. Normal First-Fit: tries existing boxes, opens a new one only if necessary.

**`placeOnePerBox`** — ignores all existing boxes, always opens a new one. Every rectangle gets its own dedicated box — N rectangles → N boxes. The worst possible objective short of leaving rectangles unplaced. Sets `x=0, y=0, rotated=false` explicitly since the new box is always empty.

### Why two separate bad-start mechanisms?

`BadStartingStrategy` + `placePacking` still fits rectangles into existing boxes, just in a bad order — suboptimal but reasonable. `BadStartingStrategy` + `placeOnePerBox` produces the absolute worst packing. The latter is used for geometry-based local search (max room to improve via moves between boxes). The former is used for permutation-based local search, where "badness" comes from ordering (SmallestFirst), not placement.

---

## `GeometryNeighborhood.hpp` — Moving Rectangles Between Boxes

Two classes: `GeometryNeighborhood` (systematic) and `RandomizedGeometryNeighborhood` (tuned). Both generate neighbors by the same logical move, differing only in how they search.

### The move

Every neighbor is produced by:
1. Pick rectangle `rect` from box `b1`
2. Remove it from `b1`
3. If `b1` is now empty, delete it and adjust `b2`'s index (`adjustedB2--` when `b2 > b1`)
4. Try placing `rect` into box `b2` — unrotated first, then rotated
5. Accept only if the result is strictly better

**Step 3 index adjustment** is the critical bookkeeping: when `b1` is erased, all indices above it shift down by one. Getting this wrong would silently place the rectangle into the wrong box.

**No new box is ever opened.** The neighborhood only moves rectangles between existing boxes. The `geometryScore()` helper at the top of the file applies a squared fill-ratio bonus on top of the box count, which gives both variants a gradient even when box count doesn't change yet — a move that drains a sparse box improves the score and nudges the search toward eventually emptying it. The score lives in the neighborhood, not on `PackingSolution`, so the problem class stays pure.

### `GeometryNeighborhood` — Systematic (lines 12–60)

Triple nested loop: every `b1`, every rectangle in `b1`, every other `b2`. Returns the **first** strictly better neighbor (first-improvement, not best-improvement).

Correct but slow. Worst-case per step: O(K·N) neighbor evaluations, each calling `addRectangle` which is itself O(N²). Only used for small instances (`numRects <= 200` in `test_env.cpp`, and the "Syst Geometry" button in the GUI).

### `RandomizedGeometryNeighborhood` — Tuned (lines 65–123)

Same move, 500 random attempts per call. Each attempt picks random `b1`, random rectangle, random `b2`. Returns the first strictly better result found, or `nullptr` if none found in 500 tries.

**Why 500?** Tuned constant balancing coverage vs. speed. At 500 attempts × O(N) per attempt, a step on 1000 rectangles costs ~500K operations — fast enough to run many steps within the 10-second budget.

**No explicit sparse-box bias.** All boxes are picked uniformly. `geometryScore()` provides implicit bias: moves that drain sparse boxes are rewarded even without a box-count drop, so the random search still consolidates over many steps.

**Empty-box guard** (line 81): `b1` is sampled before checking if it's empty. Rather than adjusting the distribution, the code skips and retries — acceptable with 500 attempts since empty boxes become rare as consolidation progresses.

---

## `PermutationNeighborhood.hpp` — Rule-Based Local Search

### `PermutationSolution`

Indirect solution representation. Holds only a `vector<Rectangle>` (the ordering) and `L`. No boxes, no coordinates.

**`objectiveValue()`** builds a full `PackingSolution` on demand by replaying `placeRectangle` on every rectangle in permutation order, then returns that solution's objective — which, after the refactor, is just the box count (plus the safety-net penalty for unplaced rectangles). The result is cached in `mutable double cachedObjective`; `mutable` is needed because `objectiveValue()` is `const` (required by the `Solution` interface) but must write to the cache.

The cache is never explicitly invalidated — it doesn't need to be. `PermutationSolution` objects are immutable after construction. Every neighbor is a new object with `cachedObjective = -1`, so it always recomputes on first call.

The integer-box-count objective alone would leave the permutation neighborhoods staring at large plateaus where most moves produce identical box counts. The free function `permutationScore()` at the top of the file materialises the implied packing and adds the same squared fill-ratio bonus the geometry neighborhoods use. The tuned randomized neighborhood uses a private `analyze()` helper that computes the same score *and* records each rectangle's box assignment in one pass (used for sparse-box bias — see below); `SystematicSwapNeighborhood` just calls `permutationScore()`. Either way, every comparison goes through the neighborhood-side score, never through `objectiveValue()` on the solution.

### `SystematicSwapNeighborhood` (lines 37–58)

Tries every pair `(i, j)` with `i < j`, swapping in-place, evaluating, then swapping back. Returns the first strictly better result.

The in-place swap-and-backtrack avoids allocating a new vector per candidate. Worst case: O(N²) evaluations per call, each rebuilding a full `PackingSolution` — O(N³) per step total. Restricted to `numRects <= 50` in `test_env.cpp`.

### `RandomizedSwapNeighborhood` — the tuned version

Originally this was a pure 2-swap with `min(1000, 2N)` random attempts per step. That gave the local search 1000 chances per step at large N, each of which rebuilt a full packing — at N=200 this already took ~20 s, and the test environment silently skipped it at higher N. The spec asks every algorithm to handle 1000 rectangles in 10 s, so the neighborhood needed rethinking. Today's version applies four tuning measures:

**(A) Sparse-box-biased source selection.** At the top of each `findBetterNeighbor`, the helper `analyze()` does one packing pass and records which box each rectangle id landed in plus which box ended up sparsest. The positions of rectangles that landed in the sparsest box are collected as `sparsePositions`. When picking the source position `i` for the next move, with probability 0.7 the neighborhood draws from `sparsePositions`; otherwise uniformly. This directly implements the spec's hint (`Instruction.txt` line 75–76): *Rechtecke in relativ leeren Boxen anderswo in der Permutation zu platzieren.*

**(B) Mixed move type — swap or insert.** Each attempt flips a coin: half the time the move is a 2-swap (as before), half the time it's an **insertion** — remove the rectangle at position `i`, insert it at position `j`. Insertion can pull a rectangle a long distance across the permutation in one move without disturbing the elements in between; swap can only achieve that by chaining many moves. The two move types complement each other.

**(C) Lower attempt budget.** Now `min(150, 40 + N/10)` instead of `min(1000, 2N)` — at N=1000 that's 140 attempts versus 1000. The reduction is safe precisely because (A) makes each attempt more likely to land, so per-step coverage of *useful* moves is comparable to or better than the old version while wall-clock time drops by ~7×.

**(D) `analyze()` returns the score itself**, so we don't pay an extra `permutationScore` call at the top of the step — the two passes are folded into one. The score used inside the loop for the baseline is `info.score` (computed in `analyze`); per-candidate scores still use the standalone `permutationScore` helper because we don't need the box-assignment side data for neighbors.

**Where this leaves us**: the time budget for permutation LS is now in line with the geometry neighborhoods (a few seconds at N=1000 in the test environment), and quality is competitive with greedy at all sizes.

`if (i == j) continue` skips no-op moves — same rationale as `b1 == b2` in the geometry neighborhood.

### Key asymmetry vs. geometry neighborhoods

Geometry works directly on `PackingSolution` — coordinates are real, moves are physical relocations. Permutation works one abstraction level higher: the "solution" is an ordering, and the packing is always derived by replaying greedy placement from scratch. Every candidate evaluation re-runs the full greedy. Slower per evaluation, but swapping two elements can produce a completely different packing topology rather than just a local adjustment.

---

## `OverlapNeighborhood.hpp` — Overlap-Tolerant Cooling Neighborhood

The most conceptually different neighborhood. The others search within the space of valid solutions. This one starts deliberately outside it — allowing overlaps — and cools its way back in.

### The idea

Start at 100% overlap allowed (trivially 1 box, all rectangles stacked at the origin). Cool the tolerance toward 0% while local search improves at each level. The neighborhood's own `score()` adds an overlap-violation penalty (×5000) that makes violating solutions expensive, so the search is pushed to resolve overlaps as the threshold tightens.

### Fields

`maxOverlapPercent` — current tolerance, passed to `canPlace` and `addRectangle`, and used by `score()` when summing per-box violations. Mutable via `setMaxOverlap()` — used by both the GUI step callback and `test_env.cpp`'s outer cooling loop.

`maxAttempts` — constructor parameter (not hardcoded). `test_env.cpp` computes it as `max(50, min(500, 30000 / numRects))`, scaling down for large instances to stay within the time budget.

### `score()` — the neighborhood's private objective

```cpp
double score(const PackingSolution& sol) const {
    double penalty = 0, density = 0;
    for (const auto& box : sol.boxes) {
        penalty += box.getOverlapViolation(maxOverlapPercent);
        // ... accumulate fill² for density bonus
    }
    return sol.boxes.size()
           - density * 0.1
           + sol.unplacedRectangles.size() * 1000.0
           + penalty * 5000.0;
}
```

Three concerns mixed into one comparable score: number of boxes (the actual objective), a fill-density gradient for plateau navigation, and an overlap-violation penalty against the *current* threshold. This is exactly the kind of search-side tweaking the spec allows in a neighborhood — but specifically forbids on the problem class. Keeping `score()` here is what lets `PackingSolution::objectiveValue()` stay a one-liner.

The candidate comparison in `findBetterNeighbor` is `score(neighbor) < score(current)`, not `objectiveValue()`.

### Key difference from `RandomizedGeometryNeighborhood`

**A new box is opened as a fallback**:
```cpp
if (!placed) {
    Box newBox(neighbor.L);
    if (newBox.addRectangle(rect, maxOverlapPercent)) {
        neighbor.boxes.push_back(newBox);
        placed = true;
    }
}
```
`GeometryNeighborhood` never opens new boxes. This one does. Essential during cooling: as the tolerance tightens, rectangles that previously fit in an existing box under relaxed rules can no longer be placed there. Without this fallback, they'd go to `unplacedRectangles` and incur the ×1000 penalty. Opening a new box is recoverable — subsequent steps can consolidate.

**Empty-box safety guard**: if `b1` had only one rectangle, removing it deletes the box and `neighbor.boxes` could be empty. Sampling `b2` from an empty vector is undefined behaviour. The guard ensures there is always at least one target box.

### What "better" means during cooling

At high overlap, `score()`'s penalty term is near zero (most overlaps fall under the generous threshold) so the search mainly improves fill ratios. As the threshold drops toward 0, the ×5000 penalty dominates and the search is under increasing pressure to resolve overlaps. Near zero, eliminating one overlap pair outweighs any fill-ratio gain. At exactly 0.0, `canPlace` enters strict mode and the neighborhood behaves like a geometry neighborhood with a new-box fallback.

---

## Visualization Architecture — Three-File Split

The GUI is split across three files with distinct roles:

| File | Role |
|---|---|
| `GUI.hpp` | Reusable input widgets — knows nothing about rectangles or algorithms |
| `Visualizer.hpp` | Renders a `PackingSolution` — knows nothing about widgets or algorithms |
| `gui_main.cpp` | Orchestrator — defines layout, wires everything together, owns algorithm state, drives the render loop |

**`GUI.hpp`** is purely about user input. `Button` and `NumericInput` are self-contained components that handle their own events and draw themselves wherever they're told. They have no knowledge of the packing problem.

**`Visualizer.hpp`** is purely about output. Given a `PackingSolution`, it draws boxes and rectangles. It has no knowledge of widgets, button clicks, or algorithm state — only the single `sidebarWidth` boundary it must respect.

**`gui_main.cpp`** is the glue. It owns all application state (`currentSol`, `activeGeomLS`, `activePermLS`), hardcodes all pixel positions, connects button clicks to algorithm actions, and runs the per-frame step-execution and rendering loop. Nothing executes until `while (window.isOpen())` — everything before that line is setup and wiring.

The sidebar exists only as a convention enforced by two matching constants: `visualizer.sidebarWidth = 300.0f` and the `x=20` origin of all widgets. There is no layout engine.

---

## `GUI.hpp` — Input Widgets

Two self-contained SFML UI components.

### `Button` (lines 9–53)

Constructor centers the label text geometrically by computing the text's bounding box and setting its SFML origin to its center. `handleEvent()` checks for a left click inside the button bounds and fires `onClick`. `draw()` implements hover by reading the mouse position every frame and switching fill color (`100,100,100` → `130,130,130`) — no state stored, recomputed each draw call. `onClick` is a `std::function<void()>`.

### `NumericInput` (lines 55–150)

Composite widget: label + clickable text box + `+`/`−` buttons. Binds to `int&` — a reference to the caller's variable — so edits are immediately visible without explicit get/set.

**Two states** controlled by `focused`. Unfocused: displays current value. Focused (on click): shows `inputBuffer` with a `|` cursor, accepts digit keys and backspace, updates the bound value live.

**`commit()`** — the single parse point. Converts `inputBuffer` via `std::stoi`, clamps to `minValue`, silently ignores invalid input. Called on Enter, on click-away, and before `+`/`−` to flush any in-progress typed value.

**`+`/`−` buttons** step by 5 (hardcoded, appropriate for expected value ranges). Buffer capped at 5 characters → max value 99999.

---

## `Visualizer.hpp` — Drawing the Packing

### Layout (lines 43–51)

Boxes arranged in a `ceil(sqrt(N)) × ceil(N/cols)` grid — most square layout for any N, minimising wasted space. Scale factor computed to fit the entire grid into the draw area:
```cpp
scale = min(availableWidth / neededWidth, availableHeight / neededHeight)
```
Automatically adjusts as box count changes during a run — more boxes → smaller scale, fewer → larger.

### Drawing order (lines 53–87)

Box background drawn first, rectangles on top. Correct layering so rectangle outlines always appear above the grey box fill.

### Colorblind-safe palette (lines 72–81)

The **Wong palette** — a standard 7-color set designed to remain distinguishable under common color vision deficiencies, particularly red-green. The instruction explicitly warns against red/green and recommends yellow/blue. Colors assigned by `rect.id % 7` so each rectangle keeps the same color across all frames, making individual rectangles visually trackable as they move between boxes during local search.

The sidebar background is redrawn at the top of every `draw()` call, clearing the previous frame's widget content before SFML widgets are drawn on top.

---

## `gui_main.cpp` — The Interactive GUI

### State model

```cpp
std::unique_ptr<LocalSearch<PackingSolution>>     activeGeomLS;
std::unique_ptr<LocalSearch<PermutationSolution>> activePermLS;
```

At most one algorithm runs at a time. Every button that starts a new algorithm calls `.reset()` on both first — "stop current, start new." `currentSol` (`shared_ptr<PackingSolution>`) is the snapshot being drawn; the active algorithm owns its own internal copy.

### Button actions

**Instance buttons** — reset active algorithms, generate new rectangles, build a `placeOnePerBox` bad start and display it immediately. Gives a clear before/after when an algorithm runs next.

**Greedy buttons** — synchronous. Run `solve()` to completion in one frame, update `currentSol`, record elapsed time. No active algorithm is set.

**LS buttons** — asynchronous. Construct a `LocalSearch` and assign it to `activeGeomLS` or `activePermLS`. The solve loop runs across many frames, one batch of steps per frame.

### Per-frame execution — geometry LS

```cpp
const int STEPS_PER_FRAME  = 15;
const int DISPLAY_INTERVAL = 20;
```

Runs up to 15 steps per frame. Visualization updates only when:
- Box count changed (primary meaningful event)
- 20 frames elapsed without update (heartbeat, prevents frozen screen)
- No improvement found (algorithm converging — show final state)

This satisfies the instruction's requirement to skip micro-changes in fast succession. At 60fps and 15 steps per frame, the screen updates at most 4 times per second.

**Overlap cooling termination**: when no improvement is found and an `OverlapNeighborhood` is active, the algorithm is NOT stopped — the step callback is still ticking down the threshold and will eventually open new improvement opportunities. Algorithm stops only when no improvement AND no active cooling.

**Post-processing on finish**: same cleanup as `test_env.cpp` — extract rects from violating boxes, erase empty boxes, repack cleanly, then `activeGeomLS.reset()`.

### Per-frame execution — permutation LS

```cpp
const int STEPS_PER_FRAME  = 5;
const int DISPLAY_INTERVAL = 30;
```

5 steps per frame (vs. 15) because each permutation step rebuilds a full `PackingSolution` internally — far more expensive than a geometry step. Display requires an explicit rebuild from the permutation, done only at display time, not every step.

### Elapsed time

Starts when a button is pressed, keeps ticking while an algorithm is active, freezes when it finishes. Format switches at 1000ms: below → milliseconds, above → `s.f` seconds with one decimal.

### Render order every frame

1. `window.clear(White)`
2. `visualizer.draw()` — boxes and rectangles
3. Inputs
4. Separator line (1px grey rectangle, inlined — not a widget)
5. Buttons
6. Stats text (boxes used, raw objective, elapsed time)

After the refactor the displayed objective equals `boxes.size()` (plus the unplaced penalty, which should never trigger), so box count and objective track each other exactly. The plateau-bridging heuristic now lives inside each neighborhood's private `score()` — invisible to the stats line but still doing the work of moving the search across integer plateaus.

---

---

## How Each Algorithm Is Visualized

### Greedy (Area / MaxSide / Smallest)

On click, the full algorithm runs synchronously in one shot — rectangles are sorted and packed into a temporary solution — and the wall-clock time is recorded immediately. The GUI then **replays** the sorted insertions at **one rectangle per frame** into a fresh empty solution. At 60 fps, 40 rectangles animate over ~0.67 s. The time displayed is the actual algorithm time, not the animation duration.

### LS (Rand Swap) / LS (Syst Swap)

The starting permutation is **SmallestFirst** — the instance sorted by area ascending. This matches the spec's "decidedly bad" requirement (small rectangles placed first leave gaps that the big ones can't fill later) and is deterministic, which makes benchmarking repeatable. On click, this ordering is immediately repacked into `currentSol` and displayed — the viewer sees the bad starting state before any improvement begins.

**One step per frame.** After every step, `currentSol` is rebuilt by replaying `placeRectangle` on the current permutation. Every box-count drop is therefore visible as it happens. The algorithm stops as soon as a step finds no improvement.

### LS (Rand Geometry) / LS (Syst Geometry)

Starts from `placeOnePerBox` — every rectangle in its own dedicated box. **15 steps per frame.** `currentSol` updates only when:
- The box count changes (a rectangle was consolidated — meaningful event)
- 20 frames have elapsed without an update (heartbeat, prevents frozen screen)
- A step found no improvement (algorithm converging — show final state)

This step-skip logic avoids overwhelming the viewer with micro-changes that do not affect box count.

### LS (Overlap Cooling)

Starts with all rectangles stacked at (0, 0) inside a single box — 100 % overlap allowed, trivially one box. The step callback reduces the tolerance by 0.1 per step, cooling from 1.0 to 0.0 over ten cool-down events. The search continues at each level until no further improvement is found before the next tightening. Same 15-steps-per-frame and step-skip logic as the geometry neighborhoods.

Unlike the others, a plateau (no improvement) does **not** stop the algorithm as long as the overlap threshold is still above zero — the cooling continues regardless. Visually, the box count often stays flat for many frames while the threshold tightens, then drops sharply as the neighborhood's overlap penalty (now inside its private `score()`, not the solution's objective) forces rectangles to separate.

On final convergence at tolerance = 0, any boxes still containing violations are post-processed: their rectangles are extracted, empty boxes removed, and the contents repacked strictly via `placeRectangle`. The display updates to the clean final state.

---

*More sections will be added as the tour progresses.*
