#!/usr/bin/env python3
"""Вызовы перевода (NxPort.NxLoc) в сгенерированном C++ TextMeshPro и UGUI.

Перевод подставляется там, где текст попадает в компонент:
  TMP_Text.text (сеттер), TMP_Text.SetText(шаблон, ...),
  TMP_Text.ParseInputText -- статичный текст сцены (m_inputSource == 0),
  UI.Text.text (сеттер), UI.Text.OnEnable -- статичный текст сцены.
Имена методов NxLoc.TrTmp/TrUI с хешем берутся из NxPort*.cpp той же выгрузки.
Повторный запуск ничего не меняет (метка // NX_LOC).

    python tools/patch_nx_localization.py <Il2CppOutputProject/Source/il2cppOutput>
"""
import argparse
import re
from pathlib import Path

MARK = "// NX_LOC"


def method_name(cppdir, name):
    for f in sorted(cppdir.glob("NxPort*.cpp")):
        m = re.search(r"\b(" + name + r"_m[0-9A-F]{40}) \(", f.read_text(encoding="utf-8", errors="ignore"))
        if m:
            return m.group(1)
    raise SystemExit(f"{name} не найден в NxPort*.cpp: NxLocalization.cs не вошёл в выгрузку")


def decl(fn):
    return (f"IL2CPP_EXTERN_C IL2CPP_METHOD_ATTR String_t* {fn} (RuntimeObject* ___0_text, "
            f"String_t* ___1_s, const RuntimeMethod* method); {MARK}\n")


