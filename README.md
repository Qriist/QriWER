# QriWER

AHK v2.1 native library reproducing JiWER 4.0.0's core error-rate measures,
using RapidFuzz-CPP (header-only) as the sequence-alignment engine.

## What it is

A C ABI shim (`qriwer_shim.dll`) compiled from `src/qriwer_shim.cpp` that links
RapidFuzz-CPP as its alignment engine. An AHK v2.1 wrapper (`lib/QriWER.ahk`)
calls the DLL and returns result objects. No Python, no subprocess, no
additional runtime dependencies.

## Supported measures (JiWER 4.0.0 defaults)

| Measure | Meaning | Pipeline |
|---------|---------|----------|
| `wer` | Word Error Rate `(S+D+I)/(H+S+D)` | `wer_default` |
| `mer` | Minimum Edit Rate `(S+D+I)/(H+S+D+I)` | `wer_default` |
| `wip` | Word Insertion Penalty `(H/refW)*(H/hypW)` | `wer_default` |
| `wil` | Word Insertion Length `1 - wip` | `wer_default` |
| `cer` | Character Error Rate `(S+D+I)/(H+S+D)` | `cer_default` |

plus the integer counts `hits`, `substitutions`, `deletions`, `insertions`,
and token counts `ref_words` / `hyp_words` (for CER, `ref_chars` / `hyp_chars`).

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
scratch\run_detached.bat scratch\smoke_ahk.ahk scratch\smoke_ahk_result.json
python scratch\corpus_validate.py   &&   echo JiWER 1:1 match
```

## Public C ABI

All functions are `__stdcall`, take/return UTF-16, and never throw across the
DLL boundary.

```c
typedef struct {
    int64_t hits;          // matching positions
    int64_t substitutions; // replace ops
    int64_t deletions;     // delete ops
    int64_t insertions;    // insert ops
    int64_t ref_words;     // reference token count  (ref_chars for CER)
    int64_t hyp_words;     // hypothesis token count (hyp_chars for CER)
    double  wer;           // WER (holds CER for the cer export)
    double  mer;           // Minimum Edit Rate
    double  wip;           // Word Insertion Penalty
    double  wil;           // Word Insertion Length
    int     error;         // 0 = success, 1 = bad result ptr, 2 = internal
} QriWERResult;           // sizeof == 88 bytes

// Word-level: WER + MER + WIP + WIL (wer_default tokenization)
int __stdcall qriwer_compare(const wchar_t* reference,
                             const wchar_t* hypothesis,
                             QriWERResult* result);

// Character-level: CER (cer_default tokenization).
//   .ref_words/.hyp_words hold char counts; .wer holds the CER value.
int __stdcall qriwer_cer(const wchar_t* reference,
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

### Formulas (JiWER 4.0.0 `process.py`)
Let `H,S,D,I` be hits/subs/dels/ins and `refW`, `hypW` the token counts.

```
wer = (refW == 0) ? I : (S+D+I)/(H+S+D)
mer = (refW == 0) ? (hypW == 0 ? 0 : 1) : (S+D+I)/(H+S+D+I)
wip = (refW == 0) ? (hypW == 0 ? 1 : 0)
      : (hypW >= 1 ? (H/refW)*(H/hypW) : 0)
wil = 1 - wip
cer = (refC == 0) ? I : (S+D+I)/(H+S+D)   # char-level
```

### Tokenization
- **`wer_default`**: collapse runs of 2+ Unicode whitespace to a single space
  (`re.sub(r"\s\s+", " ")`) → strip leading/trailing whitespace (`str.strip()`)
  → split on space, drop empty → map words to shared auto-increment int IDs.
- **`cer_default`**: strip leading/trailing whitespace → each character is a
  token (including internal spaces) → map to shared int IDs.
- Alignment: `rapidfuzz::levenshtein_editops` with uniform weights `{1,1,1}`.
  `Replace→subs`, `Delete→dels`, `Insert→ins`; `hits = ref_len − subs − dels`.
- Whitespace table covers all 29 Python `str.isspace()` / `re\s` code points
  (generated, verified 0 mismatches over 0x110000 code points).

## AHK v2.1 Usage

```ahk
#Requires AutoHotkey v2.1-
#Include <QriWER>   ; lib/QriWER.ahk, with lib/ on A_IncludePath

; Word-level: WER + MER + WIP + WIL
r := QriWER.Compare("The quick brown fox", "The quick brown fox")
; r.hits=4, r.substitutions=0, r.deletions=0, r.insertions=0, r.wer=0

r2 := QriWER.Compare("a b c", "a x c y")
; r2.hits=2, r2.substitutions=1, r2.deletions=0, r2.insertions=1, r2.wer≈0.667

; Character-level: CER
c := QriWER.CER("kitten", "sitting")
; c.hits=4, c.substitutions=2, c.deletions=0, c.insertions=1, c.cer=0.5
```

The DLL is auto-discovered from:
1. `A_ScriptDir\qriwer_shim.dll`
2. `A_ScriptDir\..\qriwer_shim.dll`
3. `A_ScriptDir\..\..\qriwer_shim.dll`
4. `A_ScriptDir\bin\qriwer_shim.dll`

Or pass an explicit path: `QriWER.Init("C:\path\to\qriwer_shim.dll")`.

## Validation

| Harness | Scope | Result |
|---------|-------|--------|
| C smoke test (`scratch/test_shim.c`) | hand-computed edge cases | pass |
| AHK smoke test (`scratch/smoke_ahk.ahk`) | 7 cases across WER/MER/WIP/WIL/CER | 7/7 |
| JiWER corpus (`scratch/corpus_validate.py`) | 796 paired chunks, all 5 measures + counts | 796/796, 0 mismatches |

Corpus pairing: `chunks/pre-chunks.json[i]` (reference = `speech` key) vs
`chunks/post-chunks/%06d(i+1)/out.txt` (hypothesis). Index 0 has empty speech
(intentional skip). All measures compared within 1e-9 tolerance.

## File layout

```
QriWER/
├── qriwer_shim.dll          # built DLL (gitignored)
├── src/
│   ├── qriwer_shim.cpp      # C shim + RapidFuzz integration (WER+MER+WIP+WIL+CER)
│   └── qriwer_ws_table.h    # Unicode whitespace ranges (generated)
├── lib/
│   ├── QriWER.ahk           # AHK v2.1 wrapper (Compare + CER + Version)
│   └── Aris/g33kdude/cjson.ahk  # JSON lib for test harnesses
├── scratch/                 # build scripts, test harnesses (gitignored)
├── chunks/                  # validation corpus (gitignored)
└── resources/rapidfuzz-cpp/ # header-only dependency (gitignored)
```
