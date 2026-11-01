/*
 * Copyright © 2026 Germán Méndez Bravo (Kronuz)
 * Copyright (c) 2015-2019 Dubalu LLC
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

// Double Metaphone (Lawrence Philips, C/C++ Users Journal, June 2000).
//
// Implemented fresh from the published algorithm: this walks the upper-cased,
// letters-only word one position at a time, and at each position a switch over
// the current letter decides what sound class(es) to append and how many
// letters to skip. Two keys are built in lockstep: the primary captures the
// dominant English reading, the alternate captures a plausible foreign reading.
// They only diverge where the rules call for it; most letters push the same
// thing onto both.
//
// The code below favors being readable against the original rule descriptions
// over being clever. Every non-obvious branch carries a short note on which
// pronunciation pattern it is catching (e.g. why CH is 'K' in "character" but
// 'X' in "chair", why a leading "WR"/"KN"/"GN"/"PN"/"PS" loses its first
// letter). Nothing here reaches outside the standard library.

#include "double_metaphone.h"

#include <cassert>                                // for assert
#include <cstddef>                                // for std::size_t, std::ptrdiff_t
#include <cstring>                                // for std::memcmp, std::memcpy, std::memset
#include <memory>                                 // for std::unique_ptr
#include <string>                                 // for std::string
#include <string_view>                            // for std::string_view
#include <utility>                                // for std::pair


namespace {

// How each input byte is cleaned: a letter maps to its upper-case form, a space
// or a tab maps to ' ', every other byte (digits, punctuation, UTF-8 bytes) to 0.
struct CleanTable {
	char map[256];
	constexpr CleanTable() : map() {
		for (int c = 0; c < 256; ++c) {
			map[c] = 0;
		}
		for (int c = 'A'; c <= 'Z'; ++c) {
			map[c] = static_cast<char>(c);
			map[c + ('a' - 'A')] = static_cast<char>(c);
		}
		map[' '] = ' ';
		map['\t'] = ' ';
	}
};
constexpr CleanTable clean_table;

// The vowels A E I O U Y.
struct VowelTable {
	bool map[256];
	constexpr VowelTable() : map() {
		for (char c : {'A', 'E', 'I', 'O', 'U', 'Y'}) {
			map[static_cast<unsigned char>(c)] = true;
		}
	}
};
constexpr VowelTable vowel_table;


// A cursor over the cleaned word: upper-case letters and single interior
// spaces, held between blank padding (four blanks in front, six behind), so
// look-ahead and look-behind like at(i - 4) or at(i + 3) never need a bounds
// check and read as a non-letter. Indices are relative to the first letter,
// which is what the published rules mean by "position 0". Words of up to
// 118 bytes are cleaned on the stack.
class Word {
	static constexpr std::size_t lead = 4;
	static constexpr std::size_t trail = 6;
	static constexpr std::size_t stack_size = 128;

	char stack_[stack_size];
	std::unique_ptr<char[]> heap_;
	char* first_;       // the first letter; blanks sit in the lead bytes before it
	std::size_t len_;   // number of letters and interior spaces (padding excluded)
	bool slavo_ = false;

public:
	explicit Word(std::string_view in) {
		char* base = stack_;
		if (in.size() + lead + trail > stack_size) {
			heap_.reset(new char[in.size() + lead + trail]);
			base = heap_.get();
		}
		first_ = base + lead;
		std::memset(base, ' ', lead);
		char* out = first_;
		bool slavo = false;
		// Upper-case the letters and keep single interior spaces; bytes >= 0x80
		// from UTF-8 input are dropped. A few rules key off word boundaries in
		// multi-word names ("SAN ", "VAN ", "VON "), so the spaces stay. The
		// same pass notes the Slavo-Germanic spellings: a W, a K or "CZ".
		for (unsigned char c : in) {
			char m = clean_table.map[c];
			if (m > ' ') {
				slavo |= m == 'W' || m == 'K' || (m == 'Z' && out[-1] == 'C');
				*out++ = m;
			} else if (m == ' ' && out[-1] != ' ') {
				// The blanks in front of the first letter also drop leading spaces.
				*out++ = ' ';
			}
		}
		// Trim a trailing space the collapsing may have left.
		while (out != first_ && out[-1] == ' ') {
			--out;
		}
		len_ = static_cast<std::size_t>(out - first_);
		slavo_ = slavo;
		std::memset(out, ' ', trail);
	}

	std::size_t length() const { return len_; }

	// Does the word contain a W, a K or "CZ" (which includes "WITZ")?
	bool slavo_germanic() const { return slavo_; }

	// Character at index `i`, which may run four before the first letter and
	// five past the last: those read as a blank, never as a letter.
	char at(std::ptrdiff_t i) const {
		assert(i >= -static_cast<std::ptrdiff_t>(lead) &&
		       i < static_cast<std::ptrdiff_t>(len_ + trail));
		return first_[i];
	}

	// Does `what` (a string literal) sit entirely inside the word at `start`?
	template <std::size_t N>
	bool match_at(std::ptrdiff_t start, const char (&what)[N]) const {
		return start >= 0 && static_cast<std::size_t>(start) + (N - 1) <= len_ &&
		       std::memcmp(first_ + start, what, N - 1) == 0;
	}

	// Is the character at `i` one of the ASCII vowels (A E I O U Y)?
	bool is_vowel(std::ptrdiff_t i) const {
		return vowel_table.map[static_cast<unsigned char>(at(i))];
	}
};


// Which keys a walk builds: the primary, the alternate, or both. A walk that
// needs one key skips every append to the other.
enum : int { primary_key = 1, alternate_key = 2, both_keys = 3 };

// Collects the keys as the walk proceeds, each into a caller-sized buffer.
// `add(p)` pushes the same class to both; `add(p, a)` pushes a primary class
// and a (possibly different) alternate class, which is how a word grows two
// readings. The classes are string literals, so each append is a copy of a
// size known at compile time.
template <int Mode>
class Keys {
	char* p_;
	char* a_;

public:
	Keys(char* primary, char* alternate) : p_(primary), a_(alternate) {}

	template <std::size_t N>
	void add(const char (&both)[N]) {
		add(both, both);
	}

	template <std::size_t N, std::size_t M>
	void add(const char (&p)[N], const char (&a)[M]) {
		if constexpr ((Mode & primary_key) != 0) {
			std::memcpy(p_, p, N - 1);
			p_ += N - 1;
		}
		if constexpr ((Mode & alternate_key) != 0) {
			std::memcpy(a_, a, M - 1);
			a_ += M - 1;
		}
	}

	const char* primary_end() const { return p_; }
	const char* alternate_end() const { return a_; }
};


// Does the word START with a Germanic marker ("VAN ", "VON ", or "SCH")? This
// is a tighter test than `Word::slavo_germanic` and gates the CH-as-K reading.
bool germanic(const Word& w) {
	return w.match_at(0, "VAN ") || w.match_at(0, "VON ") || w.match_at(0, "SCH");
}

// True if `c` is any of the listed characters (helper for the CH rules).
template <std::size_t N>
bool is_any(char c, const char (&set)[N]) {
	for (std::size_t k = 0; k + 1 < N; ++k) {
		if (set[k] == c) {
			return true;
		}
	}
	return false;
}


// Walks the word once and writes the keys that `Mode` asks for into
// `*primary` and `*alternate` (the other pointer may be null). Both are empty
// for a word with no letters.
template <int Mode>
void encode_keys(std::string_view word, std::string* primary, std::string* alternate) {
	Word w(word);

	const auto n = static_cast<std::ptrdiff_t>(w.length());
	if (n == 0) {
		return;
	}

	// A step appends at most two classes of one letter each for every letter it
	// consumes, so a key never outgrows 2n + 2; the buffers hold 3n + 3.
	char stack_keys[2][256];
	std::unique_ptr<char[]> heap_keys;
	char* primary_buf = stack_keys[0];
	char* alternate_buf = stack_keys[1];
	const std::size_t capacity = 3 * static_cast<std::size_t>(n) + 3;
	if (capacity > sizeof stack_keys[0]) {
		heap_keys.reset(new char[2 * capacity]);
		primary_buf = heap_keys.get();
		alternate_buf = primary_buf + capacity;
	}
	Keys<Mode> keys(primary_buf, alternate_buf);

	const bool slavo = w.slavo_germanic();
	const bool germ = germanic(w);

	// Skip a silent first letter in certain Germanic/Greek initial clusters:
	// GN, KN, PN, WR all start with a silent consonant; "PS" likewise (as in
	// "psychology"). Position 0 of the algorithm then sits on the second letter.
	std::ptrdiff_t i = 0;
	if (w.match_at(0, "GN") || w.match_at(0, "KN") || w.match_at(0, "PN") ||
	    w.match_at(0, "WR") || w.match_at(0, "PS")) {
		i = 1;
	}

	// Initial 'X' is pronounced 'Z' ("Xavier" -> S... no, Z-sound -> 'S' class).
	if (w.at(0) == 'X') {
		keys.add("S");  // 'Z' sound encodes to S in Double Metaphone
		i = 1;
	}

	while (i < n) {
		char c = w.at(i);

		switch (c) {
			case 'A': case 'E': case 'I': case 'O': case 'U': case 'Y':
				// Vowels only count at the very start of the word; elsewhere
				// they are dropped entirely.
				if (i == 0) {
					keys.add("A");
				}
				i += 1;
				break;

			case 'B':
				// "-MB" is silent (dumb, comb): the B after M at end adds
				// nothing, but a doubled BB collapses to one P.
				keys.add("P");
				i += (w.at(i + 1) == 'B') ? 2 : 1;
				break;

			case 'C':
				// Germanic "...ACH..." that is a hard K (Bacher, Macher), but
				// not the soft "...ACHI..." / "...ACHE..." readings.
				if (w.at(i - 1) == 'A' && w.at(i + 1) == 'H' &&
				    w.at(i + 2) != 'I' && !w.is_vowel(i - 2) &&
				    (w.at(i + 2) != 'E' ||
				     w.match_at(i - 2, "BACHER") || w.match_at(i - 2, "MACHER"))) {
					keys.add("K");
					i += 2;
					break;
				}
				// Leading "CAESAR" -> soft S.
				if (i == 0 && w.match_at(i + 1, "AESAR")) {
					keys.add("S");
					i += 2;
					break;
				}
				// Italian "CHIA..." (Chianti) -> hard K.
				if (w.match_at(i + 1, "HIA")) {
					keys.add("K");
					i += 2;
					break;
				}
				if (w.at(i + 1) == 'H') {
					// "Michael": "-CHAE-" mid-word -> K primary, X alternate.
					if (i > 0 && w.at(i + 2) == 'A' && w.at(i + 3) == 'E') {
						keys.add("K", "X");
						i += 2;
						break;
					}
					// Greek roots at the front (chemistry, chorus, character):
					// hard K both ways.
					bool greek_initial =
						i == 0 &&
						(w.match_at(i, "CHARAC") || w.match_at(i, "CHARIS") ||
						 w.match_at(i, "CHOR") || w.match_at(i, "CHYM") ||
						 w.match_at(i, "CHIA") || w.match_at(i, "CHEM"));
					if (greek_initial) {
						keys.add("K");
						i += 2;
						break;
					}
					// CH is a hard K when: the word is Germanic-looking; a Greek
					// "...ARCH.../...ORCH..." root (architect, orchestra, orchid);
					// CH before T or S (yacht-like clusters); or CH at the start
					// or after a back vowel and before a consonant in the
					// "KH"-sounding set (wachtler, weschsler).
					bool greek_ch =
						w.match_at(i - 2, "ARCHIT") || w.match_at(i - 2, "ORCHES") ||
						w.match_at(i - 2, "ORCHID");
					bool ch_for_kh =
						(i == 0 || w.at(i - 1) == 'A' || w.at(i - 1) == 'E' ||
						 w.at(i - 1) == 'O' || w.at(i - 1) == 'U') &&
						is_any(w.at(i + 2), "BFHKLMNPQRSTVWXZ");
					if (germ || greek_ch || w.at(i + 2) == 'T' ||
					    w.at(i + 2) == 'S' || ch_for_kh) {
						keys.add("K");
					} else if (i == 0) {
						// Front-of-word English CH -> X (chair, cheese).
						keys.add("X");
					} else if (w.match_at(0, "MC")) {
						// "McHugh" -> K.
						keys.add("K");
					} else {
						// Mid-word English CH -> X primary, K alternate.
						keys.add("X", "K");
					}
					i += 2;
					break;
				}
				// "CZ" (Czerny) -> S primary, X alternate, except "WICZ".
				if (w.at(i + 1) == 'Z' && !w.match_at(i - 2, "WI")) {
					keys.add("S", "X");
					i += 2;
					break;
				}
				// "CIA" (focaccia) -> X.
				if (w.match_at(i + 1, "CIA") || w.match_at(i, "CIA")) {
					keys.add("X");
					i += 3;
					break;
				}
				// Doubled "CC", but not in "McC...".
				if (w.at(i + 1) == 'C' && !(i == 1 && w.at(0) == 'M')) {
					// "CCI"/"CCE"/"CCH" (but not "CCHU"): the Italian/Latin
					// split. "Accident"/"Accede"/"Succeed" read KS; the Italian
					// "Bacci"/"Bellocchio"/"Bertucci" read X.
					if ((w.at(i + 2) == 'I' || w.at(i + 2) == 'E' || w.at(i + 2) == 'H') &&
					    !w.match_at(i + 2, "HU")) {
						if ((i == 1 && w.at(0) == 'A') ||
						    w.match_at(i - 1, "UCCEE") || w.match_at(i - 1, "UCCES")) {
							keys.add("KS");
						} else {
							keys.add("X");
						}
						i += 3;
						break;
					}
					// Any other doubled C -> single K (Bacchus).
					keys.add("K");
					i += 2;
					break;
				}
				if (w.match_at(i, "CK") || w.match_at(i, "CG") || w.match_at(i, "CQ")) {
					keys.add("K");
					i += 2;
					break;
				}
				// Italian "CIO"/"CIE" -> S primary, X alternate.
				if (w.at(i + 1) == 'I' && (w.at(i + 2) == 'E' || w.at(i + 2) == 'O')) {
					keys.add("S", "X");
					i += 2;
					break;
				}
				// Soft C before I/E/Y -> S.
				if (w.at(i + 1) == 'I' || w.at(i + 1) == 'E' || w.at(i + 1) == 'Y') {
					keys.add("S");
					i += 2;
					break;
				}
				// Plain hard C -> K. "Mac Caffrey"/"Mac Gregor": skip an extra
				// letter across the space. A doubled CC that reached here (the
				// "McC..." case) advances only one, so the second C still sounds.
				keys.add("K");
				if (w.at(i + 1) == ' ' &&
				    (w.at(i + 2) == 'C' || w.at(i + 2) == 'G' || w.at(i + 2) == 'Q')) {
					i += 3;
				} else {
					i += 1;
				}
				break;

			case 'D':
				if (w.match_at(i, "DG")) {
					// "DGE"/"DGI"/"DGY" (edge, ridge) -> J sound; otherwise
					// "DG" -> TK (e.g. "Wedgwood").
					if (w.at(i + 2) == 'I' || w.at(i + 2) == 'E' || w.at(i + 2) == 'Y') {
						keys.add("J");
						i += 3;
					} else {
						keys.add("TK");
						i += 2;
					}
					break;
				}
				if (w.match_at(i, "DT") || w.match_at(i, "DD")) {
					keys.add("T");
					i += 2;
					break;
				}
				keys.add("T");
				i += 1;
				break;

			case 'F':
				keys.add("F");
				i += (w.at(i + 1) == 'F') ? 2 : 1;
				break;

			case 'G':
				if (w.at(i + 1) == 'H') {
					// "GH" after a non-initial consonant is a hard K (Pittsburgh,
					// burgher).
					if (i > 0 && !w.is_vowel(i - 1)) {
						keys.add("K");
						i += 2;
						break;
					}
					// Leading "GH": "GHI..." (Ghislane) -> J, else K (ghost).
					if (i == 0) {
						if (w.at(i + 2) == 'I') {
							keys.add("J");
						} else {
							keys.add("K");
						}
						i += 2;
						break;
					}
					// Parker's rule: GH is silent after certain spellings
					// (Hugh, bough, Broughton) detected by a B/H/D two, three, or
					// four letters back.
					if (w.at(i - 2) == 'B' || w.at(i - 2) == 'H' || w.at(i - 2) == 'D' ||
					    w.at(i - 3) == 'B' || w.at(i - 3) == 'H' || w.at(i - 3) == 'D' ||
					    w.at(i - 4) == 'B' || w.at(i - 4) == 'H') {
						i += 2;
						break;
					}
					// "...UGH" preceded by C/G/L/R/T voices to F (laugh, cough,
					// rough, tough); otherwise GH after a non-I vowel is a hard K.
					if (i > 2 && w.at(i - 1) == 'U' && is_any(w.at(i - 3), "CGLRT")) {
						keys.add("F");
					} else if (i > 0 && w.at(i - 1) != 'I') {
						keys.add("K");
					}
					i += 2;
					break;
				}
				if (w.at(i + 1) == 'N') {
					// "GN": "...IGN..." or "GNED" silent G; "Magner" etc. keep
					// the alternate K. Encode K primary (silent-ish) but give a
					// foreign KN alternate where the rules want it.
					if (i == 1 && w.is_vowel(0) && !slavo) {
						keys.add("KN", "N");
					} else if (!w.match_at(i + 2, "EY") && w.at(i + 1) == 'N' && !slavo) {
						keys.add("N", "KN");
					} else {
						keys.add("KN");
					}
					i += 2;
					break;
				}
				// "-GLI-" Italian (e.g. "tagliaro") -> KL with L alternate.
				if (w.match_at(i, "GLI") && !slavo) {
					keys.add("KL", "L");
					i += 2;
					break;
				}
				// Front-vowel softening to J (gemetry, ginger), with the many
				// hard-G exceptions baked into the simplified rule below.
				if (w.at(i + 1) == 'Y' ||
				    w.match_at(i, "GES") || w.match_at(i, "GEP") || w.match_at(i, "GEB") ||
				    w.match_at(i, "GEL") || w.match_at(i, "GEY") || w.match_at(i, "GIB") ||
				    w.match_at(i, "GIL") || w.match_at(i, "GIN") || w.match_at(i, "GIE") ||
				    w.match_at(i, "GEI") || w.match_at(i, "GER")) {
					// These are mostly hard-G words (Gertrude, Gibson) -> K/J
					// pair.
					keys.add("K", "J");
					i += 2;
					break;
				}
				if (w.at(i + 1) == 'E' || w.at(i + 1) == 'I' || w.at(i + 1) == 'Y') {
					// Soft G before E/I/Y -> J (with K alternate for the
					// foreign hard reading).
					keys.add("J", "K");
					i += 2;
					break;
				}
				// Plain hard G -> K, collapsing a doubled GG.
				keys.add("K");
				i += (w.at(i + 1) == 'G') ? 2 : 1;
				break;

			case 'H':
				// H is only kept when it actually sounds: between two vowels, or
				// at the start before a vowel. Otherwise it is silent.
				if ((i == 0 || w.is_vowel(i - 1)) && w.is_vowel(i + 1)) {
					keys.add("H");
					i += 2;
				} else {
					i += 1;
				}
				break;

			case 'J':
				// "JOSE" / "SAN " contexts read the Spanish 'H' sound, but the
				// common English J -> J. Doubled handling and a Spanish-leaning
				// alternate.
				if (w.match_at(i, "JOSE") || w.match_at(0, "SAN ")) {
					// "San Jose" -> H sound; here we keep primary J with H-ish
					// alternate folded to A (vowel-initial).
					if ((i == 0 && w.at(i + 4) == ' ') || w.match_at(0, "SAN ")) {
						keys.add("H");
					} else {
						keys.add("J", "H");
					}
					i += 1;
					break;
				}
				if (i == 0) {
					keys.add("J", "A");  // initial J: English J, Spanish/A alt
					i += (w.at(i + 1) == 'J') ? 2 : 1;
				} else if (w.is_vowel(i - 1) && !slavo &&
				           (w.at(i + 1) == 'A' || w.at(i + 1) == 'O')) {
					// Spanish "bajador": J reads J primary, H alternate.
					keys.add("J", "H");
					i += (w.at(i + 1) == 'J') ? 2 : 1;
				} else if (static_cast<std::ptrdiff_t>(i) == n - 1) {
					// Final J sounds in the primary only (raj, Taj).
					keys.add("J", "");
					i += 1;
				} else if (w.at(i - 1) != 'S' && w.at(i - 1) != 'K' && w.at(i - 1) != 'L' &&
				           !is_any(w.at(i + 1), "LTKSNMBZ")) {
					keys.add("J");
					i += (w.at(i + 1) == 'J') ? 2 : 1;
				} else {
					// J between certain consonants is effectively silent
					// (Jankelowicz's internal J after N before K).
					i += (w.at(i + 1) == 'J') ? 2 : 1;
				}
				break;

			case 'K':
				keys.add("K");
				i += (w.at(i + 1) == 'K') ? 2 : 1;
				break;

			case 'L':
				if (w.at(i + 1) == 'L') {
					// Spanish "...ILLO"/"...ILLA"/"...ALLE" (cabrillo, gallegos):
					// the second L is dropped from the ALTERNATE only, so the
					// foreign reading loses it. Detect the Spanish-final shape.
					bool ends_alle =
						w.at(n - 1) == 'A' || w.at(n - 1) == 'O' ||
						w.match_at(n - 2, "AS") || w.match_at(n - 2, "OS");
					bool spanish_ll =
						(static_cast<std::ptrdiff_t>(i) == n - 3 &&
						 ((w.at(i - 1) == 'A' && w.at(i + 2) == 'E') ||
						  (w.at(i - 1) == 'I' &&
						   (w.at(i + 2) == 'O' || w.at(i + 2) == 'A')))) ||
						(w.at(i - 1) == 'A' && w.at(i + 2) == 'E' && ends_alle);
					if (spanish_ll) {
						keys.add("L", "");  // L primary only
						i += 2;
						break;
					}
					keys.add("L");
					i += 2;
					break;
				}
				keys.add("L");
				i += 1;
				break;

			case 'M':
				// The B is silent only in "UMB" at the end (dumb, thumb, plumb)
				// or before "ER" (number, plumber); a doubled MM collapses too.
				// "bomb"/"lamb"/"tomb" are NOT "UMB", so they keep the B.
				keys.add("M");
				if (w.at(i + 1) == 'M' ||
				    (w.match_at(i - 1, "UMB") &&
				     (static_cast<std::ptrdiff_t>(i + 1) == n - 1 ||
				      w.match_at(i + 2, "ER")))) {
					i += 2;
				} else {
					i += 1;
				}
				break;

			case 'N':
				keys.add("N");
				i += (w.at(i + 1) == 'N') ? 2 : 1;
				break;

			case 'P':
				// "PH" -> F (phone, Philip).
				if (w.at(i + 1) == 'H') {
					keys.add("F");
					i += 2;
					break;
				}
				// Plain P -> P. Standard Double Metaphone does NOT silence the P
				// before F or after M: the P rule only special-cases PH. So
				// "Pfister" -> PFSTR and "Thompson" -> TMPSN, matching the reference
				// implementations. Collapse a doubled PP or a "PB" cluster.
				keys.add("P");
				i += (w.at(i + 1) == 'P' || w.at(i + 1) == 'B') ? 2 : 1;
				break;

			case 'Q':
				keys.add("K");
				i += (w.at(i + 1) == 'Q') ? 2 : 1;
				break;

			case 'R':
				// Final French "-IER" (Rogier, Xavier): the R voices only the
				// alternate, not the primary, unless the word is Slavo-Germanic
				// or matches the "Hochmeier" exclusions.
				if (static_cast<std::ptrdiff_t>(i) == n - 1 && !slavo &&
				    w.at(i - 1) == 'E' && w.at(i - 2) == 'I' &&
				    w.at(i - 4) != 'M' && w.at(i - 3) != 'E' && w.at(i - 3) != 'A') {
					keys.add("", "R");
				} else {
					keys.add("R");
				}
				i += (w.at(i + 1) == 'R') ? 2 : 1;
				break;

			case 'S':
				// "ISL"/"YSL": the S is silent (island, Carlisle).
				if (w.match_at(i - 1, "ISL") || w.match_at(i - 1, "YSL")) {
					i += 1;
					break;
				}
				// Leading "SUGAR" -> X (the only English word where SU- is "sh").
				if (i == 0 && w.match_at(i, "SUGAR")) {
					keys.add("X", "S");
					i += 1;
					break;
				}
				// "SH" -> X (shoe), except Germanic/Dutch roots HEIM/HOEK/HOLM/
				// HOLZ which keep a plain S.
				if (w.match_at(i, "SH")) {
					if (w.match_at(i + 1, "HEIM") || w.match_at(i + 1, "HOEK") ||
					    w.match_at(i + 1, "HOLM") || w.match_at(i + 1, "HOLZ")) {
						keys.add("S");
					} else {
						keys.add("X");
					}
					i += 2;
					break;
				}
				// "SIO"/"SIA"/"SIAN" (Asia, vision): X primary, S alternate,
				// except in Slavo-Germanic words where it stays S.
				if (w.match_at(i, "SIO") || w.match_at(i, "SIA") || w.match_at(i, "SIAN")) {
					if (slavo) {
						keys.add("S");
					} else {
						keys.add("S", "X");
					}
					i += 3;
					break;
				}
				// Initial "SM"/"SN"/"SL"/"SW" (Smith, Snyder) and any "SZ":
				// the "sh"-flavored alternate that makes Smith collapse with
				// Schmidt: S primary, X alternate.
				if ((i == 0 && (w.at(i + 1) == 'M' || w.at(i + 1) == 'N' ||
				                w.at(i + 1) == 'L' || w.at(i + 1) == 'W')) ||
				    w.at(i + 1) == 'Z') {
					keys.add("S", "X");
					i += (w.at(i + 1) == 'Z') ? 2 : 1;
					break;
				}
				if (w.match_at(i, "SC")) {
					// "SCH": Greek roots SCHOO/SCHER/SCHEN/SCHUY/SCHED/SCHEM read
					// SK (school, scheme); "SCHER"/"SCHEN" specifically split
					// X/SK; an initial "SCH" before a non-vowel (Schmidt,
					// Schneider) is X primary / S alternate; everything else X.
					if (w.at(i + 2) == 'H') {
						if (w.match_at(i + 3, "OO") || w.match_at(i + 3, "ER") ||
						    w.match_at(i + 3, "EN") || w.match_at(i + 3, "UY") ||
						    w.match_at(i + 3, "ED") || w.match_at(i + 3, "EM")) {
							if (w.match_at(i + 3, "ER") || w.match_at(i + 3, "EN")) {
								keys.add("X", "SK");
							} else {
								keys.add("SK");
							}
						} else if (i == 0 && !w.is_vowel(i + 3) && w.at(i + 3) != 'W') {
							// "SCHMIDT", "SCHNEIDER" -> X primary, S alternate
							keys.add("X", "S");
						} else {
							keys.add("X");
						}
						i += 3;
						break;
					}
					// "SCI"/"SCE"/"SCY" -> S; other "SC..." -> SK.
					if (w.at(i + 2) == 'I' || w.at(i + 2) == 'E' || w.at(i + 2) == 'Y') {
						keys.add("S");
					} else {
						keys.add("SK");
					}
					i += 3;
					break;
				}
				// Final "...AIS"/"...OIS" French (the S only voices the
				// alternate); otherwise plain S.
				if (static_cast<std::ptrdiff_t>(i) == n - 1 &&
				    (w.match_at(i - 2, "AI") || w.match_at(i - 2, "OI"))) {
					keys.add("", "S");
				} else {
					keys.add("S");
				}
				i += (w.at(i + 1) == 'S' || w.at(i + 1) == 'Z') ? 2 : 1;
				break;

			case 'T':
				// "TION"/"TIA"/"TCH" and the all-important "TH" -> '0' (the
				// theta sound), with a Germanic T alternate.
				if (w.match_at(i, "TIO") || w.match_at(i, "TIA") || w.match_at(i, "TCH")) {
					keys.add("X");
					i += (w.match_at(i, "TCH")) ? 3 : 3;
					break;
				}
				if (w.match_at(i, "TH") || w.match_at(i, "TTH")) {
					// "THOM"/"THAM" (Thomas, Thames) read a hard T both ways;
					// everything else is the dental "0" with a T alternate.
					if (w.match_at(i, "THOM") || w.match_at(i, "THAM")) {
						keys.add("T");
					} else {
						keys.add("0", "T");
					}
					i += 2;
					break;
				}
				keys.add("T");
				i += (w.at(i + 1) == 'T' || w.at(i + 1) == 'D') ? 2 : 1;
				break;

			case 'V':
				keys.add("F");
				i += (w.at(i + 1) == 'V') ? 2 : 1;
				break;

			case 'W':
				// "WR" -> R (the W is silent: wrack, wright).
				if (w.at(i + 1) == 'R') {
					keys.add("R");
					i += 2;
					break;
				}
				// Initial "W" before a vowel reads A primary, F alternate
				// (Wasserman / Vasserman); "WH" reads A both ways (Womo / Uomo).
				if (i == 0) {
					if (w.is_vowel(i + 1)) {
						keys.add("A", "F");
					} else if (w.at(i + 1) == 'H') {
						keys.add("A");
					}
				}
				// "Arnow" matches "Arnoff": a "W" ending after a vowel, a Polish
				// "...EWSKI" / leading "SCH" word, voices an F alternate only.
				if (((w.at(i - 1) == 'E' || w.at(i - 1) == 'O') && w.at(i + 1) == 'S' &&
				     w.at(i + 2) == 'K' && (w.at(i + 3) == 'I' || w.at(i + 3) == 'Y')) ||
				    w.match_at(0, "SCH") ||
				    (static_cast<std::ptrdiff_t>(i) == n - 1 && w.is_vowel(i - 1))) {
					keys.add("", "F");
					i += 1;
					break;
				}
				// Polish "...WICZ" / "...WITZ" (Filipowicz): TS primary, FX
				// alternate.
				if (w.at(i + 1) == 'I' && (w.at(i + 2) == 'C' || w.at(i + 2) == 'T') &&
				    w.at(i + 3) == 'Z') {
					keys.add("TS", "FX");
					i += 4;
					break;
				}
				// Otherwise the W is silent.
				i += 1;
				break;

			case 'X':
				// "X" -> KS, but a final "-AUX"/"-EAUX" (French) is silent.
				if (!(static_cast<std::ptrdiff_t>(i) == n - 1 &&
				      (w.match_at(i - 3, "IAU") || w.match_at(i - 3, "EAU") ||
				       w.match_at(i - 2, "AU") || w.match_at(i - 2, "OU")))) {
					keys.add("KS");
				}
				i += (w.at(i + 1) == 'C' || w.at(i + 1) == 'X') ? 2 : 1;
				break;

			case 'Z':
				// "ZH" -> J (pinyin Zhao, Russian transliteration). An Italian
				// "ZZ" before A/I/O (pizza, mezzo) or a Slavo-Germanic Z reads S
				// primary, TS alternate. Otherwise plain S.
				if (w.at(i + 1) == 'H') {
					keys.add("J");
					i += 2;
					break;
				}
				if ((w.at(i + 1) == 'Z' &&
				     (w.at(i + 2) == 'A' || w.at(i + 2) == 'I' || w.at(i + 2) == 'O')) ||
				    (slavo && i > 0 && w.at(i - 1) != 'T')) {
					keys.add("S", "TS");
				} else {
					keys.add("S");
				}
				i += (w.at(i + 1) == 'Z') ? 2 : 1;
				break;

			default:
				// Any stray character (shouldn't happen after cleaning): skip.
				i += 1;
				break;
		}
	}

	if constexpr ((Mode & primary_key) != 0) {
		primary->assign(primary_buf, static_cast<std::size_t>(keys.primary_end() - primary_buf));
	}
	if constexpr ((Mode & alternate_key) != 0) {
		alternate->assign(alternate_buf, static_cast<std::size_t>(keys.alternate_end() - alternate_buf));
	}
}

} // namespace


std::string DoubleMetaphone::encode(std::string_view word) const {
	std::string primary;
	encode_keys<primary_key>(word, &primary, nullptr);
	return primary;
}

std::string DoubleMetaphone::encode_alternate(std::string_view word) const {
	std::string alternate;
	encode_keys<alternate_key>(word, nullptr, &alternate);
	return alternate;
}

std::pair<std::string, std::string> DoubleMetaphone::encode_both(std::string_view word) const {
	std::pair<std::string, std::string> keys;
	encode_keys<both_keys>(word, &keys.first, &keys.second);
	return keys;
}
