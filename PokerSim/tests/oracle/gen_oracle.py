#!/usr/bin/env python3
"""Extrait CalculatePokerval du code ORIGINAL d'OpenHoldem (CSymbolEnginePokerval.cpp) et le transforme en
fonction autonome (seul l'accès à p_table_state est remplacé par un masque de cartes passé en paramètre).
Usage : gen_oracle.py <racine du dépôt OH15> <fichier de sortie .inc>"""
import re, sys
root, out = sys.argv[1], sys.argv[2]
src = open(root + "/OpenHoldem/CSymbolEnginePokerval.cpp", encoding="latin-1").read()
a = src.index("int CSymbolEnginePokerval::CalculatePokerval(")
b = src.index("bool CSymbolEnginePokerval::EvaluateSymbol(")
fn = src[a:b]
fn = fn.replace("int CSymbolEnginePokerval::CalculatePokerval(HandVal hv, int n, int *pcb, int card0, int card1)",
                "static int oracle_calc(HandVal hv, int n, int *pcb, int card0, int card1, CardMask AllCards)", 1)
# bloc qui lit p_table_state : de "CardMask_RESET(Cards);" jusqu'à "max = 0;" -> on utilise le masque fourni
i = fn.index("CardMask_RESET(Cards);")
j = fn.index("max = 0;")
fn = fn[:i] + "Cards = AllCards;\n\n\t\t" + fn[j:]
assert "p_table_state" not in fn and "NumberOfCardsPerPlayer" not in fn, "dépendance inattendue"
open(out, "w", encoding="latin-1").write(fn)
print("oracle généré :", out, len(fn.splitlines()), "lignes")
