#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Pablo Torres, EA5JLJ. See LICENSE.
"""mkdict — builds the word list used by the word-spacing fix (src/words.c)
from the common English and Spanish words written for this project in
tools/words_en.txt and tools/words_es.txt.

usage: python3 tools/mkdict.py tools/words_en.txt tools/words_es.txt > src/dict.c
       python3 tools/mkdict.py tools/words_en.txt tools/words_es.txt --bin radio/SKIMDICT.BIN
       python3 tools/mkdict.py tools/words_en.txt tools/words_es.txt --list   (one word per line)

Both files list words with the forms they ask for (plurals, verb forms,
comparatives; in Spanish also feminines). All in capitals, A-Z only: accents
removed and Ñ as N, as in CW. Two letters or more.

Format (compact, no pointers, freestanding C):
  - words are grouped by their first two letters (26 x 26 buckets);
  - dict_idx[b] is the byte offset of bucket b in dict_data, dict_idx[676]
    is the end;
  - inside a bucket each word is stored without its first two letters, front
    coded against the previous one: a header byte (high nibble = letters kept
    from the previous word, low nibble = new letters) and the new letters.

SKIMDICT.BIN (--bin), read by the radio app: "SKD1", the 677 offsets as
little-endian 16-bit numbers, then the data.
"""
import re
import sys
import unicodedata

def plain(w):
    """capitals A-Z: accents removed, Ñ → N"""
    w = unicodedata.normalize("NFD", w)
    w = "".join(c for c in w if not unicodedata.combining(c))
    return w.upper()


# ---- English forms

def en_s(w):
    """plural / third person: boxes, cities, rigs"""
    if re.search(r"(s|x|z|ch|sh)$", w):
        return w + "es"
    if re.search(r"[^aeiou]y$", w):
        return w[:-1] + "ies"
    return w + "s"


def en_verb(w, mark):
    dbl = w + w[-1] if mark == "2" else w
    if w.endswith("e") and not w.endswith("ee"):
        ed, ing = w + "d", w[:-1] + "ing"
    elif re.search(r"[^aeiou]y$", w):
        ed, ing = w[:-1] + "ied", w + "ing"
    else:
        ed, ing = dbl + "ed", dbl + "ing"
    return [w, en_s(w), ed, ing]


def en_adj(w, mark):
    if not mark.endswith("er"):
        return [w]
    if mark == "2er":
        b = w + w[-1]
    elif w.endswith("e"):
        b = w[:-1]
    elif re.search(r"[^aeiou]y$", w):
        b = w[:-1] + "i"
    else:
        b = w
    return [w, b + "er", b + "est"]


def load_en(path):
    words = set()
    for kind, w, mark in entries(path):
        if kind == "=":
            forms = [w]
        elif kind == "n":
            forms = [w, en_s(w)]
        elif kind == "v":
            forms = en_verb(w, mark)
        else:
            forms = en_adj(w, mark)
        for x in forms:
            x = plain(x)
            if re.fullmatch(r"[A-Z]{2,17}", x):
                words.add(x)
    return words


def entries(path):
    """(kind, word, mark) for every word of a words_xx.txt file"""
    with open(path, encoding="utf-8") as f:
        for line in f:
            line = line.split("#", 1)[0].strip()
            if not line:
                continue
            if line.startswith("="):
                kind, items = "=", line[1:].split()
            elif re.match(r"^[nav]:", line):
                kind, items = line[0], line[2:].split()
            else:
                sys.exit(f"{path}: bad line: {line}")
            for it in items:
                w, _, mark = it.partition("/")
                yield kind, plain(w).lower(), mark


# ---- Spanish forms

def plural(w):
    if w[-1] in "aeiou":
        return [w + "s"]
    if w[-1] == "z":
        return [w[:-1] + "ces"]
    if w[-1] == "s":
        return []                                     # lunes, crisis
    return [w + "es"]


def adjective(w):
    if w.endswith("o"):
        return [w, w[:-1] + "a", w + "s", w[:-1] + "as"]
    return [w] + plural(w)


def soften(stem, ending):
    """spelling before e: busc+e → busque, lleg+e → llegue, empez+e → empiece"""
    if ending.startswith("e"):
        if stem.endswith("c"):
            return stem[:-1] + "qu" + ending
        if stem.endswith("g"):
            return stem[:-1] + "gu" + ending
        if stem.endswith("z"):
            return stem[:-1] + "c" + ending
    return stem + ending


def last_swap(stem, vowel, new):
    k = stem.rfind(vowel)
    return stem[:k] + new + stem[k + 1:] if k >= 0 else stem


