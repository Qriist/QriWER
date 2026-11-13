#Requires AutoHotkey v2.1-
/**
 * QriWER — AHKv2 JiWER-compatible word error rate.
 * 
 * Wraps qriwer_shim.dll (C ABI, UTF-16 in) which implements JiWER 4.0.0
 * wer_default tokenization + rapidfuzz-cpp Levenshtein alignment.
 * 
 * Usage:
 *   #Include <QriWER>
 *   QriWER.Init()
 *   r := QriWER.Compare("The quick brown fox", "The quick brown fox")
 *   msgbox r.Get("hits") " " r.Get("wer")
 * 
 * QriWER.Compare(ref, hyp)  [word-level, wer_default] returns a Map with:
 *   hits, substitutions, deletions, insertions,
 *   ref_words, hyp_words, wer, mer, wip, wil, error
 * 
 * QriWER.CER(ref, hyp)      [char-level, cer_default] returns a Map with:
 *   hits, substitutions, deletions, insertions,
 *   ref_chars, hyp_chars, cer, error
 * 
 * QriWER.Version() -> string
 * 
 * Access via Map methods (required — dot-notation is NOT supported on
 * plain Maps in this build): r.Get("wer")  or  r["wer"]  or  r.Has("wer").
 * 
 * Throws on native error.
 */
class QriWER {
    static hmodule := 0
    static dllPath := ""
    static funcCompare := 0
    static funcCER := 0
    static funcVersion := 0
    static structSize := 88

    static Init(dllPathOverride := "") {
        if(QriWER.hmodule)
            return QriWER.dllPath
        if(dllPathOverride == "")
            dllPathOverride := QriWER.__FindDefaultDll()
        if(dllPathOverride == "")
            throw "QriWER: DLL not found. Searched: " A_ScriptDir
        if(!FileExist(dllPathOverride))
            throw "QriWER: DLL does not exist: " dllPathOverride

        QriWER.hmodule := DllCall("LoadLibrary", "Str", dllPathOverride, "Ptr")
        if(!QriWER.hmodule)
            throw "QriWER: LoadLibrary failed (err=" A_LastError ")"

        QriWER.dllPath := dllPathOverride
        QriWER.funcCompare := DllCall("GetProcAddress", "Ptr", QriWER.hmodule, "AStr", "qriwer_compare", "Ptr")
        if(!QriWER.funcCompare)
            throw "QriWER: GetProcAddress qriwer_compare failed (err=" A_LastError ")"
        QriWER.funcCER := DllCall("GetProcAddress", "Ptr", QriWER.hmodule, "AStr", "qriwer_cer", "Ptr")
        if(!QriWER.funcCER)
            throw "QriWER: GetProcAddress qriwer_cer failed (err=" A_LastError ")"
        QriWER.funcVersion := DllCall("GetProcAddress", "Ptr", QriWER.hmodule, "AStr", "qriwer_version", "Ptr")

        return dllPathOverride
    }

    static Release() {
        if(!QriWER.hmodule)
            return
        DllCall("FreeLibrary", "Ptr", QriWER.hmodule)
        QriWER.hmodule := 0
        QriWER.dllPath := ""
        QriWER.funcCompare := 0
        QriWER.funcCER := 0
        QriWER.funcVersion := 0
    }

    static Version() {
        if(!QriWER.hmodule)
            QriWER.Init()
        return DllCall(QriWER.funcVersion, "AStr")
    }

    static Compare(ref, hyp) {
        if(!QriWER.hmodule)
            QriWER.Init()
        buf := Buffer(QriWER.structSize, 0)
        ret := DllCall(QriWER.funcCompare
            , "Str", ref
            , "Str", hyp
            , "Ptr", buf
            , "Cdecl Int")
        if(ret != 0) {
            errCode := NumGet(buf, 80, "Int")
            throw "QriWER: qriwer_compare failed (rc=" ret ", error=" errCode ")"
        }
        return Map(
            "hits",          NumGet(buf, 0, "Int64"),
            "substitutions", NumGet(buf, 8, "Int64"),
            "deletions",     NumGet(buf, 16, "Int64"),
            "insertions",    NumGet(buf, 24, "Int64"),
            "ref_words",     NumGet(buf, 32, "Int64"),
            "hyp_words",     NumGet(buf, 40, "Int64"),
            "wer",           NumGet(buf, 48, "Double"),
            "mer",           NumGet(buf, 56, "Double"),
            "wip",           NumGet(buf, 64, "Double"),
            "wil",           NumGet(buf, 72, "Double"),
            "error",         0
        )
    }

    static CER(ref, hyp) {
        if(!QriWER.hmodule)
            QriWER.Init()
        buf := Buffer(QriWER.structSize, 0)
        ret := DllCall(QriWER.funcCER
            , "Str", ref
            , "Str", hyp
            , "Ptr", buf
            , "Cdecl Int")
        if(ret != 0) {
            errCode := NumGet(buf, 80, "Int")
            throw "QriWER: qriwer_cer failed (rc=" ret ", error=" errCode ")"
        }
        return Map(
            "hits",          NumGet(buf, 0, "Int64"),
            "substitutions", NumGet(buf, 8, "Int64"),
            "deletions",     NumGet(buf, 16, "Int64"),
            "insertions",    NumGet(buf, 24, "Int64"),
            "ref_chars",     NumGet(buf, 32, "Int64"),
            "hyp_chars",     NumGet(buf, 40, "Int64"),
            "cer",           NumGet(buf, 48, "Double"),
            "error",         0
        )
    }

    static __FindDefaultDll() {
        candidates := [
            A_ScriptDir "\qriwer_shim.dll",
            A_ScriptDir "\..\qriwer_shim.dll",
            A_ScriptDir "\..\..\qriwer_shim.dll",
            A_ScriptDir "\..\bin\qriwer_shim.dll",
            A_ScriptDir "\bin\qriwer_shim.dll"
        ]
        i := 1
        while(i <= candidates.length) {
            if(FileExist(candidates[i]))
                return candidates[i]
            i++
        }
        return ""
    }
}
