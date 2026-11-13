# QriWER

AHK v2.1 native library reproducing JiWER 4.0.0's core error-rate measures,
using RapidFuzz-CPP (header-only) as the sequence-alignment engine.

## What it is

A C ABI shim (`QriWER.dll`) compiled from `src/qriwer_shim.cpp` that links
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
- [RapidFuzz-CPP](https://github.com/rapidfuzz/rapidfuzz-cpp) (header-only) — clone into `resources/rapidfuzz-cpp`

### Steps

```bat
:: From the project root
src\build_shim.bat
```

This calls `vcvars64.bat`, then compiles with:

```bat
cl.exe /nologo /std:c++17 /O2 /MD /W4 /EHsc /permissive- /utf-8 ^
  /I resources\rapidfuzz-cpp /I src ^
  src\qriwer_shim.cpp ^
  /link /DLL /OUT:QriWER.dll /IMPLIB:QriWER.lib
```

producing `QriWER.dll` (and the import stub `QriWER.lib`) in the project root.

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
if r["error"]
    return   ; check r["error"] before using results
; r["hits"]=4, r["substitutions"]=0, r["wer"]=0

r2 := QriWER.Compare("a b c", "a x c y")
; r2["hits"]=2, r2["substitutions"]=1, r2["wer"]≈0.667

; Character-level: CER
c := QriWER.CER("kitten", "sitting")
; c["hits"]=4, c["substitutions"]=2, c["cer"]=0.5

; Existence check:
; r.Has("mer") -> 1
```

`Compare` and `CER` **never throw** — they always return a fully-populated Map.
On success `r["error"]` is `0`; on failure it is non-zero and the numeric fields
are zero. `Init`/`Release` still throw, since DLL loading is a prerequisite,
not an operation result.

### Sample result

A lightly-edited 20-word line (the hypothesis only drops the two surrounding
quote marks) run through both entry points:

```text
reference : His countenance instantly assumed an aspect of the deepest gloom, and he replied, "To seek one who fled from me."
hypothesis: His countenance instantly assumed an aspect of the deepest gloom, and he replied, To seek one who fled from me.
```

```python
QriWER.Compare(reference, hypothesis)   # word-level
{
  "error":         0,
  "hits":          18,
  "substitutions": 2,
  "deletions":     0,
  "insertions":    0,
  "ref_words":     20,
  "hyp_words":     20,
  "wer":  0.10,    # (S+D+I)/(H+S+D)   = 2/20
  "mer":  0.10,    # (S+D+I)/(H+S+D+I) = 2/20
  "wip":  0.81,    # (H/refW)*(H/hypW) = (18/20)²
  "wil":  0.19     # 1 - wip
}

QriWER.CER(reference, hypothesis)       # character-level
{
  "error":         0,
  "hits":          111,
  "substitutions": 0,
  "deletions":     2,   # the two dropped " quote marks
  "insertions":    0,
  "ref_chars":     113,
  "hyp_chars":     111,
  "cer": 0.0177         # 2/113
}
```

Notice the gap: **WER 0.10** vs. **CER 0.0177**. At word level the dropped
quotes fuse onto the surrounding tokens (`"To`, `me."`), so the aligner counts
2 of 20 words as substituted. At character level only the 2 quote characters
differ (out of 113). CER is therefore the better choice for lightly-edited
text; WER is the better choice when whole tokens (misrecognized words) are the
concern. Float fields are shown rounded — the shim returns full precision
(e.g. `wil` is exactly `0.18999999999999995`).

The DLL is auto-discovered in this order:

1. **Versioned Aris install** (libmagic pattern):
   the latest `lib\Aris\Qriist\QriWER@*\bin\QriWER.dll`
2. **Naive `A_ScriptDir` candidates**:
   - `A_ScriptDir\QriWER.dll`
   - `A_ScriptDir\..\QriWER.dll`
   - `A_ScriptDir\..\..\QriWER.dll`
   - `A_ScriptDir\..\bin\QriWER.dll`
   - `A_ScriptDir\bin\QriWER.dll`

Or pass an explicit path: `QriWER.Init("C:\path\to\QriWER.dll")`.


