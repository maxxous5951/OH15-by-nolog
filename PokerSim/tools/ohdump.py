"""Équivalent Python de apps/ohdump : référence pour le test différentiel (utilise SpinGoSimulator/spingosim)."""
import os, sys
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "SpinGoSimulator"))
from spingosim import ohlang as L, shanky

def fmt(v): return "%.17g" % v
def dump(n, ids):
    if n is None: return "nil"
    k = type(n).__name__
    if k == "Num": return "(num %s)" % fmt(n.value)
    if k == "Ident": return "(id %s)" % n.name
    if k == "Unary": return "(u %s %s)" % (n.op, dump(n.a, ids))
    if k == "Bracket": return "(( %s)" % dump(n.a, ids)
    if k == "Binary": return "(b %s %s %s)" % (n.op, dump(n.a, ids), dump(n.b, ids))
    if k == "Ternary": return "(? %s %s %s)" % (dump(n.c, ids), dump(n.a, ids), dump(n.b, ids))
    if k == "UserVarSet": return "(set %s)" % n.name
    if k == "BetsizeAction": return "(bs %s %s)" % (n.mode, dump(n.expr, ids))
    if k == "EndOfFunction":
        if id(n) in ids: return "(ref %d)" % ids[id(n)]
        i = len(ids); ids[id(n)] = i
        return "(end#%d %s)" % (i, n.fallback or "-")
    if k == "When":
        if id(n) in ids: return "(ref %d)" % ids[id(n)]
        i = len(ids); ids[id(n)] = i
        return "(when#%d %s %s %s)" % (i, dump(n.cond, ids), dump(n.second, ids), dump(n.third, ids))
    raise Exception(k)

for path in sys.argv[1:]:
    print("FILE", path)
    try:
        raw = open(path, "rb").read()
        if raw[:3] == b"\xef\xbb\xbf": raw = raw[3:]
        text = raw.decode("latin-1")
        fc = L.FormulaCollection()
        fc.add_text(shanky.convert(text) if shanky.is_shanky(text) else text, path)
        fc.parse_all()
        for name in sorted(fc.functions):
            f = fc.functions[name]
            if f.error: print("%s\tERROR" % name)
            elif f.is_list: print("%s\tLIST %d" % (name, len(f.hand_list) if f.hand_list is not None else -1))
            elif f.tree is not None: print("%s\t%s" % (name, dump(f.tree, {})))
            else: print("%s\tSKIP" % name)
    except Exception as e:
        print("FATAL\t%s" % e)
