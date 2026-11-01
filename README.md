# double-metaphone

Part of [Kronuz C++ Libraries](https://github.com/Kronuz/cpp-libraries), a collection of small, reusable C++ libraries.

[![CI](https://github.com/Kronuz/double-metaphone/actions/workflows/ci.yml/badge.svg)](https://github.com/Kronuz/double-metaphone/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)

Double Metaphone for C++20: two phonetic keys per word, so Smith matches Schmidt and Catherine
matches Kathryn. The primary key follows the word's main English pronunciation and the alternate
a plausible foreign one, and two words match when any of their keys agree. Reach for it in name
search and record matching across spelling variants.

## ✨ Highlights

- **Two keys per word**, primary and alternate; `encode_both` builds both in one pass.
- **Real-name rules**: silent letters, Germanic, Slavic and Romance spellings, and the many
  English spellings of one sound.
- **A drop-in for [soundex](https://github.com/Kronuz/soundex)'s encoders**: the same
  `encode()` interface, returning the primary key.
- **Allocation only for the result.** A word of up to 118 bytes is cleaned on the stack, so a
  call allocates only for the returned strings, and not at all for keys that fit the string's
  inline buffer.
- **No dependencies**: one header and one source file.

## 🚀 Quick start

```cpp
#include "double_metaphone.h"

#include <cstdio>

int main() {
	DoubleMetaphone dm;
	for (const char* name : {"Smith", "Schmidt", "Kathryn", "Catherine"}) {
		auto [primary, alternate] = dm.encode_both(name);
		std::printf("%-9s %-4s %s\n", name, primary.c_str(), alternate.c_str());
	}
	// Smith     SM0  XMT
	// Schmidt   XMT  SMT    Smith's alternate is Schmidt's primary: a match
	// Kathryn   K0RN KTRN
	// Catherine K0RN KTRN
}
```

[`examples/demo.cc`](examples/demo.cc) prints both keys for a list of names.

## 📦 Installation

Requires a C++20 compiler and CMake 3.14 or newer; it compiles one source file. CI builds it on
Linux (x86_64, arm64), macOS (arm64, x86_64), and FreeBSD, plus ASan, UBSan, and TSan builds.

```cmake
include(FetchContent)
FetchContent_Declare(double_metaphone
  GIT_REPOSITORY https://github.com/Kronuz/double-metaphone.git
  GIT_TAG        a824e1d2273e6cbba20989a18ebf82d8450863b7)  # main tracks the tip
FetchContent_MakeAvailable(double_metaphone)

target_link_libraries(my_app PRIVATE double_metaphone::double_metaphone)
```

Then `#include "double_metaphone.h"`.

## 🎯 When to use it

It fits names and words with varied spellings, mostly English or of European origin.

Not for:

- **Languages with their own phonetics** (French, German, Spanish): soundex has
  language-specific encoders.
- **Non-Latin scripts**: it works on ASCII letters.

## ⚡ Performance

Time per call from `double_metaphone_bench` (the best of 9 passes over each set, one call per
item), Apple clang 17 with `-O3`, on an Apple M4 Pro (macOS 26, arm64) and an Intel Core i9-9980HK
(macOS 15, x86-64). Each machine has three columns. `encode` is this library's call that returns
the primary key and `encode_both` its call that returns both keys. *PostgreSQL* is
[`dmetaphone.c`](https://github.com/postgres/postgres/blob/REL_17_0/contrib/fuzzystrmatch/dmetaphone.c)
from PostgreSQL `REL_17_0`, the C port of Text::DoubleMetaphone in `contrib/fuzzystrmatch`, which
the benchmark downloads and compiles for itself. It always computes both keys and stops once each
has four characters. Rows are the input: dictionary words (the first 100,000 words of
`/usr/share/dict/words`), names and phrases made from English-like syllables so that the common
rules fire, and longer texts. Lower is faster.

| Input | M4 Pro: `encode` | `encode_both` | PostgreSQL | i9-9980HK: `encode` | `encode_both` | PostgreSQL |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| dictionary word (3 to 12 letters) | 60 ns | 69 ns | 196 ns | 99 ns | 113 ns | 496 ns |
| name (4 to 9 letters) | 69 ns | 77 ns | 207 ns | 110 ns | 130 ns | 534 ns |
| phrase (40 to 80 characters) | 576 ns | 607 ns | 334 ns | 866 ns | 981 ns | 712 ns |
| text (200 to 600 characters) | 2.8 µs | 2.9 µs | 644 ns | 4.9 µs | 5.3 µs | 1.4 µs |

The two give the same keys on 98.6% of the 100,000 dictionary words (98,566; ours cut to
PostgreSQL's four characters); the benchmark prints the count for each set before it times
anything. On words and names this encoder takes 2.7x to 4.4x less time for both keys than
PostgreSQL; on phrases and texts PostgreSQL is 1.4x to 4.5x faster, because it stops at four
characters and this encoder reads the whole input. A call builds only the key it returns:
`encode_alternate` is within 5% of `encode`, and `encode_both` costs 3% to 18% more than one key.

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release -DDOUBLE_METAPHONE_BUILD_BENCH=ON \
  -DDOUBLE_METAPHONE_BENCH_REFERENCE=ON && cmake --build build
./build/double_metaphone_bench /usr/share/dict/words
```

[docs/perf/encoder-cost.md](docs/perf/encoder-cost.md) has the method and where a call spends
its time.

## 📚 Documentation

- [docs/API.md](docs/API.md): `encode`, `encode_alternate`, `encode_both`, and how to compare
  keys.
- [ARCHITECTURE.md](ARCHITECTURE.md): how the rule set is organized.
- [docs/perf/](docs/perf/): the encoder cost measurements.
- [CHANGELOG.md](CHANGELOG.md): changes with their measurements, newest first.
- [AGENTS.md](AGENTS.md): repository map and traps.
- [Catalog entry](https://github.com/Kronuz/cpp-libraries/blob/main/AGENTS.md#double-metaphone)
  in Kronuz C++ Libraries.

## 🛠️ Building and testing

```sh
cmake -B build && cmake --build build && ctest --test-dir build
./build/double_metaphone_demo
```

`ctest` compares every key with a frozen reference encoder in `test/` and checks copies and
moves. `-DDOUBLE_METAPHONE_BUILD_BENCH=ON` builds the benchmark, and `-DDOUBLE_METAPHONE_FUZZ=ON`
with Clang builds a libFuzzer target.

## 🤝 Contributing

Issues and pull requests are welcome. Keys are stored in indexes, so the output must not change.
Read [AGENTS.md](AGENTS.md) and the catalog's
[rules of engagement](https://github.com/Kronuz/cpp-libraries/blob/main/AGENTS.md#rules-of-engagement)
first; CI must pass on every platform.

## 📄 License

MIT, Copyright (c) 2026 Germán Méndez Bravo and (c) 2015-2020 Dubalu LLC. See
[LICENSE](LICENSE). The algorithm is Lawrence Philips' (C/C++ Users Journal, June 2000).
