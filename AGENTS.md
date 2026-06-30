# AGENTS.md

Working notes for agents modifying this repository. For the design read
`ARCHITECTURE.md`; for usage read `README.md`. This file covers the repo layout,
how to build and test, the invariants you must not break, and the traps that are
easy to fall into.

## Repo map

```
double_metaphone.h     Public class DoubleMetaphone (declaration + small inline methods). Header.
double_metaphone.cc    The encoder: encode_both() and its Word/Keys helpers and rule switch.
test/test.cc           Runnable smoke test: documented vectors, homophone collapse, interface.
examples/demo.cc       Runnable tour (not a test): prints primary/alternate keys per group.
CMakeLists.txt         Compiled library `double_metaphone` (+ alias double_metaphone::double_metaphone); CTest test `double_metaphone`.
LICENSE                MIT, Copyright (c) 2015-2019 Dubalu LLC.
README.md              What it is, install, usage.
ARCHITECTURE.md        Internal design: the class, the single-pass encoder, standard P handling.
```

Unlike `soundex` (header-only), this library compiles one `.cc`: the Double
Metaphone rule set is large, so it lives in a source file. The CMake target is a
normal compiled library that adds the source dir to the include path, requests
`cxx_std_20`, and links nothing else.

## Build and run the test

```sh
cmake -B build && cmake --build build && ctest --test-dir build
```

No network access is needed at any step (there are no fetched dependencies).
Expected output ends with `all double metaphone tests passed`, exit 0. The test
target is `double_metaphone_test`; the registered CTest name is
`double_metaphone`. A `double_metaphone_demo` target builds alongside.

## Conventions

- **C++20.** The target requests `cxx_std_20` to stay uniform with the sibling
  libraries.
- **Zero dependencies.** Pure standard library: `<string>`, `<string_view>`,
  `<utility>`, `<cstddef>`. The encoder does its own ASCII upper-casing, so do
  not add a `strings` (or any other) dependency for case folding. There is no
  `FetchContent` and no `find_package`; keep it that way.
- Tabs for indentation, double quotes in code, no em dashes in prose.

## Load-bearing invariants

- **`encode()` / `encode(word)` return the PRIMARY key.** That is what makes the
  class a drop-in for a soundex encoder: a phonetic metric calls `encode(a)` /
  `encode(b)`. The alternate is opt-in via `encode_alternate` / `encode_both`.
  Do not change `encode` to return the alternate or a pair.
- **`encode()` vs `encode(word)` are different.** No-arg `encode()` returns the
  primary key cached at construction in `_code_str`; `encode(word)` runs the
  algorithm on a fresh word. Don't collapse them.
- **The documented vectors are the reference, not the test.** `test/test.cc`
  pins the canonical published keys (`Thompson` -> `TMPSN`, `Smith` ->
  `SM0`/`XMT`, `Schmidt` -> `XMT`/`SMT`, `Catherine` -> `K0RN`/`KTRN`,
  `Wagner` -> `AKNR`/`FKNR`, `Pfister` -> `PFSTR`, `Aubrey` -> `APR`,
  `dumb` -> `TM`). If a change breaks one of these, the change is wrong, not the
  test. Fix the rule, don't weaken the assertion.
- **The homophone collapse is the point.** `Smith.alternate` == `Schmidt.primary`
  (both `XMT`). Words that sound alike must share a key across the
  `{primary, alternate}` cross-product. The test pins this and a few pairs
  Soundex misses (`Knight`/`Night`, `Wright`/`Rite`); keep them passing.
- **Two keys, built in one pass.** `encode_both` is the only place the algorithm
  lives. `Keys::add(both)` pushes to both keys; `add(primary, alternate)` is how
  they diverge. Don't split the algorithm into two passes; the readings are
  built in lockstep.

## Standard P handling

This encoder matches a well-known reference implementation across a few hundred
words, with no departures. The `P` rule only special-cases `PH` -> `F`; `P` is
otherwise kept, so `Thompson` -> `TMPSN` and `Pfister` -> `PFSTR` (agreeing with
the common ports). Don't reintroduce `MP`/`PF` silencing.

These follow the original Lawrence Philips description, not the JS/Java ports.
**Do not "fix" them toward the ports.** If you do, the documented-vector test
fails, which is the signal that you broke the canonical behavior.

## Traps

- **Index math runs past the ends on purpose.** The `Word` cursor pads blanks
  past the end and treats negative or out-of-range indices as a non-letter
  space, so a rule can freely read `at(i - 4)` or `at(i + 3)` without bounds
  checks at every call site. Keep new rules going through `Word::at` /
  `match_at` / `substr`; do not index the raw buffer directly.
- **`slavo_germanic` and `germanic` are different tests.** The first is "W/K/CZ/
  WITZ anywhere"; the second is "starts with VAN /VON /SCH". A handful of rules
  pick one specifically (the `CH`-as-`K` reading uses `germanic`, the `Z` and
  `GN` readings use `slavo`). Swapping them changes real encodings. If you touch
  one, re-run the test and a broad comparison.
- **Spaces are kept, not stripped.** The cleaner keeps single interior spaces so
  multi-word rules (`SAN `, `VAN `, `VON `) fire. Don't drop them; "San Jacinto"
  depends on the space to read its `J` as the Spanish `H` sound.
- **Vowels survive only at position 0.** A vowel anywhere else adds nothing.
  That is standard Double Metaphone, not a bug to "fix".

## How to extend

- **Add a rule or refine a letter.** Edit the relevant `case` in the switch in
  `encode_both`. Each case is a faithful port of the published rule for that
  letter; keep new logic going through the `Word` accessors and `Keys::add`.
- **Always extend the smoke test.** `test/test.cc` is the only executable check.
  When you add or change behavior, pin the canonical key for a representative
  word. If you can't reason out the expected key by hand, check it against a
  trusted reference implementation and pin the actual output, then assert it.
  Don't guess.

## Provenance

A fresh implementation of Lawrence Philips' Double Metaphone (C/C++ Users
Journal, June 2000), written from the algorithm description for the Kronuz
family of phonetic libraries. It is a sibling to
[soundex](https://github.com/Kronuz/soundex) and is meant as a modern, better
drop-in for those encoders: it handles silent letters and language-specific
spellings, and emits a primary plus an alternate key instead of one.
