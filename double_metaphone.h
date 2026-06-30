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

#pragma once

#include <string>                                 // for std::string
#include <string_view>                            // for std::string_view
#include <utility>                                // for std::forward, std::pair


/*
 * Double Metaphone phonetic encoder (Lawrence Philips, C/C++ Users Journal,
 * June 2000).
 *
 * A modern improvement over Soundex: it understands silent letters, common
 * non-English spellings, and the many ways a single sound is written in
 * English. Two words that sound alike collapse to the same key even when their
 * spellings diverge wildly (Smith / Schmidt, Catherine / Kathryn).
 *
 * Unlike Soundex, it emits TWO keys for a word:
 *
 *   - the PRIMARY key   - the dominant English pronunciation.
 *   - the ALTERNATE key  - an alternate (often Slavic, Germanic, or Romance)
 *                          pronunciation. It equals the primary when the word
 *                          has only one likely reading.
 *
 * The keys are built from a small alphabet of sound classes: the letters
 * A B F H J K L M N P R S T W X and '0' (the digit zero stands for the "th"
 * sound). They are not truncated to a fixed length here.
 *
 * Interface
 * ---------
 * This is a plain class shaped to drop in where a soundex encoder is used: it
 * mirrors that family's surface (a cached `_code_str`, `encode()`/`encode(word)`,
 * `name()`, `description()`). `encode()` and `encode(word)` return the PRIMARY
 * key, which is what a phonetic metric calls. The alternate key is opt-in:
 *
 *   - `encode(word)`           -> primary key for `word`.
 *   - `encode()`               -> the cached primary key (set at construction).
 *   - `encode_alternate(word)` -> alternate key for `word`.
 *   - `encode_both(word)`      -> {primary, alternate}.
 *   - `name()` / `description()`.
 *
 * Pure std/<string>: it does its own ASCII upper-casing and has zero external
 * dependencies.
 */
class DoubleMetaphone {
protected:
	// Cached PRIMARY key, set at construction (mirrors the soundex base's
	// `_code_str`). Empty for a default-constructed encoder.
	std::string _code_str;

public:
	DoubleMetaphone() = default;

	// Construct from a word: encodes it and caches the PRIMARY key in
	// `_code_str`. `encode()` reads that back.
	template <typename T>
	DoubleMetaphone(T&& str)
		: _code_str(encode(std::string_view(std::forward<T>(str)))) { }

	// PRIMARY Double Metaphone key for a fresh word. This is the drop-in entry
	// point: a phonetic metric calls encode(a) / encode(b) and compares.
	std::string encode(std::string_view word) const {
		return encode_both(word).first;
	}

	// The cached PRIMARY key (set at construction).
	std::string encode() const {
		return _code_str;
	}

	// ALTERNATE Double Metaphone key for a word. Equals the primary when the
	// word has only one likely pronunciation.
	std::string encode_alternate(std::string_view word) const {
		return encode_both(word).second;
	}

	// Both keys at once: {primary, alternate}. Cheaper than two calls because
	// the algorithm computes them together in a single pass.
	std::pair<std::string, std::string> encode_both(std::string_view word) const;

	std::string_view name() const noexcept {
		return "dmetaphone";
	}

	std::string description() const noexcept {
		return "Double Metaphone phonetic encoder (primary + alternate keys)";
	}
};
