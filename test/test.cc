// Smoke test for the standalone Double Metaphone encoder.
//
// Double Metaphone (Lawrence Philips, 2000) is a modern, better-than-Soundex
// phonetic encoder. It emits TWO keys per word: a PRIMARY (dominant English
// reading) and an ALTERNATE (a plausible foreign reading). The keys use a small
// sound alphabet (A B F H J K L M N P R S T W X and '0' for the "th" sound) and
// are not truncated.
//
// The expected keys below are the canonical, widely-published Double Metaphone
// results for these words. They are the reference: if the implementation
// disagreed with one of them, the implementation was fixed, not the test.
//
// Build via CMake: cmake -B build && cmake --build build && ctest --test-dir build
#include <cassert>
#include <cstdio>
#include <string>
#include <utility>

#include "double_metaphone.h"


static int failures = 0;

// Assert the PRIMARY key for `word`. Prints the word and both keys on failure.
static void check_primary(const char* word, const std::string& want) {
	DoubleMetaphone dm;
	std::string got = dm.encode(word);
	if (got != want) {
		std::printf("FAIL primary   %-12s got '%s' want '%s'\n", word, got.c_str(), want.c_str());
		++failures;
		return;
	}
	std::printf("  ok primary   %-12s -> %s\n", word, got.c_str());
}

// Assert the ALTERNATE key for `word`.
static void check_alternate(const char* word, const std::string& want) {
	DoubleMetaphone dm;
	std::string got = dm.encode_alternate(word);
	if (got != want) {
		std::printf("FAIL alternate %-12s got '%s' want '%s'\n", word, got.c_str(), want.c_str());
		++failures;
		return;
	}
	std::printf("  ok alternate %-12s -> %s\n", word, got.c_str());
}

// Assert BOTH keys for `word` in one shot.
static void check_both(const char* word, const std::string& wp, const std::string& wa) {
	check_primary(word, wp);
	check_alternate(word, wa);
}


// ---------------------------------------------------------------------------
// Documented vectors. These are the canonical Double Metaphone results.
// ---------------------------------------------------------------------------

static void test_documented_vectors() {
	std::printf("-- documented vectors --\n");

	// "Thompson": TH -> T (the Thom-/Thom-as special case), then M-P-S-N. The P is
	// pronounced (standard Double Metaphone has no MP-silencing rule), so TMPSN.
	check_primary("Thompson", "TMPSN");

	// "Smith" / "Schmidt": the homophone pair that motivates the algorithm. They
	// share a key across the primary/alternate cross-product.
	check_both("Smith",   "SM0", "XMT");
	check_both("Schmidt", "XMT", "SMT");

	// "Catherine": TH -> '0' primary, T alternate.
	check_both("Catherine", "K0RN", "KTRN");

	// "Wagner": initial W reads A (English) / F (Germanic).
	check_both("Wagner", "AKNR", "FKNR");

	// "Pfister": P is pronounced before F (standard DM only special-cases PH), so
	// P-F-S-T-R -> PFSTR.
	check_both("Pfister", "PFSTR", "PFSTR");

	// "Aubrey": no divergence.
	check_both("Aubrey", "APR", "APR");

	// "dumb": the trailing B is silent.
	check_primary("dumb", "TM");
}


// ---------------------------------------------------------------------------
// Homophone collapse: the whole point of the algorithm. Words that sound alike
// must share a key somewhere across the {primary, alternate} cross-product.
// ---------------------------------------------------------------------------

// Do the two words share any key across the {primary, alternate} cross-product?
static bool collapses(const std::string& a, const std::string& b) {
	DoubleMetaphone dm;
	auto ka = dm.encode_both(a);
	auto kb = dm.encode_both(b);
	auto eq = [](const std::string& x, const std::string& y) {
		return !x.empty() && x == y;
	};
	return eq(ka.first, kb.first) || eq(ka.first, kb.second) ||
	       eq(ka.second, kb.first) || eq(ka.second, kb.second);
}

static void test_homophones() {
	std::printf("-- homophone collapse --\n");

	// The motivating pair: Smith.alternate "XMT" == Schmidt.primary "XMT".
	DoubleMetaphone dm;
	assert(dm.encode_alternate("Smith") == dm.encode("Schmidt"));
	assert(collapses("Smith", "Schmidt"));
	std::printf("  ok Smith / Schmidt collapse (XMT shared)\n");

	// Pairs that Soundex would miss but Double Metaphone catches, because the
	// leading consonant clusters differ (silent letters / spelling variants):
	//   - "Knight" / "Night":   the silent K is dropped, both read NT.
	//   - "Wright" / "Rite":    the silent W/GH drop, both read RT.
	//   - "Catherine"/"Kathryn": TH/T and vowel spellings differ.
	struct Pair { const char* a; const char* b; };
	const Pair pairs[] = {
		{"Knight", "Night"},
		{"Wright", "Rite"},
		{"Catherine", "Kathryn"},
		{"Smith", "Smyth"},
	};
	for (const auto& p : pairs) {
		if (!collapses(p.a, p.b)) {
			std::printf("FAIL homophone %s / %s do not share a key\n", p.a, p.b);
			++failures;
		} else {
			std::printf("  ok %s / %s collapse\n", p.a, p.b);
		}
	}
}


// ---------------------------------------------------------------------------
// Interface surface: the drop-in shape that mirrors the soundex encoders.
// ---------------------------------------------------------------------------

static void test_interface() {
	std::printf("-- interface --\n");

	// Construct-from-word caches the PRIMARY key; encode() reads it back.
	assert(DoubleMetaphone("Smith").encode() == "SM0");

	// encode(word) on an instance encodes a fresh word (the primary key).
	DoubleMetaphone dm;
	assert(dm.encode("Smith") == "SM0");

	// encode_both returns {primary, alternate}.
	auto both = dm.encode_both("Schmidt");
	assert(both.first == "XMT" && both.second == "SMT");

	// Empty input stays empty on both keys.
	assert(dm.encode("") == "");
	assert(dm.encode_alternate("") == "");

	// name()/description().
	assert(dm.name() == "dmetaphone");
	assert(!dm.description().empty());

	std::printf("  ok interface surface\n");
}


int main() {
	test_documented_vectors();
	test_homophones();
	test_interface();

	if (failures != 0) {
		std::printf("\n%d double metaphone test(s) FAILED\n", failures);
		return 1;
	}
	std::printf("\nall double metaphone tests passed\n");
	return 0;
}
