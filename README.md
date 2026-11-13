# QriWER

AHK v2.1 native library reproducing JiWER 4.0.0 `wer_default` word error rate.

## What it is

A C ABI shim (`qriwer_shim.dll`) compiled from `src/qriwer_shim.cpp` that links
RapidFuzz-CPP (header-only) as its alignment engine. An AHK v2.1 wrapper
(`lib/QriWER.ahk`) calls the DLL and returns a result object. No Python, no
subprocess, no additional runtime dependencies.

## Build

### Prerequisites
- Visual Studio 2022 (any edition) with the C++ Desktop workload
- AutoHotkey v2.1+ (`#Requires AutoHotkey v2.1-`)
- `resources/rapidfuzz-cpp` (header-only, gitignored)

### Steps

```bat
:: From the project root
scratch\build_shim.bat
```

This runs `vcvars64.bat` then `cl.exe /std:c++17 /O2` to produce
`qriwer_shim.dll` in the project root.

### Verify

```bat
scratch\build_test.bat    &&   echo C tests passed
scratch\run_smoke_ahk.bat &&   echo AHK tests passed
python scratch\corpus_validate.py && echo JiWER 1:1 match
```

## Public C ABI

```c
typedef struct {
    int64_t hits;          // matching word positions
    int64_t substitutions; // replace ops
    int64_t deletions;     // delete ops
    int64_t insertions;    // insert ops
    int64_t ref_words;     // total reference word count
    int64_t hyp_words;     // total hypothesis word count
    double  wer;           // (S+D+I) / ref_words, or I if ref_words==0
    int     error;         // 0 = success, nonzero = failure
} QriWERResult;

int __stdcall qriwer_compare(const wchar_t* reference,
                             const wchar_t* hypothesis,
                             QriWERResult* result);

const char* __stdcall qriwer_version(void);
```

### Conventions
- UTF-16 strings in; no ownership transfer (caller owns both input strings)
- `NULL` inputs are treated as empty strings
- `result == NULL` returns 1 without calling the alignment engine
- Exceptions are caught internally; never cross the DLL boundary
- Thread-safe: each call is stateless

### Tokenization (JiWER 4.0.0 `wer_default`)
1. Collapse runs of 2+ Unicode whitespace to a single space (`re.sub(r"\s\s+", " ")`)
2. Strip leading/trailing Unicode whitespace (`str.strip()`)
3. Split on space, drop empty tokens
4. Map words to shared auto-increment integer IDs
5. Call `rapidfuzz::levenshtein_editops` with uniform weights `{1,1,1}`
6. Count ops: Replace→subs, Delete→dels, Insert→ins; hits = ref_len − subs − dels

## AHK v2.1 Usage

```ahk
#Requires AutoHotkey v2.1-
#Include <QriWER>   ; lib/QriWER.ahk, with lib/ on A_IncludePath

r := QriWER.Compare("The quick brown fox", "The quick brown fox")
msgbox r.hits " " r.substitutions " " r.wer

r2 := QriWER.Compare("a b c", "a x c y")
; r2.hits=2, r2.substitutions=1, r2.deletions=0, r2.insertions=1, r2.wer≈0.667
```

The DLL is auto-discovered from:
1. `A_ScriptDir\qriwer_shim.dll`
2. `A_ScriptDir\..\qriwer_shim.dll`
3. `A_ScriptDir\..\..\qriwer_shim.dll`
4. `A_ScriptDir\bin\qriwer_shim.dll`

Or pass an explicit path: `QriWER.Init("C:\path\to\qriwer_shim.dll")`.

## Validation

### C test harness (`scratch/test_shim.c`)
24 hand-computed edge cases: identical, empty, all-del, all-ins, mixed ops,
whitespace collapse, tab-in-word, punctuation, repeated words, case
sensitivity, Unicode (U+00E9), null-safety, WER > 1.0.

### JiWER 1:1 corpus validation (`scratch/corpus_validate.py`)
796 paired reference/hypothesis chunks compared field-by-field
(hits, substitutions, deletions, insertions, ref_words, hyp_words, wer).
All 796 pairs match JiWER 4.0.0 exactly (WER tolerance: 1e-9).

## File layout

```
QriWER/
├── qriwer_shim.dll          # built DLL (gitignored)
├── src/
│   ├── qriwer_shim.cpp      # C shim + RapidFuzz integration
│   └── qriwer_ws_table.h    # Unicode whitespace ranges (generated)
├── lib/
│   ├── QriWER.ahk           # AHK v2.1 wrapper
│   └── Aris/g33kdude/cjson.ahk  # JSON lib for test harnesses
├── scratch/                 # build scripts, test harnesses (gitignored)
├── chunks/                  # validation corpus (gitignored)
└── resources/rapidfuzz-cpp/ # header-only dependency (gitignored)
```
