# double-metaphone API reference

## Usage

The class is shaped to drop in where a `soundex` encoder is used: the same
cached-code surface (`encode()`, `encode(word)`, `name()`, `description()`), so a
phonetic metric that calls `encode(a)` / `encode(b)` works unchanged. The key
those calls return is the **primary**; the alternate is opt-in.

```cpp
#include "double_metaphone.h"

DoubleMetaphone dm;

// encode(word) returns the PRIMARY key, the drop-in entry point.
dm.encode("Smith");            // "SM0"
dm.encode("Catherine");        // "K0RN"
dm.encode("dumb");             // "TM"   (the B is silent)

// Construct from a word, read the cached primary key back.
DoubleMetaphone("Smith").encode();   // "SM0"

// The richer API: the alternate key, or both at once.
dm.encode_alternate("Smith");  // "XMT"
dm.encode_both("Schmidt");     // {"XMT", "SMT"}

// The homophone collapse that motivates the algorithm: Smith and Schmidt
// share a key across the primary/alternate cross-product.
dm.encode_alternate("Smith") == dm.encode("Schmidt");   // both "XMT"

// Empty input stays empty on both keys.
dm.encode("");                 // ""
```

`encode()` and `encode(word)` always return the primary key. To compare words
the way the algorithm intends, two of them match if any of their keys agree
across the `{primary, alternate}` cross-product. The class also exposes
`name()` (`"dmetaphone"`) and `description()`.