def verb(inf, mark):
    """the usual forms of a regular verb; mark: stem change ie / ue / i, or ui
    (construir → construyo)"""
    stem, kind = inf[:-2], inf[-2:]
    strong = weak = stem                       # stressed stem; -ir weak stem (sintio, durmio)
    if mark == "ie":
        strong, weak = last_swap(stem, "e", "ie"), last_swap(stem, "e", "i")
    elif mark == "ue":
        strong, weak = last_swap(stem, "o", "ue"), last_swap(stem, "o", "u")
    elif mark == "i":
        strong = weak = last_swap(stem, "e", "i")
    elif mark == "ui":
        strong = stem + "y"
    if kind != "ir":
        weak = stem
    forms = [inf]
    if kind == "ar":
        forms += [strong + "o", strong + "as", strong + "a", stem + "amos", stem + "ais", strong + "an"]
        forms += [soften(stem, "e"), stem + "aste", stem + "o", stem + "asteis", stem + "aron"]
        forms += [stem + e for e in ("aba", "abas", "abamos", "abais", "aban")]
        forms += [soften(strong, e) for e in ("e", "es", "en")] + [soften(stem, e) for e in ("emos", "eis")]
        forms += [stem + e for e in ("ando", "ado", "ada", "ados", "adas", "ad")]
    else:
        th = "e" if kind == "er" else "i"
        forms += [strong + "o", strong + "es", strong + "e", stem + th + "mos",
                  stem + ("eis" if kind == "er" else "is"), strong + "en"]
        if mark == "ui":
            forms += [stem + "i", stem + "iste", stem + "yo", stem + "imos", stem + "isteis", stem + "yeron", stem + "yendo"]
        else:
            forms += [stem + "i", stem + "iste", weak + "io", stem + "imos", stem + "isteis", weak + "ieron", weak + "iendo"]
        forms += [stem + e for e in ("ia", "ias", "iamos", "iais", "ian")]
        forms += [strong + "a", strong + "as", strong + "an", weak + "amos", weak + "ais"]
        forms += [stem + e for e in ("ido", "ida", "idos", "idas")] + [stem + th + "d"]
    forms += [inf + e for e in ("e", "as", "a", "emos", "eis", "an", "ia", "ias", "iamos", "iais", "ian")]
    return forms


def load_es(path):
    words = set()
    for kind, w, mark in entries(path):
        if kind == "=":
            forms = [w]
        elif kind == "n":
            forms = [w] + plural(w)
        elif kind == "a":
            forms = adjective(w)
        else:
            forms = verb(w, mark)
        for x in forms:
            x = plain(x)
            if re.fullmatch(r"[A-Z]{2,17}", x):
                words.add(x)
    return words


def main():
    args = sys.argv[1:]
    mode = "c"
    if len(args) == 4 and args[2] == "--bin":
        mode = "bin"
    elif len(args) == 3 and args[2] == "--list":
        mode = "list"
    elif len(args) != 2:
        sys.exit(__doc__)
    en, es = load_en(args[0]), load_es(args[1])
    words = sorted(en | es)
    if mode == "list":
        print("\n".join(words))
        return
    buckets = [[] for _ in range(26 * 26)]
    for w in words:
        buckets[(ord(w[0]) - 65) * 26 + ord(w[1]) - 65].append(w[2:])
    data = bytearray()
    idx = []
    for b in buckets:
        idx.append(len(data))
        prev = ""
        for rest in b:
            p = 0
            while p < len(prev) and p < len(rest) and prev[p] == rest[p] and p < 15:
                p += 1
            new = rest[p:]
            if len(new) > 15:
                sys.exit(f"word too long: {rest}")
            data.append((p << 4) | len(new))
            data += new.encode("ascii")
            prev = rest
    idx.append(len(data))
    if idx[-1] > 65535:
        sys.exit("word list too big for 16-bit offsets")

    if mode == "bin":
        with open(args[3], "wb") as f:
            f.write(b"SKD1")
            for v in idx:
                f.write(v.to_bytes(2, "little"))
            f.write(data)
        return

    out = sys.stdout
    out.write("/* Generated by tools/mkdict.py - do not edit.\n")
    out.write(" * %d words (%d English from tools/words_en.txt, %d Spanish from\n"
              " * tools/words_es.txt, some in both), %d bytes. */\n" % (len(words), len(en), len(es), len(data) + 2 * len(idx)))
    out.write('#include "dict.h"\n\n')
    out.write("const unsigned short dict_idx[677] = {\n")
    for i in range(0, len(idx), 12):
        out.write("    " + ", ".join(str(v) for v in idx[i:i + 12]) + ",\n")
    out.write("};\n\n")
    out.write("const unsigned char dict_data[%d] = {\n" % len(data))
    for i in range(0, len(data), 16):
        out.write("    " + ", ".join(str(v) for v in data[i:i + 16]) + ",\n")
    out.write("};\n")


if __name__ == "__main__":
    main()
