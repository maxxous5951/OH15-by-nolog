#!/usr/bin/env python3
"""Compacte versus.bin d'OpenHoldem (1326 mains héros x 1225 mains adverses x (victoires, défaites) sur les 1 712 304 boards)
en une table canonique : deux matchups identiques à une permutation des couleurs près (ou à l'échange héros/adversaire près,
avec victoires/défaites échangées) ont les mêmes comptes. Vérifie chacune des 1 624 350 entrées d'origine.
Usage : gen_versus_table.py <versus.bin> <src/versus_data.cpp>"""
import itertools, sys
import numpy as np

src, out = sys.argv[1], sys.argv[2]
a = np.fromfile(src, dtype='<u4').reshape(1326, 1225, 2).astype(np.int64)
pairs = [(i, j) for i in range(51) for j in range(i + 1, 52)]            # indice poker-eval : couleur*13 + rang
# tableaux (héros, adversaire) pour les 1 624 350 entrées
H1 = np.empty((1326, 1225), np.int64); H2 = np.empty_like(H1); O1 = np.empty_like(H1); O2 = np.empty_like(H1)
for k, (h0, h1) in enumerate(pairs):
    opp = [p for p in pairs if h0 not in p and h1 not in p]
    H1[k, :] = h0; H2[k, :] = h1
    O1[k, :] = [p[0] for p in opp]; O2[k, :] = [p[1] for p in opp]
H1, H2, O1, O2 = (x.ravel() for x in (H1, H2, O1, O2))
W = a[:, :, 0].ravel(); L = a[:, :, 1].ravel()
rank = lambda c: c % 13
suit = lambda c: c // 13

best = np.full(H1.shape, np.iinfo(np.int64).max, np.int64)
swapped = np.zeros(H1.shape, bool)
for perm in itertools.permutations(range(4)):
    p = np.array(perm)
    def code(c): return rank(c) * 4 + p[suit(c)]
    h = np.sort(np.stack([code(H1), code(H2)]), axis=0); o = np.sort(np.stack([code(O1), code(O2)]), axis=0)
    for sw in (False, True):
        a1, a2 = (o, h) if sw else (h, o)
        key = (a1[0] << 18) | (a1[1] << 12) | (a2[0] << 6) | a2[1]
        better = key < best
        best = np.where(better, key, best)
        swapped = np.where(better, sw, swapped)
# comptes orientés selon la forme canonique
cw = np.where(swapped, L, W); cl = np.where(swapped, W, L)
order = np.argsort(best, kind='stable')
bk = best[order]; first = np.concatenate([[True], bk[1:] != bk[:-1]])
ukeys = bk[first]; uw = cw[order][first]; ul = cl[order][first]
grp = np.cumsum(first) - 1
# cohérence : tous les membres d'un groupe ont les mêmes comptes canoniques
ok = (cw[order] == uw[grp]) & (cl[order] == ul[grp])
print("entrées d'origine :", H1.size, "| matchups canoniques :", ukeys.size, "| entrées incohérentes :", int((~ok).sum()))
assert ok.all(), "le fichier n'est pas invariant par permutation des couleurs : table non compactable"
# relecture : reconstruit chaque entrée depuis la table compacte
idx = np.searchsorted(ukeys, best)
assert (ukeys[idx] == best).all()
rw = np.where(swapped, ul[idx], uw[idx]); rl = np.where(swapped, uw[idx], ul[idx])
assert (rw == W).all() and (rl == L).all()
print("relecture : les", H1.size, "entrées d'origine sont retrouvées à l'identique")
assert int(uw.max()) < 2**21 and int(ul.max()) < 2**21
with open(out, "w") as f:
    f.write("// GENERE par tools/gen_versus_table.py à partir de versus.bin (OpenHoldem, 2007-10-13). Ne pas éditer.\n")
    f.write("// %d matchups canoniques (permutation des couleurs, échange héros/adversaire) : clé, victoires, défaites sur 1712304 boards.\n" % ukeys.size)
    f.write('#include "pokersim/versus.hpp"\nnamespace ps::oh {\nconst unsigned kVersusEntries = %d;\nconst unsigned kVersusData[] = {\n' % ukeys.size)
    for i in range(0, ukeys.size, 4):
        f.write(",".join("%d,%d,%d" % (ukeys[j], uw[j], ul[j]) for j in range(i, min(i + 4, ukeys.size))) + ",\n")
    f.write("};\n}  // namespace ps::oh\n")
print("écrit :", out)
