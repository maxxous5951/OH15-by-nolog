"""Lecture des profils PPL « Shanky » (fichiers .ohf / .ppl sans balises ``##f$...##``).

Un profil Shanky ressemble à ::

    MaxSessionHands = 10000          <- options du bot (ignorées)
    custom
    preflop
    When BotsLastAction = call and raises = 1
    When (hand = AA or hand = KK) Raise 90% force
    ...
    flop
    ...

On le convertit en OpenPPL en suivant les règles du convertisseur officiel du dépôt
(``OpenPPL/Converters/ShankyToOpenPPL``) : les lignes sans action restent des « WHEN » ouverts, les
lignes de continuation sont jointes, ``hand = AKs`` devient ``hand$AKSuited``, ``Raise 90%`` devient
``RaiseBy 90%``, ``Bet`` seul devient ``Raise``, etc.  Le texte produit est ensuite lu par le même
analyseur OpenPPL que les autres profils.
"""
import re

SECTIONS = ("preflop", "flop", "turn", "river")
_SECTION_RE = re.compile(r"^\s*(preflop|flop|turn|river)\s*$", re.I)
_ACTION_WORDS = ("call", "fold", "bet", "raise", "check", "play", "beep", "sitout")
_CONST_CASE = {"call": "Call", "fold": "Fold", "bet": "Bet", "raise": "Raise", "check": "Check",
               "play": "Play", "beep": "Beep", "raisemax": "RaiseMax", "raisepot": "RaisePot",
               "force": "FORCE"}
_RANK = r"[2-9TJQKA]"
# Symboles Shanky dont le nom OpenPPL diffère (table de CShankySymbolNameTranslator.cpp)
_RENAME = {
    "calledonflop": "BotCalledOnFlop",
    "calledonturn": "BotCalledOnTurn",
    "fourof1suitonturn": "FourOfOneSuitOnTurn",
    "have2ndbestkicker": "HaveSecondBestKicker",
    "have2ndbestkickerorbetter": "HaveSecondBestKickerOrBetter",
    "have2ndbestoverpairorbetter": "HaveSecondBestOverPairOrBetter",
    "have2ndnutflush": "HaveSecondNutFlush",
    "have2ndnutflushdraw": "HaveSecondNutFlushDraw",
    "have2ndnutlow": "HaveSecondNutLow",
    "have2ndnutlowdraw": "HaveSecondNutLowDraw",
    "have2ndnutstraight": "HaveSecondNutStraight",
    "have2ndoverpair": "HaveSecondOverPair",
    "have2ndtoppair": "HaveSecondTopPair",
    "have2ndtopset": "HaveSecondTopSet",
    "have3rdbestkicker": "HaveThirdBestKicker",
    "have3rdbestkickerorbetter": "HaveThirdBestKickerOrBetter",
    "have3rdbestoverpairorbetter": "HaveThirdBestOverPairOrBetter",
    "have3rdnutflush": "HaveThirdNutFlush",
    "have3rdnutflushdraw": "HaveThirdNutFlushDraw",
    "have3rdnutlow": "HaveThirdNutLow",
    "have3rdnutlowdraw": "HaveThirdNutLowDraw",
    "have3rdoverpair": "HaveThirdOverPair",
    "have3rdtoppair": "HaveThirdTopPair",
    "have3rdtopset": "HaveThirdTopSet",
    "have4thnutflush": "HaveFourthNutFlush",
    "have4thnutflushdraw": "HaveFourthNutFlushDraw",
    "have4thnutlow": "HaveFourthNutLow",
    "have4thnutlowdraw": "HaveFourthNutLowDraw",
    "have4thoverpair": "HaveFourthOverPair",
    "have4thtoppair": "HaveFourthTopPair",
    "have4thtopset": "HaveFourthTopSet",
    "have5thnutflush": "HaveFifthNutFlush",
    "have5thnutflushdraw": "HaveFifthNutFlushDraw",
    "have5thoverpair": "HaveFifthOverPair",
    "havebackdoor2ndnutflushdraw": "HaveBackdoorSecondNutFlushDraw",
    "havebackdoor3rdnutflushdraw": "HaveBackdoorThirdNutFlushDraw",
    "maxcurrentopponentstacksize": "MaxOpponentsLeftStackSize",
    "morethanonestraightpossibleonturn": "MoreThanOneStraightPossibleOnRiver",
    "nutfullhouseorfourofakind": "NutFullHouseOrFourOfAKind_ShankyCompatible",
    "sitout": "Fold",
}
_RENAME_RE = re.compile(r"(?i)\b(calledonflop|calledonturn|fourof1suitonturn|have2ndbestkicker|have2ndbestkickerorbetter|have2ndbestoverpairorbetter|have2ndnutflush|have2ndnutflushdraw|have2ndnutlow|have2ndnutlowdraw|have2ndnutstraight|have2ndoverpair|have2ndtoppair|have2ndtopset|have3rdbestkicker|have3rdbestkickerorbetter|have3rdbestoverpairorbetter|have3rdnutflush|have3rdnutflushdraw|have3rdnutlow|have3rdnutlowdraw|have3rdoverpair|have3rdtoppair|have3rdtopset|have4thnutflush|have4thnutflushdraw|have4thnutlow|have4thnutlowdraw|have4thoverpair|have4thtoppair|have4thtopset|have5thnutflush|have5thnutflushdraw|have5thoverpair|havebackdoor2ndnutflushdraw|havebackdoor3rdnutflushdraw|maxcurrentopponentstacksize|morethanonestraightpossibleonturn|nutfullhouseorfourofakind|sitout)\b")


