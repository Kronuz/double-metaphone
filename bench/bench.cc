// Benchmark: DoubleMetaphone against the frozen original (test/double_metaphone_old.*).
//
// No third-party Double Metaphone library exists for C++ in the tree, so the baseline is the
// frozen original. Each row is the best of several passes over the same words; the "A/A" column
// times the frozen original against itself and shows the noise of the run.
//
// Build: cmake -B build -DCMAKE_BUILD_TYPE=Release -DDOUBLE_METAPHONE_BUILD_BENCH=ON
//        cmake --build build --target double_metaphone_bench
// Run:   ./build/double_metaphone_bench [word-file]    (one word per line; default: generated)
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <random>
#include <string>
#include <vector>

#include "../test/double_metaphone_old.h"
#include "double_metaphone.h"

namespace {

std::vector<std::string> read_words(const char* path, std::size_t limit) {
	std::vector<std::string> words;
	std::ifstream in(path);
	for (std::string line; words.size() < limit && std::getline(in, line);) {
		if (!line.empty()) {
			words.push_back(line);
		}
	}
	return words;
}

// Pseudo-words from English-like syllables, so the common rules (CH, TH, PH, GH, ...) fire.
std::vector<std::string> generate(std::size_t n, std::size_t min_len, std::size_t max_len, unsigned seed) {
	static const char* const syl[] = {
		"ch", "th", "ph", "gh", "sh", "ck", "tion", "sch", "wr", "kn", "ough", "ce", "ci",
		"a", "e", "i", "o", "u", "y", "b", "d", "f", "g", "h", "j", "k", "l", "m", "n",
		"p", "r", "s", "t", "v", "w", "x", "z", "ll", "ss", "mb", "ign", "ia", "io", "z",
	};
	constexpr std::size_t count = sizeof(syl) / sizeof(*syl);
	std::mt19937_64 rng(seed);
	std::vector<std::string> words(n);
	for (auto& w : words) {
		auto len = min_len + rng() % (max_len - min_len + 1);
		while (w.size() < len) {
			w += syl[rng() % count];
		}
		if (rng() % 4 == 0) {
			w[0] = static_cast<char>(w[0] - 'a' + 'A');
		}
	}
	return words;
}

// Several words joined with spaces, 40 to 80 characters.
std::vector<std::string> generate_phrases(std::size_t n, unsigned seed) {
	auto words = generate(n * 8, 3, 9, seed);
	std::mt19937_64 rng(seed + 1);
	std::vector<std::string> out(n);
	std::size_t at = 0;
	for (auto& s : out) {
		auto len = 40 + rng() % 41;
		while (s.size() < len) {
			if (!s.empty()) {
				s += ' ';
			}
			s += words[at++ % words.size()];
		}
	}
	return out;
}

volatile std::size_t sink = 0;

template <typename Fn>
double best_ns(const std::vector<std::string>& words, Fn&& fn, int reps = 9) {
	double best = 1e300;
	for (int rep = 0; rep < reps; ++rep) {
		auto t0 = std::chrono::steady_clock::now();
		std::size_t s = 0;
		for (const auto& w : words) {
			s += fn(w);
		}
		auto t1 = std::chrono::steady_clock::now();
		sink = sink + s;
		best = std::min(best, std::chrono::duration<double, std::nano>(t1 - t0).count() / static_cast<double>(words.size()));
	}
	return best;
}

void row(const char* set, const char* op, const std::vector<std::string>& words,
         std::size_t (*old_fn)(const std::string&), std::size_t (*new_fn)(const std::string&)) {
	// Interleave old, new and old again so drift shows up in the A/A spread.
	double a1 = best_ns(words, old_fn);
	double b = best_ns(words, new_fn);
	double a2 = best_ns(words, old_fn);
	double a = std::min(a1, a2);
	std::printf("%-8s %-19s old %8.1f ns  new %8.1f ns  x%5.2f   A/A %4.1f%%\n", set, op, a, b, a / b,
	            100.0 * std::abs(a1 - a2) / a);
}

const DoubleMetaphoneOld old_dm;
const DoubleMetaphone new_dm;

std::size_t old_primary(const std::string& w) { return old_dm.encode(w).size(); }
std::size_t new_primary(const std::string& w) { return new_dm.encode(w).size(); }
std::size_t old_alternate(const std::string& w) { return old_dm.encode_alternate(w).size(); }
std::size_t new_alternate(const std::string& w) { return new_dm.encode_alternate(w).size(); }
std::size_t old_both(const std::string& w) { auto p = old_dm.encode_both(w); return p.first.size() + p.second.size(); }
std::size_t new_both(const std::string& w) { auto p = new_dm.encode_both(w); return p.first.size() + p.second.size(); }
std::size_t old_ctor(const std::string& w) { return DoubleMetaphoneOld(w).encode().size(); }
std::size_t new_ctor(const std::string& w) { return DoubleMetaphone(w).encode().size(); }

void rows(const char* set, const std::vector<std::string>& words) {
	row(set, "encode", words, old_primary, new_primary);
	row(set, "encode_alternate", words, old_alternate, new_alternate);
	row(set, "encode_both", words, old_both, new_both);
	row(set, "construct+encode()", words, old_ctor, new_ctor);
}

} // namespace

int main(int argc, char** argv) {
	std::vector<std::string> dict;
	if (argc > 1) {
		dict = read_words(argv[1], 100000);
	}
	if (dict.empty()) {
		dict = generate(100000, 3, 12, 7);
	}
	auto names = generate(2000, 4, 9, 11);
	auto phrases = generate_phrases(20000, 13);
	auto longs = generate(200, 200, 600, 17);

	std::printf("double_metaphone bench: %zu dictionary words, %zu names, %zu phrases, %zu long\n",
	            dict.size(), names.size(), phrases.size(), longs.size());
	rows("words", dict);
	rows("names", names);
	rows("phrases", phrases);
	rows("long", longs);
	return 0;
}
