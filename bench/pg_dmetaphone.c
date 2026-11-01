/*
 * The benchmark's reference: PostgreSQL's contrib/fuzzystrmatch/dmetaphone.c, compiled in its
 * standalone mode (DMETAPHONE_MAIN, plain malloc and free). The build fetches that file for the
 * benchmark target only (DOUBLE_METAPHONE_BENCH_REFERENCE); this wrapper holds none of its code
 * and the library never links it. The file carries its own license (Text::DoubleMetaphone, under
 * the terms of Perl, and a permissive notice for the PostgreSQL additions).
 */
#define DMETAPHONE_MAIN 1
#define main int pg_dmetaphone_unused_main
#define DoubleMetaphone pg_double_metaphone
#include DMETAPHONE_PG_SOURCE
#undef main
#undef DoubleMetaphone

/* The primary key in codes[0] and the alternate in codes[1], both malloc'd for the caller. */
void pg_double_metaphone_codes(const char* word, char** codes) {
	pg_double_metaphone((char*) word, codes);
}
