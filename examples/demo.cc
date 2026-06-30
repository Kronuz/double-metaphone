// A runnable tour of the Double Metaphone encoder.
//
// Build (when this repo is the top-level project):
//   cmake -B build && cmake --build build && ./build/double_metaphone_demo
//
// The idea worth taking away: Double Metaphone turns a word into TWO phonetic
// keys, a primary (dominant English reading) and an alternate (a plausible
// foreign reading). Two words that sound alike share a key even when their
// spellings diverge (Smith / Schmidt). It understands silent letters and
// language-specific spellings in ways Soundex never could.
#include <cstdio>
#include <string>
#include <utility>

#include "double_metaphone.h"

static void rule(const char* title) {
	std::printf("\n\033[1m-- %s --\033[0m\n", title);
}

static void show(const char* word) {
	DoubleMetaphone dm;
	auto both = dm.encode_both(word);
	std::printf("  %-12s -> primary %-6s alternate %-6s\n",
	            word, both.first.c_str(), both.second.c_str());
}

int main() {
	std::puts("double metaphone demo");

	rule("Silent letters fall away");
	show("Knight");     // silent K
	show("dumb");       // silent B
	show("Pfister");    // silent P before F
	show("Wright");     // silent W and GH

	rule("Spelling variants that sound alike collapse");
	show("Smith");
	show("Schmidt");    // Smith.alternate == Schmidt.primary
	show("Catherine");
	show("Kathryn");

	rule("Primary vs. alternate diverge on foreign readings");
	show("Wagner");     // A... (English) vs F... (Germanic)
	show("Thompson");
	show("Aubrey");

	std::puts("\ndone.");
	return 0;
}
