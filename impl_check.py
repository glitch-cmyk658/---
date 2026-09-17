#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
impl_check.py —— 扫描 C++ 头文件 + 源文件，判断每个成员函数"实现到什么程度了"。

解决的问题：
    写完一半就想跑测试的时候，不用一个个函数去翻源码，一条命令看清楚
    哪些是空壳（调用会 SIGILL / 毫无效果）、哪些连定义都没有（链接必挂）。

用法：
    python impl_check.py                    # 自动找当前目录的 *.h / *.hpp / *.cpp
    python impl_check.py BST.hpp BST.cpp    # 显式指定
    python impl_check.py -q                 # 只列有问题的
    python impl_check.py --no-compile       # 跳过 g++ 语法检查
    python impl_check.py --debug            # 打印每个函数的解析细节

注意：类外定义在 .cpp 和头文件里都会找 —— 模板的实现必须写在头文件中，
所以定义不一定在 .cpp 里。

状态含义：
    MISSING  只有声明、没有定义         → 链接期 undefined reference
    EMPTY    函数体是空的               → 非 void 函数会插 ud2，调用即 SIGILL(132)
    BROKEN   非 void 函数里有裸 return; → 编译直接报错
    STUB     函数体只有一句 return 常量 → 返回值固定，功能缺失
    OK       函数体里有实际语句
    SPECIAL  = default / = delete / 构造函数靠初始化列表
