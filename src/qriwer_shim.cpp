// QriWER native shim — JiWER 4.0.0 word & character error rates + RapidFuzz alignment.
// C ABI, UTF-16 in, no exceptions across boundary.
//
// Exports:
//   qriwer_compare(ref, hyp, QriWERResult*) -> int
//     Computes WER + MER + WIP + WIL using wer_default tokenization.
//
//   qriwer_cer(ref, hyp, QriWERResult*) -> int
//     Computes CER using cer_default tokenization (strip + char-level).
//     Returns counts in same struct; 'wer' field holds the CER value.
//
//   qriwer_version() -> const char*

#define WIN32_LEAN_AND_MEAN
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <unordered_map>
#include <cwchar>
#include <climits>
#include <limits>

#include <rapidfuzz/distance.hpp>
#include "qriwer_ws_table.h"

// ---------------------------------------------------------------------------
// Result struct (C-compatible, no STL)
// ---------------------------------------------------------------------------
typedef struct {
    int64_t hits;
    int64_t substitutions;
    int64_t deletions;
    int64_t insertions;
    int64_t ref_words;   // for CER: ref_char_count
    int64_t hyp_words;   // for CER: hyp_char_count
    double  wer;         // for CER: cer value
    double  mer;
    double  wip;
    double  wil;
    int     error;       // 0 = success, nonzero = failure
} QriWERResult;

// ---------------------------------------------------------------------------
// Internal: Unicode whitespace check (from generated table)
// ---------------------------------------------------------------------------
static inline bool is_ws(uint32_t cp) {
    for (unsigned int i = 0; i < kWsRangeCount; ++i)
        if (cp >= kWsRanges[i].start && cp <= kWsRanges[i].end)
            return true;
    return false;
}

// ---------------------------------------------------------------------------
// JiWER wer_default pipeline (word-level):
//   1. RemoveMultipleSpaces: re.sub(r"\s\s+", " ", s)
//   2. Strip: s.strip()
//   3. ReduceToListOfListOfWords: split(" "), drop empty
// ---------------------------------------------------------------------------
static std::vector<std::wstring> tokenize_words(const wchar_t* text) {
    if (!text || !*text) return {};

    std::wstring s(text);

    // Step 1: collapse every maximal run of 2+ ws to single space; single ws stays
    {
        std::wstring out;
        out.reserve(s.size());
        for (size_t i = 0; i < s.size();) {
            if (is_ws((uint32_t)s[i])) {
                size_t j = i;
                while (j < s.size() && is_ws((uint32_t)s[j])) j++;
                out += (j - i >= 2) ? L' ' : s[i];
                i = j;
            } else {
                out += s[i];
                i++;
            }
        }
        s = std::move(out);
    }

    // Step 2: strip leading/trailing whitespace
    {
        size_t b = 0, e = s.size();
        while (b < e && is_ws((uint32_t)s[b])) b++;
        while (e > b && is_ws((uint32_t)s[e-1])) e--;
        s = s.substr(b, e - b);
    }

    // Step 3: split on space, drop empty
    std::vector<std::wstring> words;
    if (s.empty()) return words;
    size_t start = 0;
    for (size_t i = 0; i <= s.size(); ++i) {
        if (i == s.size() || s[i] == L' ') {
            if (i > start)
                words.push_back(s.substr(start, i - start));
            start = i + 1;
        }
    }
    return words;
}

// ---------------------------------------------------------------------------
// JiWER cer_default pipeline (char-level):
//   1. Strip: s.strip()
//   2. ReduceToListOfListOfChars: split into individual characters
// ---------------------------------------------------------------------------
static std::vector<std::wstring> tokenize_chars(const wchar_t* text) {
    if (!text || !*text) return {};

    std::wstring s(text);

    // Strip leading/trailing whitespace
    size_t b = 0, e = s.size();
    while (b < e && is_ws((uint32_t)s[b])) b++;
    while (e > b && is_ws((uint32_t)s[e-1])) e--;
    s = s.substr(b, e - b);

    // Each character is a token (including internal spaces)
    std::vector<std::wstring> chars;
    chars.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i)
        chars.push_back(std::wstring(1, s[i]));
    return chars;
}

// ---------------------------------------------------------------------------
// Word/char → int mapping (shared auto-incrementing vocab)
// ---------------------------------------------------------------------------
struct IntPairs {
    std::vector<int32_t> ref;
    std::vector<int32_t> hyp;
};

static IntPairs tokens_to_ints(const std::vector<std::wstring>& ref,
                               const std::vector<std::wstring>& hyp) {
    IntPairs out;
    out.ref.reserve(ref.size());
    out.hyp.reserve(hyp.size());
    std::unordered_map<std::wstring, int32_t> vocab;
    int32_t next_id = 0;

    for (const auto& t : ref) {
        auto it = vocab.find(t);
        if (it == vocab.end())
            out.ref.push_back(vocab.emplace(t, next_id++).first->second);
        else
            out.ref.push_back(it->second);
    }
    for (const auto& t : hyp) {
        auto it = vocab.find(t);
        if (it == vocab.end())
            out.hyp.push_back(vocab.emplace(t, next_id++).first->second);
        else
            out.hyp.push_back(it->second);
    }
    return out;
}

