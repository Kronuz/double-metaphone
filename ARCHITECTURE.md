# Architecture

The internal design of `double-metaphone`: one plain class and a single-pass
encoder that walks the word letter by letter, building two keys at once. For
usage see `README.md`; for the repo map and invariants see `AGENTS.md`.

## Shape

One header plus one source file:

```
  double_metaphone.h    The public class DoubleMetaphone (declaration + small inline methods)
  double_metaphone.cc   The encoder: encode_both() and its private helpers
```

The header is deliberately thin. It declares the class, the cached `_code_str`
member, the constructors, and the small forwarding methods (`encode`,
`encode_alternate`, `name`, `description`). The one method that does real work,
`encode_both`, is declared in the header and defined in the `.cc`, because the
rule set is large and there is no reason to recompile it in every translation
unit. That split is the only structural difference from header-only `soundex`.

## The class

`DoubleMetaphone` is a plain class, not a CRTP template. Soundex is a CRTP base
with per-language subclasses; Double Metaphone is a single algorithm, so there
is nothing to parameterize and a flat class is the honest shape. It still mirrors
the soundex surface so it drops in where a soundex encoder is used:

- a protected `std::string _code_str`, the cached primary key (set at
  construction), exactly like the soundex base.
- `encode(word)` returns the primary key for a fresh word. This is the drop-in
  entry point: a phonetic metric calls `encode(a)` / `encode(b)` and compares.
- `encode()` returns the cached `_code_str`.
- `encode_alternate(word)` and `encode_both(word)` expose the richer two-key API.
- `name()` / `description()`.

`encode` and `encode_alternate` are thin wrappers over `encode_both`, which
computes both keys in one pass (cheaper than two calls, and the two readings
are built in lockstep anyway).

## The encoder

`encode_both` is the whole algorithm. It has two internal helpers and a main
loop.

`Word` is a cursor over the cleaned input. The constructor upper-cases ASCII in
place, keeps `A-Z` and collapsed interior spaces, and drops everything else
(including UTF-8 high bytes, so non-ASCII input degrades to its letters). It then
pads a few blanks past the end, so look-ahead like `at(i + 2)` is always
in-bounds and reads as a non-letter rather than running off the buffer. Spaces
survive because a few rules key off word boundaries in multi-word names
(`SAN `, `VAN `, `VON `). Every index in the rules is relative to the first real
letter, which is what the published algorithm means by "position 0".

`Keys` collects the two outputs. `add(both)` pushes the same sound class onto
both keys; `add(primary, alternate)` pushes different classes, which is how a
word grows two readings. Most letters push the same thing onto both; the keys
diverge only where a rule explicitly calls for it.

The main loop walks the word one position at a time. A `switch` over the current
letter decides what to append and how far to advance. Each case is a faithful
port of the original rule for that letter: why `CH` is a hard `K` in "character"
but `X` in "chair" (Greek roots and Germanic spellings versus default English),
why a leading `WR` / `KN` / `GN` / `PN` / `PS` loses its first letter (silent
initial consonant), why `Smith` grows an `X` alternate (the initial `SM` reads
"shm" in the Germanic alternate), why the `B` in "dumb" and the `P` in
"Thompson" fall away. Vowels are kept only at the very start of the word and
dropped elsewhere, which is standard Double Metaphone.

Two free helpers gate several rules. `slavo_germanic` returns true when the word
contains `W`, `K`, `CZ`, or `WITZ` anywhere, a cheap proxy for "this spelling
looks Slavic or Germanic" that pushes a few rules toward hard `K` and `TS`/`F`
readings. `germanic` is the tighter, start-anchored test (`VAN `, `VON `, `SCH`
at the front) that specifically forces the `CH`-as-`K` reading. They are not
interchangeable; using one where the original uses the other changes real
encodings (it did, during development, on "Czechoslovakia").

## Matches the reference implementations

The encoder is checked against an established reference implementation across a
few hundred words and matches it everywhere, including the cases the `P` rule
decides: `Thompson` -> `TMPSN` and `Pfister` -> `PFSTR`. The `P` is kept (only
`PH` collapses to `F`), agreeing with the widely-copied JavaScript and Java ports.

## No truncation

Some Double Metaphone implementations cap each key at four characters. This one
does not: the keys are emitted at full length. Most of the canonical vectors in
the test (`SM0`, `XMT`, `K0RN`, `AKNR`, `PFSTR`, ...) are short, but that is the
natural length of those words' codes, not a truncation. Longer words produce
longer keys.