def is_shanky(text):
    """Vrai si le texte est du PPL Shanky : aucune balise ##...## et une section preflop/flop/turn/river."""
    has_section = False
    for line in text.splitlines():
        s = line.strip()
        if s.startswith("##"):
            return False
        if _SECTION_RE.match(s):
            has_section = True
    return has_section


def _convert_line(line):
    """Convertit une ligne « When ... » complète (continuations déjà jointes)."""
    s = line.strip()
    s = re.sub(r"(?i)\bdelay\s+\S+\s*$", "", s).strip()
    # noms de symboles Shanky -> OpenPPL, puis 2nd -> second, ...
    s = _RENAME_RE.sub(lambda m: _RENAME[m.group(1).lower()], s)
    for a, b in (("2nd", "second"), ("3rd", "third"), ("4th", "fourth"), ("5th", "fifth")):
        s = re.sub(a, b, s, flags=re.I)
    # « in small blind » / « in button » -> InSmallBlind / InButton
    s = re.sub(r"(?i)\bin\s+(?=[sb])", "In", s)
    # mains : hand = AKs / hand = AJ suited / hand = A / board = 2
    def _hand(m):
        kind = m.group(1).lower()
        ranks = m.group(2).upper()
        suf = "Suited" if (m.group(3) or m.group(4)) else ""
        return "%s$%s%s" % (kind, ranks, suf)
    s = re.sub(r"(?i)\b(hand|board)\s*=\s*(%s{1,2})(?:(s)(?![A-Za-z0-9$])|(?![A-Za-z0-9$])(?:\s+(suited))?)" % _RANK, _hand, s)
    # « = » sur des montants : comparaison approchée de l'ancien PPL
    s = re.sub(r"(?i)\b(BetSize|StackSize|AmountToCall)\s*=(?![=~])", r"\1 ~~", s)
    # actions et constantes : casse OpenPPL (Call/Fold/Bet/Raise sont des constantes, call/bet des montants)
    def _const(m):
        return _CONST_CASE[m.group(0).lower()]
    s = re.sub(r"(?i)\b(call|fold|bet|raise|check|play|beep|raisemax|raisepot|force)\b(?!\$)", _const, s)
    # FORCE manquant en fin de ligne
    low = s.lower().split()
    if low and low[-1] != "force":
        tail = low[-1]
        prev = low[-2] if len(low) > 1 else ""
        if tail.startswith(_ACTION_WORDS) and prev not in ("<", ">", "=", "<=", ">=", "and", "or", "not", "!=", "%") \
                and not re.match(r"^when$", tail):
            s += " FORCE"
        elif prev.startswith("raise") and re.match(r"^\d+(\.\d+)?%?$", tail):
            s += " FORCE"
    # Raise N / Bet N (+ %) -> RaiseBy ; Bet seul -> Raise
    s = re.sub(r"(?i)\b(?:Raise|Bet)\s+(\d+(?:\.\d+)?\s*%?)\s+FORCE\s*$", lambda m: "RaiseBy %s FORCE" % m.group(1).replace(" ", ""), s)
    s = re.sub(r"(?i)\bBet\s+FORCE\s*$", "Raise FORCE", s)
    return s


def convert(text):
    """Texte Shanky -> texte OpenPPL (.ohf) avec les fonctions f$preflop, f$flop, f$turn, f$river."""
    out = []
    cur = ""
    in_sections = False

    def flush():
        nonlocal cur
        if cur:
            out.append("\t" + _convert_line(cur))
            cur = ""
    for raw in text.replace("﻿", "").splitlines():
        s = raw.strip().replace("\t", " ")
        if not s:
            continue
        if s.startswith("//"):
            flush()
            if in_sections:
                out.append(s)
            continue
        if "//" in s:
            s = s[:s.index("//")].strip()
            if not s:
                continue
        m = _SECTION_RE.match(s)
        if m:
            flush()
            in_sections = True
            out.append("\n##f$%s##" % m.group(1).lower())
            continue
        if not in_sections:
            continue          # options du bot, mot-clé « custom », ...
        if s.lower().split()[0] == "when":
            flush()
            cur = s
        else:
            cur = (cur + " " + s) if cur else s
    flush()
    return "\n".join(out) + "\n"
