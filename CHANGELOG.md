# Changelog

Notable changes to double-metaphone, newest first. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/); entries are dated because the
library has no releases.

## 2026-11-01

### Added

- `bench/bench.cc` times [PostgreSQL](https://github.com/postgres/postgres)'s `dmetaphone.c`
  (`REL_17_0`, `contrib/fuzzystrmatch`, a C port of Text::DoubleMetaphone) beside the encoder,
  fetched as one file and built only with `-DDOUBLE_METAPHONE_BENCH_REFERENCE=ON`. It first
  counts the words on which the two give the same keys, ours cut to PostgreSQL's four
  characters.

- `docs/perf/encoder-cost.md`: the method, the numbers on two machines, where a call goes, and the open items.

## 2026-10-31

### Fixed

- A copy of a `DoubleMetaphone` from a non-const lvalue (`DoubleMetaphone b = a;`) or from
  a const rvalue did not compile: the forwarding constructor that takes a word is a better
  match than the copy constructor there, and it tried to encode the encoder. It also broke
  `vector::emplace_back(a)`, `std::make_unique<DoubleMetaphone>(a)` and
  `std::optional<DoubleMetaphone>(a)` with a non-const `a`. The constructor now excludes
  `DoubleMetaphone`, its derived classes and their cv-ref forms. Keys are unchanged.
  `test/copy_move.cc` (`double_metaphone_copy`) covers it.

### Changed

- `encode()` builds only the primary key and `encode_alternate()` only the alternate;
  `encode_both()` still builds both in one pass. Before, every call built both keys and
  dropped the one it did not return. Output is identical. (`82ee696`)
- The encoder cleans the word in one pass through a 256-entry table into a stack buffer
  (a heap buffer above 118 bytes), between four blanks in front and six behind, notes the
  Slavo-Germanic spellings (`W`, `K`, `CZ`, `WITZ`) in that pass, and writes the keys into
  fixed buffers that are copied once into the returned strings. Before, it copied the word
  into a `std::string`, scanned it again for those spellings with a bounds-clamping
  substring compare at every position, and appended every sound class to two `std::string`s
  with a length known only at run time. Output is identical. (`82ee696`)

Time per call on an Apple M4 Pro (Apple clang 17, `-O3`), from `bench/bench.cc` with the machine
to itself. Before is the frozen encoder in `test/double_metaphone_old.cc`; each figure is
the best of 9 passes, and the frozen encoder run against itself differs by 0.0% to 3.1%:

| input | call | before | after | speedup |
| --- | --- | ---: | ---: | ---: |
| 100,000 dictionary words | `encode` | 132.2 ns | 60.8 ns | 2.18x |
| | `encode_alternate` | 133.8 ns | 61.4 ns | 2.18x |
| | `encode_both` | 134.3 ns | 68.9 ns | 1.95x |
| 2,000 names | `encode` | 128.2 ns | 72.4 ns | 1.77x |
| | `encode_alternate` | 126.2 ns | 68.6 ns | 1.84x |
| | `encode_both` | 122.8 ns | 77.4 ns | 1.59x |
| 20,000 phrases (40 to 80 characters) | `encode` | 1021.5 ns | 580.0 ns | 1.76x |
| | `encode_alternate` | 1022.9 ns | 590.4 ns | 1.73x |
| | `encode_both` | 1021.1 ns | 606.9 ns | 1.68x |
| 200 texts (200 to 600 characters) | `encode` | 5633.3 ns | 2878.1 ns | 1.96x |
| | `encode_alternate` | 5661.0 ns | 3127.9 ns | 1.81x |
| | `encode_both` | 5683.5 ns | 3029.8 ns | 1.88x |

Constructing an encoder from a word and reading `encode()` costs the same as `encode(word)`
(2.20x, 1.78x, 1.74x and 1.98x on the four sets). On a dictionary word, a probe build that stops
after cleaning the word takes 13 ns of the 61 ns; the rule walk takes about 37 ns and copying
the key into the returned string about 7 ns.

The same bench on an Intel i9-9980HK (Apple clang 17, `-O3`, machine to itself; the frozen encoder
run against itself differs by 0.0% to 5.2%), before and after in ns per call, with the speedup:

| input | `encode` | `encode_alternate` | `encode_both` |
| --- | ---: | ---: | ---: |
| dictionary words | 258.5, 103.8 (2.49x) | 257.5, 102.8 (2.51x) | 255.8, 105.0 (2.44x) |
| names | 239.4, 116.8 (2.05x) | 238.6, 117.4 (2.03x) | 240.0, 125.9 (1.91x) |
| phrases | 2049.0, 896.7 (2.29x) | 2125.4, 907.8 (2.34x) | 2060.2, 946.5 (2.18x) |
| texts | 10521.8, 4764.4 (2.21x) | 10615.9, 4788.4 (2.22x) | 10821.8, 4910.9 (2.20x) |

### Added

- `test/diff_test.cc` compares `encode`, `encode_alternate` and `encode_both` with the frozen
  encoder on every string up to four characters of the letters and a space, longer strings over
  reduced alphabets built around the rule triggers, the dictionary, and random bytes, text and
  syllable strings with lengths around every buffer boundary (7M inputs, CTest
  `double_metaphone_diff`).
- `tools/mutants.py` breaks the encoder's primitives in 19 ways; the differential test has to
  fail on each.
- `fuzz/fuzz_encode.cc`, a libFuzzer target of the same comparison (`DOUBLE_METAPHONE_FUZZ`).
- `bench/bench.cc` (`DOUBLE_METAPHONE_BUILD_BENCH`) times all four calls against the frozen
  encoder.
