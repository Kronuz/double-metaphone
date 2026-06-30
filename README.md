# double-metaphone

A small, standalone **Double Metaphone** phonetic encoder for C++20. It turns a
word into two phonetic keys so that words that sound alike collapse to the same
string, even when their spellings have nothing in common.

## What it is

Double Metaphone is Lawrence Philips' 2000 successor to Soundex (C/C++ Users
Journal, June 2000). It is a better fit for real names than Soundex because it
understands silent letters, common non-English spellings, and the many ways one
sound gets written in English. Where Soundex sees only the surface letters,
Double Metaphone reads the word the way it is pronounced.

The defining difference from Soundex is that it emits **two** keys per word:

- the **primary** key captures the dominant English pronunciation.
- the **alternate** key captures a plausible foreign reading (often Slavic,
  Germanic, or Romance). It equals the primary when the word has only one
  likely pronunciation.

Two keys are what let `Smith` and `Schmidt` collapse: `Smith` is
`SM0` / `XMT` and `Schmidt` is `XMT` / `SMT`, and they meet on `XMT`. Soundex
never catches that pair. The keys are built from a small sound alphabet,
the letters `A B F H J K L M N P R S T W X` plus `0` (the digit zero stands for
the "th" sound).

## Install

This is a tiny compiled library: one header (`double_metaphone.h`) and one
source file (`double_metaphone.cc`). The rule set is large, so it is compiled,
not header-only. It requires C++20 and has **zero external dependencies**, only
the standard library. It does its own ASCII upper-casing, so unlike the
French/German/Spanish Soundex variants it needs no `strings` library.

With CMake `FetchContent`:

```cmake
include(FetchContent)
FetchContent_Declare(
  double_metaphone
  GIT_REPOSITORY https://github.com/Kronuz/double-metaphone.git
  GIT_TAG        main
)
FetchContent_MakeAvailable(double_metaphone)

target_link_libraries(your_target PRIVATE double_metaphone::double_metaphone)
```

The `double_metaphone` target compiles the one `.cc`, requests `cxx_std_20`, and
puts the header on your include path. Then:

```cpp
#include "double_metaphone.h"
```

## Usage

The class is shaped to drop in where a `soundex` encoder is used: the same
cached-code surface (`encode()`, `encode(word)`, `name()`, `description()`), so a
phonetic metric that calls `encode(a)` / `encode(b)` works unchanged. The key
those calls return is the **primary**; the alternate is opt-in.

```cpp
#include "double_metaphone.h"

DoubleMetaphone dm;

// encode(word) returns the PRIMARY key, the drop-in entry point.
dm.encode("Smith");            // "SM0"
dm.encode("Catherine");        // "K0RN"
dm.encode("dumb");             // "TM"   (the B is silent)

// Construct from a word, read the cached primary key back.
DoubleMetaphone("Smith").encode();   // "SM0"

// The richer API: the alternate key, or both at once.
dm.encode_alternate("Smith");  // "XMT"
dm.encode_both("Schmidt");     // {"XMT", "SMT"}

// The homophone collapse that motivates the algorithm: Smith and Schmidt
// share a key across the primary/alternate cross-product.
dm.encode_alternate("Smith") == dm.encode("Schmidt");   // both "XMT"

// Empty input stays empty on both keys.
dm.encode("");                 // ""
```

`encode()` and `encode(word)` always return the primary key. To compare words
the way the algorithm intends, two of them match if any of their keys agree
across the `{primary, alternate}` cross-product. The class also exposes
`name()` (`"dmetaphone"`) and `description()`.

## Build & test

```sh
cmake -B build && cmake --build build && ctest --test-dir build
```

No network access is needed at any step. The test pins the canonical, published
Double Metaphone keys for a set of names (`Thompson`, `Smith`, `Schmidt`,
`Catherine`, `Wagner`, `Pfister`, `Aubrey`, `dumb`), checks the `Smith` /
`Schmidt` homophone collapse and a few pairs Soundex would miss
(`Knight` / `Night`, `Wright` / `Rite`), and verifies the drop-in interface.
It prints `all double metaphone tests passed` and exits 0.

## Examples

[`examples/demo.cc`](examples/demo.cc) is a runnable tour. A top-level CMake
build produces it next to the test:

```sh
cmake -B build && cmake --build build && ./build/double_metaphone_demo
```

It prints the primary and alternate keys for a handful of names, grouped by what
each group shows off: silent letters, spelling variants that collapse, and the
primary/alternate split on foreign readings.

## License

MIT, Copyright (c) 2015-2019 Dubalu LLC. See [LICENSE](LICENSE).
See [ARCHITECTURE.md](ARCHITECTURE.md) for the design and [AGENTS.md](AGENTS.md)
for the repo map and invariants.
