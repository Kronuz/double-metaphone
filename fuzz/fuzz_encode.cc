// libFuzzer target: every key of an arbitrary byte string equals the frozen original's, and no
// read leaves the cleaned word's padding (run it under ASan).
#include <cstdint>
#include <cstdlib>
#include <string>
#include <string_view>

#include "double_metaphone.h"
#include "../test/double_metaphone_old.h"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
	static const DoubleMetaphone dm;
	static const DoubleMetaphoneOld old;
	std::string_view in(reinterpret_cast<const char*>(data), size);
	auto want = old.encode_both(in);
	if (dm.encode_both(in) != want || dm.encode(in) != want.first || dm.encode_alternate(in) != want.second) {
		std::abort();
	}
	return 0;
}
