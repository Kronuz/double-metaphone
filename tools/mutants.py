#!/usr/bin/env python3
"""Mutation check for the encoder's primitives.

For each mutant: copy double_metaphone.cc with one deliberate bug into a scratch dir, build
test/diff_test.cc against it (with the frozen original) and require that the test FAILS or
crashes. A mutant that survives means the differential test would not catch that bug.

usage: tools/mutants.py TREE_DIR [CXX] [only-name ...]     (scratch dir: $MUT_DIR or a temp dir)
"""
import os
import shutil
import subprocess
import sys
import tempfile

tree = os.path.abspath(sys.argv[1])
cxx = sys.argv[2] if len(sys.argv) > 2 else "c++"
only = set(sys.argv[3:])
src = open(os.path.join(tree, "double_metaphone.cc"), encoding="utf-8").read()

MUTANTS = {
    # the primary-only walk appends the alternate classes
    "primary_takes_alternate": (
        "std::memcpy(p_, p, N - 1);\n\t\t\tp_ += N - 1;",
        "std::memcpy(p_, a, M - 1);\n\t\t\tp_ += M - 1;"),
    # the alternate-only walk appends the primary classes
    "alternate_takes_primary": (
        "std::memcpy(a_, a, M - 1);\n\t\t\ta_ += M - 1;",
        "std::memcpy(a_, p, N - 1);\n\t\t\ta_ += N - 1;"),
    # Slavo-Germanic: K no longer counts
    "slavo_no_k": ("slavo |= m == 'W' || m == 'K' ||", "slavo |= m == 'W' ||"),
    # Slavo-Germanic: W no longer counts
    "slavo_no_w": ("slavo |= m == 'W' || m == 'K' ||", "slavo |= m == 'K' ||"),
    # Slavo-Germanic: CZ checks the wrong letter
    "slavo_cz": ("out[-1] == 'C')", "out[-1] == 'S')"),
    # a window that ends exactly at the last letter no longer matches
    "match_at_end": ("static_cast<std::size_t>(start) + (N - 1) <= len_", "static_cast<std::size_t>(start) + (N - 1) < len_"),
    # a window is not compared past its first byte
    "match_at_short": ("std::memcmp(first_ + start, what, N - 1) == 0", "std::memcmp(first_ + start, what, 1) == 0"),
    # tabs are dropped instead of read as spaces
    "tab_dropped": ("\t\tmap['\\t'] = ' ';\n", ""),
    # lower-case letters are not folded
    "lower_not_folded": ("map[c + ('a' - 'A')] = static_cast<char>(c);", "map[c + ('a' - 'A')] = static_cast<char>(c + 1);"),
    # runs of spaces are not collapsed
    "spaces_not_collapsed": ("} else if (m == ' ' && out[-1] != ' ') {", "} else if (m == ' ') {"),
    # a trailing space is kept
    "trailing_space_kept": ("while (out != first_ && out[-1] == ' ') {\n\t\t\t--out;\n\t\t}", ""),
    # Y is not a vowel
    "y_not_vowel": ("for (char c : {'A', 'E', 'I', 'O', 'U', 'Y'}) {", "for (char c : {'A', 'E', 'I', 'O', 'U'}) {"),
    # the blanks before the first letter read as a vowel
    "lead_pad_vowel": ("std::memset(base, ' ', lead);", "std::memset(base, 'A', lead);"),
    # the blanks after the last letter read as a vowel
    "trail_pad_vowel": ("std::memset(out, ' ', trail);", "std::memset(out, 'E', trail);"),
    # "VAN " loses its space
    "germanic_van": ('w.match_at(0, "VAN ")', 'w.match_at(0, "VAN")'),
    # the set test misses its last member
    "is_any_last": ("for (std::size_t k = 0; k + 1 < N; ++k) {", "for (std::size_t k = 0; k + 2 < N; ++k) {"),
    # "PS" no longer loses its first letter
    "initial_ps": ('w.match_at(0, "WR") || w.match_at(0, "PS")', 'w.match_at(0, "WR")'),
    # "GN" no longer loses its first letter
    "initial_gn": ('if (w.match_at(0, "GN") || w.match_at(0, "KN")', 'if (w.match_at(0, "KN")'),
    # an initial X reads as X instead of S
    "initial_x_s": ('keys.add("S");  // \'Z\' sound encodes to S in Double Metaphone', 'keys.add("X");'),
}

if len(MUTANTS) < 19:
    raise SystemExit("mutant table shrank")

work = os.environ.get("MUT_DIR") or tempfile.mkdtemp(prefix="dm_mutants_")
os.makedirs(work, exist_ok=True)
survivors = []
for name, (old, new) in MUTANTS.items():
    if only and name not in only:
        continue
    if src.count(old) != 1:
        print(f"{name}: pattern matches {src.count(old)} times, fix the table")
        survivors.append(name)
        continue
    d = os.path.join(work, name)
    os.makedirs(d, exist_ok=True)
    with open(os.path.join(d, "double_metaphone.cc"), "w", encoding="utf-8") as f:
        f.write(src.replace(old, new))
    exe = os.path.join(d, "diff_test")
    cmd = [cxx, "-std=c++20", "-O1", "-I" + tree, "-I" + os.path.join(tree, "test"),
           os.path.join(d, "double_metaphone.cc"), os.path.join(tree, "test/double_metaphone_old.cc"),
           os.path.join(tree, "test/diff_test.cc"), "-o", exe]
    built = subprocess.run(cmd, capture_output=True, text=True)
    if built.returncode != 0:
        print(f"{name}: does not compile\n{built.stderr[:400]}")
        survivors.append(name)
        continue
    try:
        ran = subprocess.run([exe], capture_output=True, text=True, timeout=120)
    except subprocess.TimeoutExpired:
        print(f"{name}: killed (the test does not finish)")
        shutil.rmtree(d, ignore_errors=True)
        continue
    if ran.returncode == 0:
        print(f"{name}: SURVIVED")
        survivors.append(name)
    else:
        print(f"{name}: killed (exit {ran.returncode})")
    shutil.rmtree(d, ignore_errors=True)

if survivors:
    print("survivors:", ", ".join(survivors))
    sys.exit(1)
print("all mutants killed")
