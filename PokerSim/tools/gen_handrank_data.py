#!/usr/bin/env python3
"""Génère src/handrank_data.cpp à partir des sources d'OpenHoldem (tables handrank et prwin).
Usage : gen_handrank_data.py <racine du dépôt OH15> <sortie .cpp>"""
import re, sys
root, out = sys.argv[1], sys.argv[2]
def body(src, decl):
    i = src.index(decl)
    j = src.index("=", i)
    k = src.index("};", j)
    return src[j + 1:k]
hr = open(root + "/OpenHoldem/CSymbolEngineHandrank.cpp", encoding="latin-1").read()
it = open(root + "/OpenHoldem/CIteratorThread.cpp", encoding="latin-1").read()
t2652 = [int(x) for x in re.findall(r"-?\d+", body(hr, "int handrank_table_2652["))]
t169 = re.findall(r'"([^"]*)"', body(hr, "char handrank_table_169["))
pw = re.findall(r'"([^"]*)"', body(it, "char *prwin_handrank_table_169["))
assert len(t2652) == 9 * 169 and len(t169) == 9 * 169 and len(pw) == 169, (len(t2652), len(t169), len(pw))
assert all(len(s) == 3 for s in t169 + pw), "entrée de longueur inattendue"
# OpenHoldem n'initialise que 9 lignes (1..9 adversaires) sur kMaxNumberOfPlayers=10 : la 10e est nulle (C zéro-initialise)
t2652 += [0] * 169
t169 += [""] * 169
o = ["// GENERE par tools/gen_handrank_data.py depuis OpenHoldem (CSymbolEngineHandrank.cpp, CIteratorThread.cpp). Ne pas éditer.",
     '#include "pokersim/handrank.hpp"', "namespace ps::oh {"]
o.append("const int kHandrank2652[10][169] = {")
for p in range(10): o.append("  {" + ",".join(map(str, t2652[p * 169:(p + 1) * 169])) + "},")
o.append("};")
o.append("const char* const kHandrank169[10][169] = {")
for p in range(10): o.append("  {" + ",".join('"%s"' % s for s in t169[p * 169:(p + 1) * 169]) + "},")
o.append("};")
o.append("const char* const kPrwinHandrank169[169] = {" + ",".join('"%s"' % s for s in pw) + "};")
o.append("}  // namespace ps::oh")
open(out, "w").write("\n".join(o) + "\n")
print("ok:", len(t2652), len(t169), len(pw), "| ex:", t169[:3], pw[:3], t2652[:3])
