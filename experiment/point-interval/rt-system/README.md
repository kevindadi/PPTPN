# RT-System cross-tool experiment

This experiment aligns three inputs around the same point-interval source model from [tools/PToPNer/examples/RT-System.ppn](../../../tools/PToPNer/examples/RT-System.ppn).

## Scope

- point-interval only: every timed transition uses `eft == lft`
- compare only state-space / reachability outputs
- ignore the formula blocks from the original PToPNer example

## Layout

- `ptopner/RT-System.ppn`: trimmed source model with transitions and places only
- `romeo/RT-System.xml`: hand-written Romeo net using the same place/transition names
- `priority/input.json`: TDG-style approximation for this repository's JSON-only pipeline
- `mapping.md`: explicit semantic mapping and approximations

## Exact alignments

The following are preserved directly between the original `.ppn` and the experiment-local `.ppn` / Romeo `.xml`:

- place names and initial markings
- transition names
- preset/postset connectivity
- fixed firing times
- explicit resource place `c1` with initial marking `2`

## Static-net vs schedule semantics

- PToPNer and this repository use static net structure to realize preemption-related behavior: resource competition and alternative firing paths are represented explicitly by places and transitions.
- Romeo expresses priority scheduling through `place/scheduling` parameters, so the same resource-competition topology is kept in the XML while dispatch priority is delegated to Romeo's scheduler semantics.
- Because of that difference, the Romeo file should not encode priority mainly through transition-local fields; the meaningful priority information is attached to the relevant places through `gamma` / `omega`.

## Known approximations

- The original formula sections are intentionally dropped.
- Romeo does not expose a clearly documented direct equivalent of PToPNer's `is_suspend` flag in the vendored DTD/examples. For this experiment, static resource topology is preserved explicitly, while priority ordering is handed to Romeo's scheduling semantics.
- This repository accepts only TDG JSON at [src/main.cpp](../../../src/main.cpp), so `priority/input.json` is an approximation of the RT-System control flow, not a strict place/transition level encoding. It uses task nodes for all transitions, scaled integer priorities, and precedence edges to preserve the major release/competition structure.

## Priority convention in `priority/input.json`

PToPNer priorities like `2.1` and `3.2` are scaled by 10 into integers so they fit the current JSON schema:

- `1` -> `10`
- `2.0` -> `20`
- `2.1` -> `21`
- `3.0` -> `30`
- `3.1` -> `31`
- `3.2` -> `32`

Timed transitions with original priority `0` stay at low integer priority `1` in the approximation.
