#!/usr/bin/env python3
"""Check that every source file includes the standard headers it uses.

C++ projects silently depend on transitive includes. This is harmless until a
newer standard library stops providing them, at which point the whole project
fails to compile on a new toolchain while still building on the old one -- which
is exactly what happened here: with libstdc++ from GCC 16, <string> no longer
brings in <cstdint>, and <vector> no longer brings in <algorithm>, so
``std::uint32_t`` and ``std::remove_if`` stopped resolving across the codebase.

This script scans for uses of std facilities and reports any file that uses one
without including the header that declares it.

Usage:
    python3 scripts/check_includes.py           # report problems, exit 1 if any
    python3 scripts/check_includes.py --apply   # add the missing includes in place

The rules are deliberately conservative (a grep-level approximation, not a
compiler): they err towards asking for an explicit include when a symbol is
used, which is harmless, and never remove includes.
"""

import os, re, sys

RULES = [
    (r'std::(u?int(8|16|32|64)_t|intptr_t|uintptr_t|intmax_t|uintmax_t)\b', 'cstdint'),
    (r'std::(size_t|ptrdiff_t|nullptr_t|byte)\b', 'cstddef'),
    (r'\bstd::(remove_if|remove|sort|stable_sort|find|find_if|find_first_of|any_of|all_of|none_of|count|count_if|transform|fill|fill_n|copy|copy_if|copy_n|lower_bound|upper_bound|unique|min|max|min_element|max_element|clamp|reverse|rotate|for_each|replace|shuffle|set_union|set_difference|set_intersection|binary_search|equal|mismatch|partition|nth_element|merge|includes)\s*[(<]', 'algorithm'),
    (r'\bstd::(accumulate|iota|gcd|lcm)\s*[(<]', 'numeric'),
    (r'\bstd::(numeric_limits)\b', 'limits'),
    (r'\bstd::(memcpy|memset|memcmp|memmove|strlen|strcmp|strncmp|strcpy|strncpy|strstr|strchr)\b', 'cstring'),
    (r'\bstd::(move|forward|swap|exchange|pair|make_pair|declval|as_const|in_place_t)\b', 'utility'),
    (r'\bstd::(string|to_string|stoi|stol|stof|stod|getline|char_traits)\b', 'string'),
    (r'\bstd::(string_view)\b', 'string_view'),
    (r'\bstd::(vector|to_array)\b', 'vector'),
    (r'\bstd::(array)\b', 'array'),
    (r'\bstd::(map|multimap)\b', 'map'),
    (r'\bstd::(unordered_map)\b', 'unordered_map'),
    (r'\bstd::(unordered_set)\b', 'unordered_set'),
    (r'\bstd::(set|multiset)\b', 'set'),
    (r'\bstd::(unique_ptr|shared_ptr|weak_ptr|make_unique|make_shared|enable_shared_from_this|default_delete|static_pointer_cast|dynamic_pointer_cast)\b', 'memory'),
    (r'\bstd::(optional|nullopt|in_place)\b', 'optional'),
    (r'\bstd::(variant|monostate|visit|get_if)\b', 'variant'),
    (r'\bstd::(function|bind|ref|cref|reference_wrapper|hash|greater|less|equal_to|invoke)\b', 'functional'),
    (r'\bstd::filesystem\b', 'filesystem'),
    (r'\bstd::(ostringstream|istringstream|stringstream|stringbuf)\b', 'sstream'),
    (r'\bstd::(chrono|duration_cast|milliseconds|steady_clock|system_clock)\b', 'chrono'),
    (r'\bstd::(atomic|atomic_flag|memory_order)\b', 'atomic'),
    (r'\bstd::(mutex|lock_guard|unique_lock|recursive_mutex|scoped_lock)\b', 'mutex'),
    (r'\bstd::(ceil|floor|sqrt|fabs|fmod|round|pow|sin|cos|tan|isnan|isfinite|infinity|log|log2|log10|exp|fmin|fmax|trunc)\s*[(<]', 'cmath'),
    (r'\bstd::(strtol|strtoul|strtod|atoi|atof|abs|getenv|abort|exit|malloc|free|qsort|rand|srand)\b', 'cstdlib'),
    (r'\bstd::(printf|fprintf|snprintf|sprintf|FILE|fopen|fclose|puts|fputs|fwrite|fread|fflush)\b', 'cstdio'),
    (r'\bstd::(isdigit|isalpha|isalnum|isspace|tolower|toupper|isxdigit)\b', 'cctype'),
    (r'\bstd::(runtime_error|logic_error|invalid_argument|out_of_range|domain_error|exception|bad_alloc|bad_cast)\b', 'stdexcept'),
    (r'\bstd::(thread|this_thread)\b', 'thread'),
    (r'\bstd::(ifstream|ofstream|fstream|ios_base)\b', 'fstream'),
    (r'\bstd::(cout|cerr|clog|cin|ostream|istream|endl)\b', 'iostream'),
    (r'\bstd::(setw|setfill|setprecision|fixed|scientific|hex|dec|quoted)\b', 'iomanip'),
    (r'\bstd::(tuple|tie|make_tuple|get<\w+>)\b', 'tuple'),
    (r'\bstd::(initializer_list)\b', 'initializer_list'),
    (r'\bstd::(is_same|is_base_of|decay_t|remove_reference|remove_cv|enable_if|conditional)\b', 'type_traits'),
    (r'\bstd::(begin|end|data|next|prev|distance|advance|back_inserter)\s*\(', 'iterator'),
    (r'\bstd::(regex|regex_match|regex_search|smatch)\b', 'regex'),
]

