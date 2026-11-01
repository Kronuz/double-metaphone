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

Time per call from `bench/bench.cc`, Apple clang 17 with `-O3`:

| Input | `encode`, Apple M4 Pro | `encode_both`, Apple M4 Pro | `encode`, Intel Core i9-9980HK | `encode_both`, Intel Core i9-9980HK |
| --- | ---: | ---: | ---: | ---: |
| dictionary word (3 to 12 letters) | 61 ns | 69 ns | 104 ns | 105 ns |
| name (4 to 9 letters) | 72 ns | 77 ns | 117 ns | 126 ns |
| phrase (40 to 80 characters) | 580 ns | 607 ns | 897 ns | 947 ns |
| text (200 to 600 characters) | 2.9 µs | 3.0 µs | 4.8 µs | 4.9 µs |

A call builds only the key it returns: `encode_alternate` is within 7% of `encode`, and
`encode_both` costs 1% to 13% more than one key.

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release -DDOUBLE_METAPHONE_BUILD_BENCH=ON && cmake --build build
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
