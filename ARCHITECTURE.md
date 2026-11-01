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

`encode`, `encode_alternate` and `encode_both` run the same walk, which is a
template over which keys to build. `encode_both` builds both in one pass (cheaper
than two calls, and the two readings are built in lockstep anyway). `encode`
builds only the primary key and `encode_alternate` only the alternate: the walk
decides every step from the word, never from a key, so a single-key walk takes
the same steps and skips every append to the other key.

## The encoder

`encode_keys<Mode>` is the whole algorithm. It has a few internal helpers and a
main loop.

`Word` is a cursor over the cleaned input. The constructor makes one pass over
the bytes through a 256-entry table: it upper-cases ASCII letters, keeps collapsed
interior spaces and tabs, and drops everything else (including UTF-8 high bytes,
so non-ASCII input degrades to its letters). The same pass notes whether the word
has a `W`, a `K` or a `CZ`. The text sits between four blanks in front and six
behind, so look-ahead and look-behind like `at(i + 2)` or `at(i - 4)` are plain
loads that read as a non-letter. A word of up to 118 bytes is cleaned in a stack
buffer; a longer one takes one heap allocation. `match_at` compares a string
literal against the word and never reads the padding as content. Spaces survive
because a few rules key off word boundaries in multi-word names (`SAN `, `VAN `,
`VON `). Every index in the rules is relative to the first real letter, which is
what the published algorithm means by "position 0".

`Keys` writes the outputs into buffers sized from the word (a step appends at
most two sound classes per letter it consumes). `add(both)` pushes the same sound
class onto both keys; `add(primary, alternate)` pushes different classes, which is
how a word grows two readings. Each class is a string literal, so an append is a
copy of a size known at compile time, and a single-key walk compiles the other
append away. Most letters push the same thing onto both keys; the keys diverge
only where a rule explicitly calls for it.

The main loop walks the word one position at a time. A `switch` over the current
letter decides what to append and how far to advance. Each case is a faithful
port of the original rule for that letter: why `CH` is a hard `K` in "character"
but `X` in "chair" (Greek roots and Germanic spellings versus default English),
why a leading `WR` / `KN` / `GN` / `PN` / `PS` loses its first letter (silent
initial consonant), why `Smith` grows an `X` alternate (the initial `SM` reads
"shm" in the Germanic alternate), why the `B` in "dumb" and the `P` in
"Thompson" fall away. Vowels are kept only at the very start of the word and
dropped elsewhere, which is standard Double Metaphone.

Two helpers gate several rules. The slavo-Germanic test is true when the word
contains `W`, `K`, `CZ`, or `WITZ` anywhere (`Word` notes it while cleaning), a
cheap proxy for "this spelling looks Slavic or Germanic" that pushes a few rules
toward hard `K` and `TS`/`F` readings. `germanic` is the tighter, start-anchored
test (`VAN `, `VON `, `SCH` at the front) that specifically forces the
`CH`-as-`K` reading. They are not interchangeable; using one where the original
uses the other changes real encodings, for example on "Czechoslovakia".

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
