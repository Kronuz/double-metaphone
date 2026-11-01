// Differential test: DoubleMetaphone against the frozen original (double_metaphone_old.*).
//
// Every key of every input must equal the original's: encode_both, encode_alternate, encode and
// the cached key of the constructor. Inputs: every string up to four characters over the letters
// and space, every string up to a longer length over reduced alphabets built around the rule
// triggers, dictionary words, and random text (bytes, mixed case, punctuation, syllables, long).
//
// Run: double_metaphone_diff_test [--stress N]     (N times as many random inputs)
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

#include "double_metaphone.h"
#include "double_metaphone_old.h"

namespace {

const DoubleMetaphone new_dm;
const DoubleMetaphoneOld old_dm;

std::size_t checked = 0;
std::size_t failures = 0;

void fail(std::string_view in, const char* what, const std::string& want, const std::string& got) {
	if (++failures <= 20) {
		std::string shown;
		for (unsigned char c : in.substr(0, 64)) {
			char buf[8];
			if (c >= 0x20 && c < 0x7f && c != '\\') {
				shown += static_cast<char>(c);
			} else {
				std::snprintf(buf, sizeof buf, "\\x%02x", c);
				shown += buf;
			}
		}
		std::printf("FAIL %s of \"%s\" (%zu bytes): want '%s' got '%s'\n", what, shown.c_str(), in.size(),
		            want.c_str(), got.c_str());
	}
}

void check(std::string_view in) {
	++checked;
	auto want = old_dm.encode_both(in);
	auto both = new_dm.encode_both(in);
	if (both.first != want.first) {
		fail(in, "encode_both.first", want.first, both.first);
	}
	if (both.second != want.second) {
		fail(in, "encode_both.second", want.second, both.second);
	}
	auto primary = new_dm.encode(in);
	if (primary != want.first) {
		fail(in, "encode", want.first, primary);
	}
	auto alternate = new_dm.encode_alternate(in);
	if (alternate != want.second) {
		fail(in, "encode_alternate", want.second, alternate);
	}
}

struct Rng {
	std::uint64_t s;
	std::uint64_t operator()() {
		std::uint64_t z = (s += 0x9e3779b97f4a7c15ULL);
		z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
		z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
		return z ^ (z >> 31);
	}
	std::size_t below(std::size_t n) { return static_cast<std::size_t>((*this)() % n); }
};

// Every string of length 0..max_len over `alphabet`.
void exhaustive(std::string_view alphabet, std::size_t max_len) {
	std::string s;
	std::vector<std::size_t> digit;
	for (std::size_t len = 0; len <= max_len; ++len) {
		s.assign(len, alphabet[0]);
		digit.assign(len, 0);
		for (;;) {
			check(s);
			std::size_t k = 0;
			while (k < len && ++digit[k] == alphabet.size()) {
				digit[k] = 0;
				s[k] = alphabet[0];
				++k;
			}
			if (k == len) {
				break;
			}
			s[k] = alphabet[digit[k]];
		}
	}
}

void test_exhaustive() {
	// Every string up to four characters over the letters and the space.
	exhaustive("ABCDEFGHIJKLMNOPQRSTUVWXYZ ", 4);
	// Longer strings over the letters that the rules look at, in reduced alphabets.
	exhaustive("CGHKSTWX ", 6);
	exhaustive("AEIOUYCGHJ ", 5);
	exhaustive("LMNPQRVZBDF ", 5);
	exhaustive("SCHAEIZ", 7);
	exhaustive("AEIUMBLGJOWRS", 5);
	exhaustive("TCHIAOXZ", 7);
	exhaustive("WITZKCVANOS ", 5);
	std::printf("exhaustive: %zu inputs\n", checked);
}

void test_edges() {
	static const char* const words[] = {
		"", " ", "  ", "\t", " a ", "a", "A", "x", "X", "s", "ch", "CH", "Thompson", "Smith", "Schmidt",
		"Catherine", "Kathryn", "Wagner", "Pfister", "Aubrey", "dumb", "Knight", "Night", "Wright", "Rite",
		"Smyth", "Xavier", "Czechoslovakia", "Filipowicz", "Jankelowicz", "San Jose", "SAN", "san ", "VAN ",
		"von Neumann", "Van Gogh", "Mac Caffrey", "McHugh", "Mac Gregor", "Michael", "Chianti", "Chorus",
		"architect", "orchestra", "orchid", "Bacchus", "Bellocchio", "Accident", "Succeed", "Focaccia",
		"Edge", "Wedgwood", "Pittsburgh", "Laugh", "Hugh", "Bough", "Ghislane", "Ghost", "Tagliaro", "Gertrude",
		"Gibson", "Ginger", "Bajador", "Raj", "Taj", "Cabrillo", "Gallegos", "Thumb", "Plumber", "Bomb", "Pizza",
		"Mezzo", "Zhao", "Sugar", "Shoe", "Asia", "Vision", "Island", "Carlisle", "Schneider", "School", "Scheme",
		"Schenker", "Arnow", "Arnoff", "Rogier", "Hochmeier", "Dubois", "Beaux", "Breaux", "Beauxs",
		"Caesar", "Brown", "Brough", "Wasserman", "Vasserman", "Womo", "Uomo", "Jose", "Joseph", "Jjose",
		"Czerny", "Wicz", "Bacher", "Macher", "Mc Caffrey", "ACHE", "ACHI", "Bachi", "Wachtler", "Weschsler",
		"Psychology", "Gnome", "Wrack", "Pneumonia", "Xerox", "Aux", "Eaux", "Iaux", "Tchaikovsky", "Mattia",
		"Thomas", "Thames", "Matthew", "Ghent", "Maghi", "Aghi", "Ugh", "Light", "Bight",
		"hello world  this   has \t odd   spacing ", "  leading and trailing  ", "UPPER lower MiXeD",
		"caf\xc3\xa9 cr\xc3\xa8me", "na\xc3\xafve", "tab\tsep\tword", "digits 123 and 456", "a-b_c.d,e;f",
		"emb\0edded", "\xff\xfe\x80\x81", "z\0", "\0\0\0",
	};
	for (const char* w : words) {
		check(w);
	}
	check(std::string_view("emb\0edded", 9));
	check(std::string_view("\0\0\0", 3));
	check(std::string_view("z\0", 2));
	// Constructors: from std::string, const char*, std::string_view, and an rvalue.
	for (const char* w : {"Smith", "Schmidt", "Xavier", "", "Thompson"}) {
		auto want = old_dm.encode(w);
		std::string s(w);
		if (DoubleMetaphone(s).encode() != want || DoubleMetaphone(w).encode() != want ||
		    DoubleMetaphone(std::string_view(w)).encode() != want || DoubleMetaphone(std::string(w)).encode() != want) {
			fail(w, "constructor", want, DoubleMetaphone(w).encode());
		}
		++checked;
	}
	std::printf("edges: %zu inputs so far\n", checked);
}

void test_random(std::size_t scale) {
	Rng rng{0x5eed};
	// Random bytes of any value.
	for (std::size_t n = 0; n < 200000 * scale; ++n) {
		std::string s(rng.below(80), '\0');
		for (auto& c : s) {
			c = static_cast<char>(rng());
		}
		check(s);
	}
	// Letters in both cases, spaces, tabs and punctuation.
	static const char alpha[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ  \t.-'1";
	for (std::size_t n = 0; n < 400000 * scale; ++n) {
		std::string s(rng.below(40), '\0');
		for (auto& c : s) {
			c = alpha[rng.below(sizeof alpha - 1)];
		}
		check(s);
	}
	// Syllables that trigger the rules, so the deep branches are reached often.
	static const char* const syl[] = {
		"ch", "th", "ph", "gh", "sh", "ck", "tion", "sch", "wr", "kn", "ough", "ce", "ci", "cia", "cc", "cz",
		"a", "e", "i", "o", "u", "y", "b", "d", "f", "g", "h", "j", "k", "l", "m", "n", "p", "q", "r", "s", "t",
		"v", "w", "x", "z", "ll", "ss", "mb", "ign", "gn", "ia", "io", "witz", "wicz", "zz", "dg", "tch", "mc",
		" ", "van ", "von ", "san ", "ier", "ski", "sian", "isl", "ugh", "aux", "ach", "bach", "cha", "chi",
	};
	constexpr std::size_t count = sizeof(syl) / sizeof(*syl);
	for (std::size_t n = 0; n < 600000 * scale; ++n) {
		std::string s;
		auto parts = rng.below(10);
		for (std::size_t k = 0; k < parts; ++k) {
			s += syl[rng.below(count)];
		}
		if (rng.below(4) == 0 && !s.empty()) {
			s[0] = static_cast<char>(s[0] - 'a' + 'A');
		}
		check(s);
	}
	// Long inputs: past every small-buffer size.
	for (std::size_t n = 0; n < 60 * scale; ++n) {
		std::string s;
		auto target = 100 + rng.below(70000);
		while (s.size() < target) {
			s += syl[rng.below(count)];
		}
		check(s);
	}
	for (std::size_t len : {31u, 32u, 33u, 63u, 64u, 65u, 83u, 84u, 85u, 86u, 87u, 116u, 117u, 118u, 119u, 120u, 121u, 127u, 128u,
	                        129u, 255u, 256u, 257u, 1023u, 1024u, 1025u}) {
		for (std::size_t n = 0; n < 200 * scale; ++n) {
			std::string s(len, '\0');
			for (auto& c : s) {
				c = alpha[rng.below(sizeof alpha - 1)];
			}
			check(s);
			std::string t;
			while (t.size() < len) {
				t += syl[rng.below(count)];
			}
			t.resize(len);
			check(t);
		}
	}
	std::printf("random: %zu inputs so far\n", checked);
}

void test_words_file() {
	const char* path = std::getenv("DOUBLE_METAPHONE_WORDS");
	if (path == nullptr) {
		path = "/usr/share/dict/words";
	}
	std::ifstream in(path);
	if (!in) {
		std::printf("words: no file at %s, skipped\n", path);
		return;
	}
	std::size_t before = checked;
	for (std::string line; std::getline(in, line);) {
		check(line);
	}
	std::printf("words: %zu words from %s\n", checked - before, path);
}

} // namespace

int main(int argc, char** argv) {
	std::size_t scale = 1;
	for (int i = 1; i + 1 < argc; ++i) {
		if (std::strcmp(argv[i], "--stress") == 0) {
			scale = static_cast<std::size_t>(std::strtoull(argv[i + 1], nullptr, 10));
		}
	}
	test_edges();
	test_exhaustive();
	test_random(scale);
	test_words_file();
	if (failures != 0) {
		std::printf("%zu of %zu inputs DIFFER from the frozen original\n", failures, checked);
		return 1;
	}
	std::printf("all double metaphone differential tests passed (%zu inputs)\n", checked);
	return 0;
}
