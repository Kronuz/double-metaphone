/*
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

#include <cstddef>                                // for std::size_t, std::ptrdiff_t
#include <string>                                 // for std::string
#include <string_view>                            // for std::string_view
#include <utility>                                // for std::move, std::pair


namespace {

// A tiny cursor over the cleaned word. It owns the upper-cased, A-Z-only text
// plus a couple of sentinel pad characters past the end, so look-ahead like
// word[i + 2] is always safe without bounds juggling at every call site. All
// indexing is relative to `original_first`, the index of the first real letter
// after any cleaning, which is what the published rules mean by "position 0".
class Word {
	std::string buf_;  // upper-cased letters only, plus padding past the end
	std::size_t len_;  // number of real letters (padding excluded)

public:
	explicit Word(std::string_view in) {
		buf_.reserve(in.size() + 6);
		for (char c : in) {
			// ASCII upper-case in place, keep A-Z plus single interior spaces.
			// This is the library's own folding (no external strings
			// dependency); bytes >= 0x80 from UTF-8 input are dropped. Spaces
			// are kept (collapsed to one) because a few rules key off word
			// boundaries in multi-word names ("SAN ", "VAN ", "VON ").
			if (c >= 'a' && c <= 'z') {
				buf_.push_back(static_cast<char>(c - ('a' - 'A')));
			} else if (c >= 'A' && c <= 'Z') {
				buf_.push_back(c);
			} else if ((c == ' ' || c == '\t') && !buf_.empty() && buf_.back() != ' ') {
				buf_.push_back(' ');
			}
		}
		// Trim a trailing space the collapsing may have left.
		while (!buf_.empty() && buf_.back() == ' ') {
			buf_.pop_back();
		}
		len_ = buf_.size();
		// Pad so look-ahead a few past the end is always in-bounds and reads as
		// a non-letter ' ' that matches no rule.
		buf_.append(6, ' ');
	}

	std::size_t length() const { return len_; }

	// Character at absolute index (0-based over the real letters). Out-of-range
	// indices read the padding (or, for negatives, ' '): never a real letter.
	char at(std::ptrdiff_t i) const {
		if (i < 0 || static_cast<std::size_t>(i) >= buf_.size()) {
			return ' ';
		}
		return buf_[static_cast<std::size_t>(i)];
	}

	// The substring [start, start + n). Clamped so it never reads padding as if
	// it were content; an out-of-range request yields a shorter/empty view.
	std::string_view substr(std::ptrdiff_t start, std::size_t n) const {
		if (start < 0) {
			return {};
		}
		auto s = static_cast<std::size_t>(start);
		if (s >= len_) {
			return {};
		}
		if (n > len_ - s) {
			n = len_ - s;
		}
		return std::string_view(buf_).substr(s, n);
	}

	// Does the n-length window at `start` equal `what`? (length-guarded compare)
	bool match_at(std::ptrdiff_t start, std::string_view what) const {
		return substr(start, what.size()) == what;
	}

	// Is the character at `i` one of the ASCII vowels (A E I O U Y)?
	bool is_vowel(std::ptrdiff_t i) const {
		switch (at(i)) {
			case 'A': case 'E': case 'I': case 'O': case 'U': case 'Y':
				return true;
			default:
				return false;
		}
	}
};


// Collects the two keys as the walk proceeds. `add(p)` pushes the same class to
// both; `add(p, a)` pushes a primary class and a (possibly different) alternate
// class, which is how a word grows two readings.
class Keys {
	std::string primary_;
	std::string alternate_;

public:
	void add(std::string_view both) {
		primary_ += both;
		alternate_ += both;
	}

	void add(std::string_view p, std::string_view a) {
		primary_ += p;
		alternate_ += a;
	}

	std::string take_primary() { return std::move(primary_); }
	std::string take_alternate() { return std::move(alternate_); }
};


// Does the word look Slavo-Germanic? A handful of rules lean toward hard 'K'
// and TS/F readings for these (often German/Slavic) spellings. The original
// detects it by the presence of W, K, "CZ", or "WITZ" anywhere in the word.
bool slavo_germanic(const Word& w) {
	auto len = static_cast<std::ptrdiff_t>(w.length());
	for (std::ptrdiff_t i = 0; i < len; ++i) {
		char c = w.at(i);
		if (c == 'W' || c == 'K' ||
		    w.match_at(i, "CZ") || w.match_at(i, "WITZ")) {
			return true;
		}
	}
	return false;
}

// Does the word START with a Germanic marker ("VAN ", "VON ", or "SCH")? This
// is a tighter test than slavo_germanic and gates the CH-as-K reading.
bool germanic(const Word& w) {
	return w.match_at(0, "VAN ") || w.match_at(0, "VON ") || w.match_at(0, "SCH");
}

// True if `c` is any of the listed characters (helper for the CH rules).
bool is_any(char c, std::string_view set) {
	return set.find(c) != std::string_view::npos;
}

} // namespace


std::pair<std::string, std::string> DoubleMetaphone::encode_both(std::string_view word) const {
	Word w(word);
	Keys keys;

	const auto n = static_cast<std::ptrdiff_t>(w.length());
	if (n == 0) {
		return {std::string(), std::string()};
	}

	const bool slavo = slavo_germanic(w);
	const bool germ = germanic(w);

	// Skip a silent first letter in certain Germanic/Greek initial clusters:
	// GN, KN, PN, WR all start with a silent consonant; "PS" likewise (as in
	// "psychology"). Position 0 of the algorithm then sits on the second letter.
	std::ptrdiff_t i = 0;
	{
		auto first2 = w.substr(0, 2);
		if (first2 == "GN" || first2 == "KN" || first2 == "PN" ||
		    first2 == "WR" || first2 == "PS") {
			i = 1;
		}
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

	return {keys.take_primary(), keys.take_alternate()};
}