def patch_function(text, name, first_param, body, fn):
    """Вставляет body в начало каждой функции name; возвращает (текст, сколько)."""
    pattern = re.compile(r"^IL2CPP_EXTERN_C IL2CPP_METHOD_ATTR \w+\*? " + name +
                         r"_m[0-9A-F]+ \(([^\n]*)\)\s*\n\{\n", re.M)
    count, out, pos = 0, [], 0
    for m in pattern.finditer(text):
        if first_param and not re.search(first_param, m.group(1)):
            continue
        if text[m.end():m.end() + 200].lstrip("\t").startswith(body.split("(")[0].strip()) or \
           MARK in text[m.end():m.end() + 300]:
            continue
        out.append(text[pos:m.start()])
        out.append(decl(fn))
        out.append(text[m.start():m.end()])
        out.append(body + "\n")
        pos = m.end()
        count += 1
    out.append(text[pos:])
    return "".join(out), count


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("cppdir", type=Path)
    a = ap.parse_args()
    tmp_fn = method_name(a.cppdir, "NxLoc_TrTmp")
    ui_fn = method_name(a.cppdir, "NxLoc_TrUI")
    # Older r83 exports skip UI font replacement when translation returns the
    # original object. Native language names are already translated literals.
    # Equivalent to removing !ReferenceEquals(r, s) in NxLocalization.cs;
    # no signatures or metadata change, so incremental C++ builds are safe.
    for f in sorted(a.cppdir.glob('NxPort*.cpp')):
        text = f.read_text(encoding='utf-8')
        start = text.find('String_t* ' + ui_fn + ' (')
        start = text.find('String_t* ' + ui_fn + ' (', start + 1)
        if start < 0:
            continue
        end = text.find('\n}\n', start)
        body = text[start:end]
        pattern = r'if \(\(\(\(RuntimeObject\*\)\(String_t\*\)L_\d+\) == \(\(RuntimeObject\*\)\(String_t\*\)L_\d+\)\)\)'
        fixed, count = re.subn(pattern, 'if (false) // NX_LOC_CJK_LITERAL', body)
        if count > 1:
            raise SystemExit('Ambiguous TrUI reference comparison')
        if count:
            f.write_text(text[:start] + fixed + text[end:], encoding='utf-8')
    targets = [
        ("Unity.TextMeshPro*.cpp", "TextMeshProUGUI_OnEnable", None,
         "\tif (__this->___m_text) { " + MARK + "\n"
         f"\t\tString_t* nx_text = {tmp_fn}((RuntimeObject*)__this, __this->___m_text, NULL);\n"
         "\t\tif (nx_text != __this->___m_text) { __this->___m_text = nx_text; "
         "Il2CppCodeGenWriteBarrier((void**)(&__this->___m_text), (void*)nx_text); }\n\t}", tmp_fn),
        ("Unity.TextMeshPro*.cpp", "TextMeshPro_OnEnable", None,
         "\tif (__this->___m_text) { " + MARK + "\n"
         f"\t\tString_t* nx_text = {tmp_fn}((RuntimeObject*)__this, __this->___m_text, NULL);\n"
         "\t\tif (nx_text != __this->___m_text) { __this->___m_text = nx_text; "
         "Il2CppCodeGenWriteBarrier((void**)(&__this->___m_text), (void*)nx_text); }\n\t}", tmp_fn),
        ("Unity.TextMeshPro*.cpp", "TMP_Text_set_text", r"String_t\* ___0_value",
         f"\t___0_value = {tmp_fn}((RuntimeObject*)__this, ___0_value, NULL); {MARK}", tmp_fn),
        ("Unity.TextMeshPro*.cpp", "TMP_Text_SetText", r"^TMP_Text_\w+\* __this, String_t\* ___0_sourceText",
         f"\t___0_sourceText = {tmp_fn}((RuntimeObject*)__this, ___0_sourceText, NULL); {MARK}", tmp_fn),
        ("Unity.TextMeshPro*.cpp", "TMP_Text_ParseInputText", None,
         "\tif (__this->___m_inputSource == 0 && __this->___m_text) { " + MARK + "\n"
         f"\t\tString_t* nx_text = {tmp_fn}((RuntimeObject*)__this, __this->___m_text, NULL);\n"
         "\t\tif (nx_text != __this->___m_text) { __this->___m_text = nx_text; "
         "Il2CppCodeGenWriteBarrier((void**)(&__this->___m_text), (void*)nx_text); }\n\t}", tmp_fn),
        ("UnityEngine.UI*.cpp", "Text_set_text", r"String_t\* ___0_value",
         f"\t___0_value = {ui_fn}((RuntimeObject*)__this, ___0_value, NULL); {MARK}", ui_fn),
        ("UnityEngine.UI*.cpp", "Text_OnEnable", None,
         "\tif (__this->___m_Text) { " + MARK + "\n"
         f"\t\tString_t* nx_text = {ui_fn}((RuntimeObject*)__this, __this->___m_Text, NULL);\n"
         "\t\tif (nx_text != __this->___m_Text) { __this->___m_Text = nx_text; "
         "Il2CppCodeGenWriteBarrier((void**)(&__this->___m_Text), (void*)nx_text); }\n\t}", ui_fn),
    ]
    done = {}
    for glob, name, param, body, fn in targets:
        for f in sorted(a.cppdir.glob(glob)):
            text = f.read_text(encoding="utf-8")
            new, n = patch_function(text, name, param, body, fn)
            if n:
                f.write_text(new, encoding="utf-8")
                done[name] = done.get(name, 0) + n
    already = sum(f.read_text(encoding="utf-8", errors="ignore").count(MARK) > 0
                  for g in ("Unity.TextMeshPro*.cpp", "UnityEngine.UI*.cpp") for f in a.cppdir.glob(g))
    print("NX_LOC:", ", ".join(f"{k} x{v}" for k, v in done.items()) or "уже стоит", f"(файлов с меткой: {already})")
    for need in ("TMP_Text_set_text", "TMP_Text_ParseInputText", "Text_set_text", "Text_OnEnable"):
        if need not in done and not already:
            raise SystemExit(f"{need}: функция не найдена -- другая версия TMP/UGUI?")


if __name__ == "__main__":
    main()