// ---------------------------------------------------------------------------
// Core: run Levenshtein, compute all measures
// ---------------------------------------------------------------------------
static void compute_measures(int64_t ref_len, int64_t hyp_len,
                             int64_t hits, int64_t subs, int64_t dels, int64_t ins,
                             QriWERResult* r)
{
    r->hits = hits;
    r->substitutions = subs;
    r->deletions = dels;
    r->insertions = ins;
    r->ref_words = ref_len;
    r->hyp_words = hyp_len;

    // JiWER 4.0.0 formula (process.py lines 196-216)
    if (ref_len == 0) {
        r->wer = (double)ins;
        if (hyp_len == 0) {
            r->mer = 0.0;
            r->wip = 1.0;
        } else {
            r->mer = 1.0;
            r->wip = 0.0;
        }
    } else {
        double S = (double)subs, D = (double)dels, I = (double)ins, H = (double)hits;
        r->wer = (S + D + I) / (H + S + D);
        r->mer = (S + D + I) / (H + S + D + I);
        if (hyp_len >= 1)
            r->wip = (H / (double)ref_len) * (H / (double)hyp_len);
        else
            r->wip = 0.0;
    }
    r->wil = 1.0 - r->wip;
}

// ---------------------------------------------------------------------------
// Public C ABI
// ---------------------------------------------------------------------------
extern "C" __declspec(dllexport) int __stdcall qriwer_compare(
    const wchar_t* reference,
    const wchar_t* hypothesis,
    QriWERResult* result)
{
    if (!result) return 1;
    result->error = 1;
    result->hits = result->substitutions = result->deletions = 0;
    result->insertions = result->ref_words = result->hyp_words = 0;
    result->wer = result->mer = result->wip = result->wil = 0.0;

    try {
        auto ref_tokens = tokenize_words(reference);
        auto hyp_tokens = tokenize_words(hypothesis);
        int64_t ref_len = (int64_t)ref_tokens.size();
        int64_t hyp_len = (int64_t)hyp_tokens.size();

        if (ref_len == 0 && hyp_len == 0) {
            // Both empty: all zeros, WER=0, MER=0, WIP=1, WIL=0
            compute_measures(0, 0, 0, 0, 0, 0, result);
            result->error = 0;
            return 0;
        }

        auto wi = tokens_to_ints(ref_tokens, hyp_tokens);
        auto ops = rapidfuzz::levenshtein_editops(wi.ref, wi.hyp);

        int64_t subs = 0, ins = 0, dels = 0;
        for (const auto& op : ops) {
            switch (op.type) {
                case rapidfuzz::EditType::Replace: subs++; break;
                case rapidfuzz::EditType::Insert:  ins++;  break;
                case rapidfuzz::EditType::Delete:  dels++; break;
                default: break;
            }
        }
        int64_t hits = ref_len - subs - dels;

        compute_measures(ref_len, hyp_len, hits, subs, dels, ins, result);
        result->error = 0;
        return 0;
    } catch (...) {
        result->error = 2;
        return 2;
    }
}

// Character Error Rate (JiWER cer_default: strip + char-level Levenshtein)
extern "C" __declspec(dllexport) int __stdcall qriwer_cer(
    const wchar_t* reference,
    const wchar_t* hypothesis,
    QriWERResult* result)
{
    if (!result) return 1;
    result->error = 1;
    result->hits = result->substitutions = result->deletions = 0;
    result->insertions = result->ref_words = result->hyp_words = 0;
    result->wer = result->mer = result->wip = result->wil = 0.0;

    try {
        auto ref_chars = tokenize_chars(reference);
        auto hyp_chars = tokenize_chars(hypothesis);
        int64_t ref_len = (int64_t)ref_chars.size();
        int64_t hyp_len = (int64_t)hyp_chars.size();

        if (ref_len == 0 && hyp_len == 0) {
            compute_measures(0, 0, 0, 0, 0, 0, result);
            result->error = 0;
            return 0;
        }

        auto wi = tokens_to_ints(ref_chars, hyp_chars);
        auto ops = rapidfuzz::levenshtein_editops(wi.ref, wi.hyp);

        int64_t subs = 0, ins = 0, dels = 0;
        for (const auto& op : ops) {
            switch (op.type) {
                case rapidfuzz::EditType::Replace: subs++; break;
                case rapidfuzz::EditType::Insert:  ins++;  break;
                case rapidfuzz::EditType::Delete:  dels++; break;
                default: break;
            }
        }
        int64_t hits = ref_len - subs - dels;

        compute_measures(ref_len, hyp_len, hits, subs, dels, ins, result);
        result->error = 0;
        return 0;
    } catch (...) {
        result->error = 2;
        return 2;
    }
}

extern "C" __declspec(dllexport) const char* __stdcall qriwer_version() {
    return "QriWER 1.1.0 / RapidFuzz-CPP header-only / JiWER 4.0.0 compatible";
}