# 'abs' is provided by both cmath and cstdlib; treat either as satisfying it.
SOFT = {'cstdlib': {'cmath'}, 'cmath': {'cstdlib'}}

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
os.chdir(REPO_ROOT)

roots = ['engine', 'formats', 'asset_pipeline', 'studio', 'plugins', 'exporters', 'apps', 'tests', 'templates']
files = []
for root in roots:
    if not os.path.isdir(root):
        continue
    for dirpath, _, names in os.walk(root):
        for n in names:
            if n.endswith(('.h', '.hpp', '.cpp', '.cc')):
                files.append(os.path.join(dirpath, n))

apply = '--apply' in sys.argv
changed = []

for path in sorted(files):
    text = open(path, encoding='utf-8').read()
    includes = set(re.findall(r'#\s*include\s*[<"]([^>"]+)[>"]', text))
    body = re.sub(r'//[^\n]*', '', text)
    body = re.sub(r'/\*.*?\*/', '', body, flags=re.S)
    missing = set()
    for pattern, header in RULES:
        if header in includes:
            continue
        if re.search(pattern, body):
            if includes & SOFT.get(header, set()):
                continue
            missing.add(header)
    if not missing:
        continue
    changed.append((path, sorted(missing)))
    if not apply:
        continue

    lines = text.split('\n')
    # Locate the first contiguous run of <...> includes.
    run = []
    for i, line in enumerate(lines):
        m = re.match(r'#include <([^>]+)>', line.strip())
        if m:
            run.append((i, m.group(1)))
        elif run:
            break
    if run:
        entries = [h for _, h in run]
        start, end = run[0][0], run[-1][0]
        for header in sorted(missing):
            pos = 0
            while pos < len(entries) and entries[pos] < header:
                pos += 1
            entries.insert(pos, header)
        new_block = ['#include <%s>' % h for h in entries]
        lines[start:end + 1] = new_block
    else:
        block = [''] + ['#include <%s>' % h for h in sorted(missing)]
        # Insert after leading comments / #pragma once, before the first include.
        insert_at = 0
        in_block_comment = False
        for i, line in enumerate(lines):
            s = line.strip()
            if in_block_comment:
                insert_at = i + 1
                if '*/' in s:
                    in_block_comment = False
                continue
            if s.startswith('//') or not s:
                insert_at = i + 1
            elif s.startswith('/*'):
                insert_at = i + 1
                in_block_comment = '*/' not in s
            elif s.startswith('#pragma'):
                insert_at = i + 1
            else:
                break
        first_include = next((i for i, l in enumerate(lines) if l.strip().startswith('#include')), None)
        if first_include is not None and first_include < insert_at:
            # pragma-less file whose includes start earlier; insert right above them
            insert_at = first_include
        lines[insert_at:insert_at] = block
    open(path, 'w', encoding='utf-8').write('\n'.join(lines))

for path, headers in changed:
    print(f"{path}: {', '.join(headers)}")
if changed:
    verb = 'updated' if apply else 'need'
    print(f"\n{len(changed)} of {len(files)} file(s) {verb} explicit includes")
    if not apply:
        print("Run with --apply to add them.")
else:
    print(f"all {len(files)} source file(s) include the standard headers they use")

sys.exit(1 if changed and not apply else 0)
