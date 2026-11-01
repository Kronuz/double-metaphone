// Copies and moves of DoubleMetaphone. An encoder built from a word is a value: a copy
// or a move, from a non-const lvalue, a const lvalue, an rvalue or a const rvalue, holds
// the same primary key as the original, and keeps it after the original is overwritten or
// destroyed. The forwarding constructor that takes a word must not capture any of these
// (it would try to encode the encoder itself). The expected keys are the ones pinned in
// test/test.cc.
// Named check: copy_double_metaphone.

#include <cstdio>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "double_metaphone.h"

static int failures = 0;
static size_t checks = 0;

static void expect(const std::string& got, const std::string& want, const char* how) {
	++checks;
	if (got != want) {
		if (++failures < 30) std::printf("FAIL copy_double_metaphone (%s): got '%s' want '%s'\n", how, got.c_str(), want.c_str());
	}
}

// A class derived from the encoder is copied through the same constructors.
struct Derived : DoubleMetaphone {
	using DoubleMetaphone::DoubleMetaphone;
};

int main() {
	static_assert(std::is_copy_constructible_v<DoubleMetaphone> && std::is_move_constructible_v<DoubleMetaphone>);
	static_assert(std::is_copy_assignable_v<DoubleMetaphone> && std::is_move_assignable_v<DoubleMetaphone>);

	const std::string word = "Schmidt";
	const std::string want = "XMT";
	const std::string other = "Smith";
	const std::string other_want = "SM0";

	// Construction from the word still works for every kind of string argument.
	expect(DoubleMetaphone(word).encode(), want, "from a std::string lvalue");
	expect(DoubleMetaphone(word.c_str()).encode(), want, "from a C string");
	expect(DoubleMetaphone(std::string_view(word)).encode(), want, "from a string_view");
	expect(DoubleMetaphone(std::string(word)).encode(), want, "from a std::string rvalue");

	DoubleMetaphone original(word);
	expect(original.encode(), want, "original");

	{
		DoubleMetaphone copy = original;  // non-const lvalue
		expect(copy.encode(), want, "copy of a non-const lvalue");
	}
	{
		DoubleMetaphone copy(original);  // non-const lvalue, direct initialization
		expect(copy.encode(), want, "direct copy of a non-const lvalue");
	}
	{
		const DoubleMetaphone& ref = original;
		DoubleMetaphone copy = ref;
		expect(copy.encode(), want, "copy of a const lvalue");
	}
	{
		const DoubleMetaphone constant(word);
		DoubleMetaphone copy = std::move(constant);  // const rvalue
		expect(copy.encode(), want, "copy of a const rvalue");
	}
	{
		DoubleMetaphone source(word);
		DoubleMetaphone moved = std::move(source);  // non-const rvalue
		expect(moved.encode(), want, "move");
	}
	{
		DoubleMetaphone source(word);
		DoubleMetaphone copy = source;
		source = DoubleMetaphone(other);  // the copy is independent of the original
		expect(copy.encode(), want, "copy after the original is overwritten");
		expect(source.encode(), other_want, "the overwritten original");
	}
	{
		DoubleMetaphone target(other);
		target = original;  // copy assignment from a non-const lvalue
		expect(target.encode(), want, "copy assignment");
		DoubleMetaphone target2(other);
		target2 = DoubleMetaphone(word);
		expect(target2.encode(), want, "move assignment");
	}
	{
		auto destroyed = std::make_unique<DoubleMetaphone>(word);
		DoubleMetaphone copy = *destroyed;  // non-const lvalue of a heap object
		destroyed.reset();
		expect(copy.encode(), want, "copy after the original is destroyed");
	}

	// The library paths that construct in place from a lvalue go through the forwarding
	// constructor.
	{
		std::vector<DoubleMetaphone> v;
		for (int i = 0; i < 20; ++i) {
			v.emplace_back(original);  // non-const lvalue, with regrowth
		}
		for (auto& e : v) expect(e.encode(), want, "emplace_back of a non-const lvalue");
		std::vector<DoubleMetaphone> w = v;
		for (auto& e : w) expect(e.encode(), want, "vector copy");
	}
	{
		auto p = std::make_unique<DoubleMetaphone>(original);
		expect(p->encode(), want, "make_unique of a non-const lvalue");
		std::optional<DoubleMetaphone> o(original);
		expect(o->encode(), want, "optional of a non-const lvalue");
		std::pair<DoubleMetaphone, int> pr(original, 1);
		expect(pr.first.encode(), want, "pair of a non-const lvalue");
	}

	// A derived class copies and moves too.
	{
		Derived d(word);
		expect(d.encode(), want, "derived");
		Derived dc = d;
		expect(dc.encode(), want, "derived copy of a non-const lvalue");
		DoubleMetaphone base(d);  // slices a non-const derived lvalue into the base
		expect(base.encode(), want, "base from a non-const derived lvalue");
		const Derived cd(word);
		DoubleMetaphone base2(cd);
		expect(base2.encode(), want, "base from a const derived lvalue");
		Derived dm = std::move(d);
		expect(dm.encode(), want, "derived move");
	}

	// The default-constructed encoder is empty and still encodes a fresh word; the
	// alternate key and both keys work on a copy.
	{
		DoubleMetaphone empty;
		DoubleMetaphone copy = empty;  // non-const lvalue of an empty encoder
		expect(copy.encode(), "", "copy of a default-constructed encoder");
		expect(copy.encode(word), want, "encode(word) on a copy");
		expect(copy.encode_alternate(word), "SMT", "encode_alternate(word) on a copy");
		auto both = copy.encode_both(word);
		expect(both.first + "/" + both.second, "XMT/SMT", "encode_both(word) on a copy");
	}

	std::printf("%zu checks, %d failures\n", checks, failures);
	if (failures) {
		return 1;
	}
	std::printf("all double metaphone copy tests passed\n");
	return 0;
}
