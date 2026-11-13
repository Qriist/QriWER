// QriWER native shim — JiWER 4.0.0 wer_default pipeline + RapidFuzz alignment.
// C ABI, UTF-16 in, no exceptions across boundary.
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
    int64_t ref_words;
    int64_t hyp_words;
    double  wer;
    int     error;   // 0 = success, nonzero = failure
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
// JiWER wer_default pipeline:
//   1. RemoveMultipleSpaces: re.sub(r"\s\s+", " ", s)
//   2. Strip: s.strip()
//   3. ReduceToListOfListOfWords: split(" "), drop empty
// ---------------------------------------------------------------------------
static std::vector<std::wstring> jiwer_tokenize(const wchar_t* text) {
    // If null or empty, return empty vector
    if (!text || !*text) return {};

    std::wstring s(text);

    // Step 1: re.sub(r"\s\s+", " ", s) — collapse every maximal run of 2+
    // whitespace chars into a single space; a single whitespace char is left as-is
    // (so a lone tab stays a tab and remains *inside* a word, matching Python re).
    {
        std::wstring out;
        out.reserve(s.size());
        for (size_t i = 0; i < s.size();) {
            if (is_ws((uint32_t)s[i])) {
                size_t j = i;
                while (j < s.size() && is_ws((uint32_t)s[j])) j++;
                if (j - i >= 2)
                    out += L' ';   // run of 2+  -> single space
                else
                    out += s[i];   // single ws char -> keep original
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

    // Step 3: split on space, drop empty strings
    std::vector<std::wstring> words;
    if (s.empty()) return words;
    size_t start = 0;
    for (size_t i = 0; i <= s.size(); ++i) {
        if (i == s.size() || s[i] == L' ') {
            if (i > start)  // only non-empty tokens
                words.push_back(s.substr(start, i - start));
            start = i + 1;
        }
    }
    return words;
}

// ---------------------------------------------------------------------------
// Word→int mapping (JiWER _word2int: auto-incrementing IDs, shared vocab)
// ---------------------------------------------------------------------------
struct WordInt {
    std::vector<int32_t> ref;
    std::vector<int32_t> hyp;
};

static WordInt word_to_int(const std::vector<std::wstring>& ref,
                           const std::vector<std::wstring>& hyp) {
    WordInt out;
    out.ref.reserve(ref.size());
    out.hyp.reserve(hyp.size());
    std::unordered_map<std::wstring, int32_t> map;
    int32_t next_id = 0;

    for (const auto& w : ref) {
        auto it = map.find(w);
        if (it == map.end())
            out.ref.push_back(map.emplace(w, next_id++).first->second);
        else
            out.ref.push_back(it->second);
    }
    for (const auto& w : hyp) {
        auto it = map.find(w);
        if (it == map.end())
            out.hyp.push_back(map.emplace(w, next_id++).first->second);
        else
            out.hyp.push_back(it->second);
    }
    return out;
}

// ---------------------------------------------------------------------------
// Public C ABI
// ---------------------------------------------------------------------------
extern "C" __declspec(dllexport) int __stdcall qriwer_compare(
    const wchar_t* reference,
    const wchar_t* hypothesis,
    QriWERResult* result)
{
    // Validate output pointer
    if (!result) return 1;
    result->error = 1;
    result->hits = result->substitutions = result->deletions = 0;
    result->insertions = result->ref_words = result->hyp_words = 0;
    result->wer = 0.0;

    try {
        // Tokenize (JiWER wer_default)
        auto ref_words = jiwer_tokenize(reference);
        auto hyp_words = jiwer_tokenize(hypothesis);

        int64_t ref_len = (int64_t)ref_words.size();
        int64_t hyp_len = (int64_t)hyp_words.size();

        result->ref_words = ref_len;
        result->hyp_words = hyp_len;

        // Edge case: both empty
        if (ref_len == 0 && hyp_len == 0) {
            result->error = 0;
            return 0;
        }

        // Map words to ints
        auto wi = word_to_int(ref_words, hyp_words);

        // Call RapidFuzz Levenshtein editops (uniform weights {1,1,1})
        auto ops = rapidfuzz::levenshtein_editops(wi.ref, wi.hyp);

        // Count operations
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

        result->hits = hits;
        result->substitutions = subs;
        result->deletions = dels;
        result->insertions = ins;

        // WER (JiWER 4.0.0 formula)
        if (ref_len == 0) {
            result->wer = (hyp_len == 0) ? 0.0 : (double)ins;
        } else {
            double denom = (double)(hits + subs + dels);  // == ref_len
            result->wer = (double)(subs + dels + ins) / denom;
        }

        result->error = 0;
        return 0;
    } catch (...) {
        result->error = 2;
        return 2;
    }
}

// Version string for sanity checks
extern "C" __declspec(dllexport) const char* __stdcall qriwer_version() {
    return "QriWER 1.0.0 / RapidFuzz-CPP header-only / JiWER 4.0.0 compatible";
}