"""

import os
import re
import sys
import glob
import shutil
import subprocess

if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")


# ==============================================================
#  0. 基础工具
# ==============================================================

def read_text(path):
    with open(path, "r", encoding="utf-8", errors="replace") as f:
        return f.read()


def strip_comments(src):
    """把注释和字符串字面量的内容替换成空格（保持长度不变，行号列号不漂）。"""
    out = list(src)
    i, n = 0, len(src)

    def blank(j):
        while j < n and src[j] != "\n":
            out[j] = " "
            j += 1
        return j

    while i < n:
        c = src[i]

        if c == "/" and i + 1 < n and src[i + 1] == "/":
            i = blank(i)

        elif c == "/" and i + 1 < n and src[i + 1] == "*":
            out[i] = out[i + 1] = " "
            i += 2
            while i < n and not (src[i] == "*" and i + 1 < n and src[i + 1] == "/"):
                if src[i] != "\n":
                    out[i] = " "
                i += 1
            if i < n:
                out[i] = " "
                out[i + 1] = " "
                i += 2

        elif c == '"' or c == "'":
            quote = c
            out[i] = " "
            i += 1
            while i < n and src[i] != quote:
                if src[i] == "\\":
                    out[i] = " "
                    i += 1
                    if i < n:
                        out[i] = " "
                        i += 1
                    continue
                if src[i] != "\n":
                    out[i] = " "
                i += 1
            if i < n:
                out[i] = " "
                i += 1

        else:
            i += 1

    return "".join(out)


def match_brace(src, i):
    """src[i] 是 '{'，返回配对 '}' 的下标；不配对返回 len(src)-1。"""
    depth = 0
    n = len(src)
    while i < n:
        if src[i] == "{":
            depth += 1
        elif src[i] == "}":
            depth -= 1
            if depth == 0:
                return i
        i += 1
    return n - 1


def line_of(src, off):
    return src.count("\n", 0, off) + 1


def split_top_commas(s):
    """按顶层逗号切分参数列表。"""
    parts, depth, cur = [], 0, ""
    for ch in s:
        if ch in "(<[":
            depth += 1
        elif ch in ")>]":
            depth -= 1
        if ch == "," and depth == 0:
            parts.append(cur)
            cur = ""
        else:
            cur += ch
    if cur.strip():
        parts.append(cur)
    return [p for p in parts if p.strip()]


def split_members(body):
    """把类体切成一段段成员声明：(起, 止)。内联定义（含 {}）作为一整段。"""
    segs, i, start, n = [], 0, 0, len(body)
    while i < n:
        if body[i] == "{":
            j = match_brace(body, i)
            segs.append((start, j + 1))
            i = j + 1
            start = i
        elif body[i] == ";":
            segs.append((start, i + 1))
            i += 1
            start = i
        else:
            i += 1
    if body[start:].strip():
        segs.append((start, n))
    return segs


# ==============================================================
#  1. 解析头文件：找出 class/struct 及其成员函数声明
# ==============================================================

SCOPE_RE = re.compile(r"\b(namespace|class|struct)\s+(\w+)?\s*(?::[^{;]*)?\{")
NAME_PARAMS_RE = re.compile(r"(~?\w+|operator\s*=\s*)\s*\(([^()]*(?:\([^()]*\)[^()]*)*)\)")
QUALIFIERS = {"explicit", "virtual", "inline", "static", "constexpr",
              "friend", "mutable", "noexcept", "override", "final"}
KEYWORDS = {"if", "while", "for", "switch", "return", "catch", "do", "else"}


class FuncInfo(object):
    def __init__(self, cls, name, params, ret, kind, line):
        self.cls = cls                  # 全限定类名，如 BSTRE::BinarySearch
        self.name = name
        self.params = params
        self.nparams = len(split_top_commas(params))
        self.ret = ret
        self.kind = kind                # 'decl' | 'inline' | 'special' | 'def'
        self.line = line
        self.body = None
        self.file = ""
        self.status = None
        self.note = ""
        self.diag = []

    @property
    def key(self):
        # 用全限定类名做 key，否则 BSTRE::BinarySearch 和 BST::BinarySearch 会撞车
        return (self.cls, self.name, self.nparams)

    @property
    def short_cls(self):
        return self.cls.split("::")[-1]

    @property
    def is_ctor(self):
        return self.name == self.short_cls

    @property
    def is_dtor(self):
        return self.name.startswith("~")

    @property
    def returns_void(self):
        return self.ret.strip().replace(" ", "") in ("void", "")

    @property
    def body_lines(self):
        if self.body is None or not self.body.strip():
            return 0
        return self.body.count("\n") + 1


def parse_scopes(src):
    out = []
    for m in SCOPE_RE.finditer(src):
        kind, name = m.group(1), m.group(2)
        if kind != "namespace" and not name:
            continue
        open_i = m.end() - 1
        out.append([kind, name or "", open_i, match_brace(src, open_i)])
    return out


def scope_prefix(scopes, off):
    best = None
    for kind, name, a, b in scopes:
        if kind == "namespace" and a < off < b:
            if best is None or a > best[0]:
                best = (a, name)
    return (best[1] + "::") if best else ""


def parse_header(header_src):
    stripped = strip_comments(header_src)
    scopes = parse_scopes(stripped)
    funcs = []

    for kind, cname, body_a, body_b in scopes:
        if kind == "namespace":
            continue
        cls = scope_prefix(scopes, body_a) + cname
        body = stripped[body_a + 1:body_b]

        for a, b in split_members(body):
            seg = body[a:b]
            if "(" not in seg:
                continue
            ms = list(NAME_PARAMS_RE.finditer(seg))
            if not ms:
                continue
            # 取第一个匹配：函数签名的名字/参数在最前面，函数体里的调用在后面
            m = ms[0]
            fname = m.group(1).strip()
            params = m.group(2)
            if fname in KEYWORDS:
                continue

            head = seg[:m.start()].strip()
            tail = seg[m.end():]
            # 初始化列表只对构造函数有意义：剥掉 ": a(b), c(d)" 再判断
            init_list = ""
            if ":" in tail.split("{")[0]:
                init_list = tail[:tail.index("{")] if "{" in tail else tail

            ret_tokens = [t for t in head.split() if t not in QUALIFIERS]
            ret = " ".join(ret_tokens)

            abs_line = line_of(header_src, body_a + 1 + a + m.start())
            open_brace = seg.find("{", m.end())

            if "=" in tail and ("default" in tail or "delete" in tail) and open_brace < 0:
                f = FuncInfo(cls, fname, params, ret, "special", abs_line)
            elif open_brace >= 0:
                inner = seg[open_brace:]
                f = FuncInfo(cls, fname, params, ret, "inline", abs_line)
                f.body = inner[1:match_brace(inner, 0)]
                f.has_init_list = bool(init_list.strip())
                funcs.append(f)
                continue
            else:
                f = FuncInfo(cls, fname, params, ret, "decl", abs_line)
            f.has_init_list = bool(init_list.strip())
            funcs.append(f)

    return funcs


# ==============================================================
#  2. 解析源文件：找类外定义
# ==============================================================

DEF_RE = re.compile(
    r"(?:(?P<ret>[^;{}()\n]+?)\s+)?"
    # 限定名要能吃下模板参数列表：BST::BinarySearch<T, Compare>::insert
    # 少了 (?:\s*<...>)? 这一段，qual 会被截成最后的 "Compare"，key 就全错了。
    #   · [^;{}()\n] 排除换行 —— 否则上一行的 template<...> 会被吞进来
    #   · 排除 * —— 否则返回类型 BSTRE::TreeNode<T>* 会被整个当成"模板参数"
    r"(?P<qual>[\w:]+(?:\s*<[^;{}()\n*]*?>)?)::(?P<name>~?\w+)\s*"
    r"\((?P<params>[^()]*(?:\([^()]*\)[^()]*)*)\)\s*"
    r"(?:(?:const|noexcept|override|final)\s*)*"
    r"(?::\s*[^{;]*)?\{"
)


def strip_template_head(s):
    """去掉开头的 template<...>：它常被当成返回类型，让 void 函数误判成非 void。
    按尖括号配对剥离，因为参数里可能还有 std::less<T>。"""
    s = s.strip()
    if not s.startswith("template"):
        return s
    i = s.find("<")
    if i < 0:
        return s
    depth = 0
    for j in range(i, len(s)):
        if s[j] == "<":
            depth += 1
        elif s[j] == ">":
            depth -= 1
            if depth == 0:
                return s[j + 1:].strip()
    return s


def strip_trailing_template_args(qual):
    """去掉限定名末尾的模板实参：BST::BinarySearch<T, Compare> -> BST::BinarySearch
    只从尾部剥，避免把返回类型里的类名一起切掉。"""
    if not qual.endswith(">"):
        return qual
    depth = 0
    for i in range(len(qual) - 1, -1, -1):
        if qual[i] == ">":
            depth += 1
        elif qual[i] == "<":
            depth -= 1
            if depth == 0:
                return qual[:i].strip()
    return qual


def parse_cpp(cpp_src):
    stripped = strip_comments(cpp_src)
    defs = {}
    for m in DEF_RE.finditer(stripped):
        # 类名后面常挂着模板实参（BinarySearch<T, Compare>）；key 要用不带参数
        # 的类名，才能和头文件里 class X 的声明对上。
        qual = strip_trailing_template_args(m.group("qual").strip())
        name = m.group("name").strip()
        ret = strip_template_head((m.group("ret") or "").strip())
        if ret.split() and ret.split()[0] in KEYWORDS:
            continue
        open_i = m.end() - 1
        close_i = match_brace(stripped, open_i)
        f = FuncInfo(qual, name, m.group("params"), ret, "def",
                     line_of(cpp_src, m.start()))
        f.body = stripped[open_i + 1:close_i]
        defs[f.key] = f
    return defs


# ==============================================================
#  3. 判定实现状态
# ==============================================================

BARE_RETURN_RE = re.compile(r"\breturn\s*;")
STUB_ONLY_RE = re.compile(
    r"^return\s*(?:nullptr|NULL|0|false|true|-?\d+|-?\d+\.\d+)?\s*;$"
)


def classify(f):
    if f.kind == "special":
        f.status = "SPECIAL"
        f.note = "编译器生成 = default / 已禁用 = delete"
        return

    if f.body is None:
        f.status = "MISSING"
        f.note = "只有声明，没有定义"
        return

    flat = re.sub(r"\s+", "", f.body)

    if flat == "":
        if f.is_dtor:
            f.status = "EMPTY"
            f.note = "空析构：什么都没释放，注意整棵树泄漏"
        elif f.is_ctor and getattr(f, "has_init_list", False):
            f.status = "SPECIAL"
            f.note = "构造函数，靠初始化列表"
        elif f.is_ctor:
            f.status = "SPECIAL"
            f.note = "空构造函数（成员用默认初始化器）"
        elif f.returns_void:
            f.status = "EMPTY"
            f.note = "空体：调用它没有任何效果"
        else:
            f.status = "EMPTY"
            f.note = "空体：GCC 会在末尾插 ud2，调用即 SIGILL(132)"
        return

    if not f.returns_void and BARE_RETURN_RE.search(f.body):
        f.status = "BROKEN"
        f.note = "非 void 函数里有裸 return; —— 编译不过"
        return

    if STUB_ONLY_RE.match(flat):
        f.status = "STUB"
        f.note = "只有一句 return 常量，功能缺失"
        return

    f.status = "OK"
    f.note = ""


# ==============================================================
#  4. 编译验证
# ==============================================================

def find_gxx():
    for cand in ("g++", r"C:\mingw64\bin\g++.exe", r"C:\msys64\mingw64\bin\g++.exe"):
        p = shutil.which(cand) if os.sep not in cand else (
            cand if os.path.exists(cand) else None)
        if p:
            return p
    return None


DIAG_RE = re.compile(r"^(.+?):(\d+):(\d+):\s*(error|warning):\s*(.*)$")


def run_compiler(gxx, cpp_path, incdir):
    try:
        r = subprocess.run(
            [gxx, "-std=c++17", "-fsyntax-only", "-Wall", "-Wextra", "-I" + incdir, cpp_path],
            capture_output=True, text=True, errors="replace", timeout=90,
        )
    except Exception:
        return []
    diags = []
    for ln in ((r.stdout or "") + (r.stderr or "")).splitlines():
        m = DIAG_RE.match(ln)
        if m:
            diags.append((m.group(1), int(m.group(2)), m.group(4), m.group(5)))
    return diags


# ==============================================================
#  5. 主流程
# ==============================================================

ORDER = {"MISSING": 0, "BROKEN": 1, "EMPTY": 2, "STUB": 3, "OK": 4, "SPECIAL": 5}
TAG = {
    "MISSING": "MISSING",
    "BROKEN":  "BROKEN",
    "EMPTY":   "EMPTY",
    "STUB":    "STUB",
    "OK":      "OK",
    "SPECIAL": "SPECIAL",
}
MEAN = {
    "MISSING": "只有声明，没有定义",
    "BROKEN":  "编译不过",
    "EMPTY":   "空函数体",
    "STUB":    "桩返回",
    "OK":      "已实现",
    "SPECIAL": "特殊成员",
}


def main():
    raw = sys.argv[1:]
    quiet = "-q" in raw or "--quiet" in raw
    debug = "--debug" in raw
    use_compile = "--no-compile" not in raw
    args = [a for a in raw if not a.startswith("-")]

    if args:
        headers = [a for a in args if a.endswith((".h", ".hpp", ".hh"))]
        cpps = [a for a in args if a.endswith((".cpp", ".cc", ".cxx"))]
    else:
        headers = sorted(glob.glob("*.h") + glob.glob("*.hpp"))
        cpps = [c for c in sorted(glob.glob("*.cpp") + glob.glob("*.cc"))
                if not re.search(r"_?test\.cpp$", c)]

    if not headers and not cpps:
        print("没找到 .h / .hpp / .cpp。用：python impl_check.py BST.hpp BST.cpp")
        return 1

    print("=" * 74)
    print("  实现状态扫描")
    print("  头文件: %s" % (", ".join(headers) or "-"))
    print("  源文件: %s" % (", ".join(cpps) or "-"))
    print("=" * 74)

    gxx = find_gxx() if use_compile else None
    diags = []
    if gxx:
        # 非 test 的 .cpp 要编；头文件也单独过一遍语法检查 ——
        # 当实现（尤其是模板）写在头文件里时，只编空的 .cpp 是查不出任何东西的。
        targets = list(cpps) + [h for h in headers if h not in cpps]
        incdir = os.path.dirname(os.path.abspath(headers[0])) if headers else "."
        for t in targets:
            diags += run_compiler(gxx, t, incdir)

    grand = {k: 0 for k in ORDER}

    # 类外定义可能出现在 .cpp，也可能出现在头文件里 —— 模板的实现必须写在头
    # 文件中，所以两边都要扫。每个定义记住自己来自哪个文件，用于报行号。
    defs = {}
    for src in list(cpps) + list(headers):
        for k, v in parse_cpp(read_text(src)).items():
            v.file = src
            defs[k] = v

    for h in headers:
        hsrc = read_text(h)
        decls = parse_header(hsrc)

        for f in decls:
            if f.kind == "decl":
                d = defs.get(f.key)
                if d is not None:
                    f.body = d.body
                    f.ret = f.ret or d.ret
                    f.line = d.line
                    f.file = d.file
                    f.kind = "def"
                else:
                    f.file = h
            elif f.kind == "inline":
                f.file = h
            else:
                f.file = h
            classify(f)

        # 编译诊断：按行号就近挂到函数上（只在同文件、行号靠后 40 行内）
        for f in decls:
            for path, dline, sev, msg in diags:
                if os.path.basename(path) != os.path.basename(f.file):
                    continue
                if f.line <= dline <= f.line + 40:
                    f.diag.append("%s: %s" % (sev, msg))
                    if sev == "error" and f.status == "OK":
                        f.status = "BROKEN"
                        f.note = "GCC error: " + msg

        groups = {}
        for f in decls:
            groups.setdefault(f.cls, []).append(f)

        for cls in groups:
            items = sorted(groups[cls], key=lambda x: (ORDER.get(x.status, 9), x.line))
            for f in items:
                grand[f.status] = grand.get(f.status, 0) + 1
            shown = [x for x in items if not (quiet and x.status in ("OK", "SPECIAL"))]
            if not shown:
                continue
            print()
            print("  [%s]" % cls)
            for f in shown:
                sig = "%s(%s)" % (f.name, "..." if f.nparams else "")
                loc = "%s:%d" % (os.path.basename(f.file), f.line) if f.file else "-"
                extra = "%d 行体" % f.body_lines if f.body_lines else ""
                print("    %-9s %-22s %-18s %-12s %s"
                      % (TAG[f.status], sig, loc, extra, MEAN[f.status]))
                if f.note:
                    print("              └─ %s" % f.note)
                for d in f.diag[:2]:
                    print("              └─ %s" % d)
            if debug:
                for f in shown:
                    print("       · debug: key=%s ret=%r kind=%s" % (f.key, f.ret, f.kind))

    print()
    print("-" * 74)
    print("  共 %d 个成员函数：已实现 %d / 空壳 %d / 编译不过 %d / 无定义 %d / 桩 %d / 特殊 %d"
          % (sum(grand.values()), grand["OK"], grand["EMPTY"],
             grand["BROKEN"], grand["MISSING"], grand["STUB"], grand["SPECIAL"]))
    problems = grand["EMPTY"] + grand["BROKEN"] + grand["MISSING"] + grand["STUB"]
    if problems:
        print("  还有 %d 处没写完：" % problems)
        if grand["MISSING"]:
            print("     MISSING → 链接期 undefined reference，编译器不会提前告警")
        if grand["BROKEN"]:
            print("     BROKEN  → g++ 直接报错，程序根本编不出来")
        if grand["EMPTY"]:
            print("     EMPTY   → 非 void 的会 SIGILL(132)；void 的静默无效果（遍历全空等）")
        if grand["STUB"]:
            print("     STUB    → 能跑，但永远返回同一个值")
    else:
        print("  ✓ 所有函数体都有实际逻辑，可以进入测试阶段了")
    print("-" * 74)
    return 0


if __name__ == "__main__":
    sys.exit(main())
