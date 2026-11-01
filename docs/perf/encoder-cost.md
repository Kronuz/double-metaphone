# Encoder cost: one key per call, one cleaning pass

Measured 2026-10-31 at `double-metaphone:82ee696`.

## Change

`encode(word)` builds only the primary key and `encode_alternate(word)` only the alternate. `encode_both(word)` builds both in one pass. The three run the same walk, a template over which keys to build: the walk decides every step from the word and never from a key, so a single-key walk takes the same steps and skips every append to the other key.

The word is cleaned in one pass through a 256-entry table into a stack buffer (a heap buffer above 118 bytes), between four blanks in front and six behind, so a look-behind or look-ahead of any rule is a plain load. The same pass notes the Slavo-Germanic spellings (`W`, `K`, `CZ`, `WITZ`). The keys are written into fixed buffers and copied once into the returned strings. `match_at` compares against a literal of compile-time size.

Output is identical to the frozen encoder (`test/double_metaphone_old.cc`, the encoder at `double-metaphone:91c17a2`) for every input.

## Method

- Bench: `bench/bench.cc`, target `double_metaphone_bench` (`-DDOUBLE_METAPHONE_BUILD_BENCH=ON`), `./build/double_metaphone_bench /usr/share/dict/words`. The frozen encoder is compiled into the bench.
- Inputs: the first 100,000 words of the dictionary file given on the command line (pseudo-words of 3 to 12 letters when none is given), 2,000 names (4 to 9 letters), 20,000 phrases (40 to 80 characters) and 200 texts (200 to 600 characters). The names, phrases and texts are built from English-like syllables so that the common rules (`CH`, `TH`, `PH`, `GH`, `TION`, `SCH`) fire.
- Each figure is the best of 9 passes over the set, one call per item. Each row times the frozen encoder, the library, and the frozen encoder again ("A/A"); the frozen encoder run against itself differs by 0.0% to 3.1% on the Apple M4 Pro and 0.0% to 5.2% on the Intel Core i9-9980HK.
- Machines: Apple M4 Pro and Intel Core i9-9980HK (macOS 15, x86_64). Apple clang 17, `-O3`, with the machine to itself.

## Results

Apple M4 Pro, ns per call:

| input | call | before | after | speedup |
|---|---|---:|---:|---:|
| 100,000 dictionary words | `encode` | 132.2 | 60.8 | **2.18x** |
| | `encode_alternate` | 133.8 | 61.4 | 2.18x |
| | `encode_both` | 134.3 | 68.9 | 1.95x |
| 2,000 names | `encode` | 128.2 | 72.4 | 1.77x |
| | `encode_alternate` | 126.2 | 68.6 | 1.84x |
| | `encode_both` | 122.8 | 77.4 | 1.59x |
| 20,000 phrases | `encode` | 1021.5 | 580.0 | 1.76x |
| | `encode_alternate` | 1022.9 | 590.4 | 1.73x |
| | `encode_both` | 1021.1 | 606.9 | 1.68x |
| 200 texts | `encode` | 5633.3 | 2878.1 | 1.96x |
| | `encode_alternate` | 5661.0 | 3127.9 | 1.81x |
| | `encode_both` | 5683.5 | 3029.8 | 1.88x |

Intel Core i9-9980HK, ns per call, before and after (speedup):

| input | `encode` | `encode_alternate` | `encode_both` |
|---|---:|---:|---:|
| dictionary words | 258.5, 103.8 (**2.49x**) | 257.5, 102.8 (2.51x) | 255.8, 105.0 (2.44x) |
| names | 239.4, 116.8 (2.05x) | 238.6, 117.4 (2.03x) | 240.0, 125.9 (1.91x) |
| phrases | 2049.0, 896.7 (2.29x) | 2125.4, 907.8 (2.34x) | 2060.2, 946.5 (2.18x) |
| texts | 10521.8, 4764.4 (2.21x) | 10615.9, 4788.4 (2.22x) | 10821.8, 4910.9 (2.20x) |

Constructing an encoder from a word and reading `encode()` costs the same as `encode(word)` (2.20x, 1.78x, 1.74x and 1.98x on the four sets, M4 Pro).

### Where a call goes

A dictionary word takes 61 ns on the M4 Pro. A probe build that stops after cleaning the word takes 13 ns, the rule walk about 37 ns, and copying the key into the returned string about 7 ns. The frozen encoder's 132 ns went to four things around the rules: copying the word into a `std::string`; a second pass over it for the Slavo-Germanic spellings, with a bounds-clamping substring compare at every position; two `std::string` keys appended to one sound class at a time with a length known only at run time; and, in `encode()`, a second key built and dropped.

## Proof

- `test/diff_test.cc` (CTest `double_metaphone_diff`, 7M inputs) compares `encode`, `encode_alternate` and `encode_both` with the frozen encoder on every string up to four characters of the letters and a space, longer strings over reduced alphabets built around the rule triggers, the dictionary, and random bytes, text and syllable strings with lengths around every buffer boundary.
- `tools/mutants.py` breaks the encoder's primitives in 19 ways and requires the differential test to fail on each. A mutant whose test does not finish in two minutes counts as killed. The blank in front of the first letter already stops a leading space from being kept, so the code has no separate test for it and the list has no mutant for it.
- `fuzz/fuzz_encode.cc` (`-DDOUBLE_METAPHONE_FUZZ=ON`, clang) runs the same comparison on arbitrary bytes under libFuzzer, ASan and UBSan.
- `test/copy_move.cc` copies and moves an encoder every way, non-const lvalues included.

## Decision and open items

- Over the 235,976 words of `/usr/share/dict/words`, the keys differ from those of the Python port `Metaphone` (PyPI) on 4,945 words (2.1%): 2,991 with `G` before `E`, `I` or `Y` in the middle of a word, 1,391 with `TIO`, `TIA` or `CIA`, 111 with `UMB`, 109 with `CH`, 92 with `GH` or `GN` and 251 with other `G` rules. [ARCHITECTURE.md](../../ARCHITECTURE.md) lists the classes. The keys are stored in indexes, so each class is a decision for the owner, and the library keeps its keys.
- The padding must be as wide as the widest look-behind or look-ahead of any rule; an `assert` in `Word::at` checks it in Debug and sanitizer builds.
- What is left is the rule walk, 37 of the 61 ns. A further cut has to change how the walk dispatches on each letter. It is not attempted.
