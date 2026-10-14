"""Interpréteur du langage de formules OpenHoldem (OH-script + OpenPPL).

Port fidèle de la grammaire de ``CTokenizer`` / ``CFormulaParser`` /
``CParseTreeOperatorNode`` du dépôt :

* fichiers ``.ohf`` découpés par en-têtes ``##nom##`` (fonctions, listes, notes) ;
* opérateurs avec les mêmes priorités qu'OpenHoldem ;
* conditions OpenPPL ``WHEN ... <action> FORCE`` (y compris WHEN « open-ended »,
  avec le même chaînage de frères que ``BackPatchOpenEndedWhenConditionSequence``) ;
* ``RaiseTo``, ``RaiseBy``, ``RaiseBy x%``, ``Return``, ``Set user_xxx`` ;
* cache de résultat par fonction (remis à zéro à chaque décision).
"""
import math
import os
import re

EPSILON = 1e-6

# --------------------------------------------------------------------------
#  Tokens
# --------------------------------------------------------------------------
T_EOF = "EOF"
T_NUM = "NUM"
T_IDENT = "IDENT"
T_CARDS = "CARDS"
T_OP = "OP"
T_OPEN = "OPEN"
T_CLOSE = "CLOSE"
T_WHEN = "WHEN"
T_FORCE = "FORCE"
T_RETURN = "RETURN"
T_SET = "SET"
T_SHEBANG = "SHEBANG"

# Priorités (identiques à kOperatorPriority)
PRIORITY = {
    "+": 11, "-": 11, "*": 12, "/": 12, "mod": 12, "%": 12, "%pct": 12,
    "**": 14, "==": 8, "~~": 8, "!=": 8, "<": 9, "<=": 9, ">": 9, ">=": 9,
    "&&": 4, "||": 2, "^^": 3, "&": 7, "|": 5, "^": 6, "<<": 10, ">>": 10,
    "?": 1,
}
UNARY_OPS = {"neg", "ln", "~", "!", "`"}
RIGHT_ASSOC = {"**", "?"}

KEYWORDS = {
    "or": ("OP", "||"), "and": ("OP", "&&"), "not": ("OP", "!"), "xor": ("OP", "^^"),
    "mod": ("OP", "mod"), "ln": ("OP", "ln"),
    "bitor": ("OP", "|"), "bitand": ("OP", "&"), "bitxor": ("OP", "^"),
    "bitnot": ("OP", "~"), "bitcount": ("OP", "`"),
}

# Actions OpenPPL (kOpenPPLActionStrings) -> nom canonique évalué comme symbole
OPENPPL_ACTIONS = [
    "Bet", "Call", "Fold", "Play", "Beep", "Raise", "RaiseTo", "RaiseBy", "Check",
    "Allin", "BetMin", "BetFourthPot", "BetThirdPot", "BetHalfPot", "BetTwoThirdPot",
    "BetThreeFourthPot", "BetPot", "BetMax", "RaiseMin", "RaiseFourthPot",
    "RaiseThirdPot", "RaiseHalfPot", "RaiseTwoThirdPot", "RaiseThreeFourthPot",
    "RaisePot", "RaiseMax", "SitOut",
]
_ACTION_LOWER = {a.lower(): a for a in OPENPPL_ACTIONS}


class ParseError(Exception):
    pass


class Token:
    __slots__ = ("kind", "text", "op", "line")

    def __init__(self, kind, text, op=None, line=0):
        self.kind = kind
        self.text = text
        self.op = op
        self.line = line

    def __repr__(self):
        return "Token(%s,%r)" % (self.kind, self.text)


def string_to_number(s):
    """Nombres décimaux, hexadécimaux (0x..) et binaires (0b..)."""
    s = s.strip()
    low = s.lower()
    neg = False
    if low.startswith("-"):
        neg = True
        low = low[1:]
    if low.startswith("0x"):
        v = float(int(low[2:], 16))
    elif low.startswith("0b"):
        v = float(int(low[2:], 2))
    else:
        v = float(low)
    return -v if neg else v


def _is_card_rank_char(ch):
    return ch.upper() in "23456789TJQKA"


def tokenize(text, is_list=False):
    """Découpe le texte d'une fonction en tokens."""
    toks = []
    i = 0
    n = len(text)
    line = 1
    inside_openppl = False
    last_kind = None   # pour le "moins binaire"

    def binary_minus_context():
        return last_kind in (T_IDENT, T_NUM, T_CLOSE, T_CARDS)

    while i < n:
        ch = text[i]
        if ch == "\n":
            line += 1
            i += 1
            continue
        if ch.isspace() or (is_list and ch == ","):
            i += 1
            continue
        # Commentaires
        if ch == "/" and i + 1 < n and text[i + 1] == "/":
            while i < n and text[i] != "\n":
                i += 1
            continue
        if ch == "/" and i + 1 < n and text[i + 1] == "*":
            j = text.find("*/", i + 2)
            if j < 0:
                raise ParseError("Commentaire multi-lignes non terminé (ligne %d)" % line)
            line += text.count("\n", i, j)
            i = j + 2
            continue
        # Identifiants / mots-clés
        if ch.isalpha() or ch == "$":
            j = i
            while j < n and (text[j].isalnum() or text[j] in "$_"):
                j += 1
            word = text[i:j]
            low = word.lower()
            i = j
            if low in KEYWORDS and not is_list:
                k, op = KEYWORDS[low]
                toks.append(Token(T_OP, word, op, line))
                last_kind = T_OP
                continue
            if low == "when" and not is_list:
                inside_openppl = True
                toks.append(Token(T_WHEN, word, None, line))
                last_kind = T_WHEN
                continue
            if low == "force":
                toks.append(Token(T_FORCE, word, None, line))
                last_kind = T_FORCE
                continue
            if low == "return":
                toks.append(Token(T_RETURN, word, None, line))
                last_kind = T_RETURN
                continue
            if low == "set":
                toks.append(Token(T_SET, word, None, line))
                last_kind = T_SET
                continue
            toks.append(Token(T_IDENT, word, None, line))
            last_kind = T_IDENT
            continue
        # Nombres et mains 72o / 54s
        if ch.isdigit() or ch == ".":
            if (i + 2 < n and _is_card_rank_char(text[i + 1])
                    and text[i + 2].lower() in "so"):
                j = i
                while j < n and text[j].isalnum():
                    j += 1
                toks.append(Token(T_CARDS, text[i:j], None, line))
                i = j
                last_kind = T_CARDS
                continue
            j = i
            # 0b... / 0x...
            while j < n and (text[j] in "0123456789abcdefABCDEF.xX"
                             or (text[j] in "bB" and j == i + 1 and text[i] == "0")):
                j += 1
            toks.append(Token(T_NUM, text[i:j], None, line))
            i = j
            last_kind = T_NUM
            continue
        two = text[i:i + 2]
        # Opérateurs
        if ch in "+-*/<>`=!?:%&|~^":
            if is_list and ch in "+-:":
                toks.append(Token(T_OP, ch, {"+": "list+", "-": "list-", ":": "list:"}[ch], line))
                i += 1
                last_kind = T_OP
                continue
            op = None
            adv = 1
            if ch == "+":
                op = "+"
            elif ch == "-":
                if binary_minus_context():
                    op = "-"
                else:
                    if i + 1 < n and text[i + 1].isdigit():
                        # nombre négatif
                        j = i + 1
                        while j < n and (text[j] in "0123456789abcdefABCDEF.xX"):
                            j += 1
                        toks.append(Token(T_NUM, text[i:j], None, line))
                        i = j
                        last_kind = T_NUM
                        continue
                    op = "neg"
            elif ch == "*":
                if two == "**":
                    op, adv = "**", 2
                else:
                    op = "*"
            elif ch == "/":
                op = "/"
            elif ch == "<":
                if two == "<=":
                    op, adv = "<=", 2
                elif two == "<<":
                    op, adv = "<<", 2
                else:
                    op = "<"
            elif ch == ">":
                if two == ">=":
                    op, adv = ">=", 2
                elif two == ">>":
                    op, adv = ">>", 2
                else:
                    op = ">"
            elif ch == "`":
                op = "`"
            elif ch == "=":
                op, adv = "==", (2 if two == "==" else 1)
            elif ch == "!":
                if two == "!=":
                    op, adv = "!=", 2
                else:
                    op = "!"
            elif ch == "?":
                op = "?"
            elif ch == ":":
                op = ":"
            elif ch == "%":
                op = "%pct" if inside_openppl else "mod"
            elif ch == "&":
                if two == "&&":
                    op, adv = "&&", 2
                else:
                    op = "&"
            elif ch == "|":
                if two == "||":
                    op, adv = "||", 2
                else:
                    op = "|"
            elif ch == "~":
                if two == "~~":
                    op, adv = "~~", 2
                else:
                    op = "~"
            elif ch == "^":
                if two == "^^":
                    op, adv = "^^", 2
                else:
                    op = "^"
            toks.append(Token(T_OP, text[i:i + adv], op, line))
            i += adv
            last_kind = T_OP
            continue
        if ch in "([{":
            toks.append(Token(T_OPEN, ch, None, line))
            i += 1
            last_kind = T_OPEN
            continue
        if ch in ")]}":
            toks.append(Token(T_CLOSE, ch, None, line))
            i += 1
            last_kind = T_CLOSE
            continue
        if ch == "#" and text[i:i + 2] == "##":
            toks.append(Token(T_SHEBANG, "##", None, line))
            i += 2
            last_kind = T_SHEBANG
            inside_openppl = False
            continue
        if ch == '"':
            j = text.find('"', i + 1)
            if j < 0:
                j = n - 1
            toks.append(Token(T_IDENT, text[i:j + 1], None, line))
            i = j + 1
            last_kind = T_IDENT
            continue
        raise ParseError("Caractère inattendu %r (ligne %d)" % (ch, line))
    toks.append(Token(T_EOF, "", None, line))
    return toks


# --------------------------------------------------------------------------
#  Noeuds de l'arbre syntaxique
# --------------------------------------------------------------------------
class Node:
    __slots__ = ()
    kind = "node"

    def ev(self, ctx):  # pragma: no cover
        raise NotImplementedError


class Num(Node):
    __slots__ = ("value",)

    def __init__(self, value):
        self.value = value

    def ev(self, ctx):
        return self.value


class Ident(Node):
    __slots__ = ("name",)
    kind = "ident"

    def __init__(self, name):
        self.name = name

    def ev(self, ctx):
        return ctx.symbol(self.name)


class Unary(Node):
    __slots__ = ("op", "a")

    def __init__(self, op, a):
        self.op = op
        self.a = a

    def ev(self, ctx):
        v = self.a.ev(ctx)
        op = self.op
        if op == "neg":
            return -v
        if op == "!":
            return 0.0 if v else 1.0
        if op == "ln":
            return math.log(v) if v > 0 else 0.0
        if op == "~":
            return float(~int(v) & 0xFFFFFFFF)
        if op == "`":
            return float(bin(int(v) & 0xFFFFFFFF).count("1"))
        if op == "(":
            return v
        raise ParseError("Opérateur unaire inconnu " + op)


class Bracket(Node):
    __slots__ = ("a",)

    def __init__(self, a):
        self.a = a

    def ev(self, ctx):
        return self.a.ev(ctx)


class Binary(Node):
    __slots__ = ("op", "a", "b")

    def __init__(self, op, a, b):
        self.op = op
        self.a = a
        self.b = b

    def ev(self, ctx):
        op = self.op
        x = self.a.ev(ctx)
        if op == "&&":
            if not x:
                return 0.0
            return 1.0 if self.b.ev(ctx) else 0.0
        if op == "||":
            if x:
                return 1.0
            return 1.0 if self.b.ev(ctx) else 0.0
        y = self.b.ev(ctx)
        if op == "+":
            return x + y
        if op == "-":
            return x - y
        if op == "*":
            return x * y
        if op == "/":
            return x / y if y != 0 else 0.0
        if op == "mod":
            ix, iy = int(x), int(y)
            return float(ix % iy) if iy != 0 else 0.0
        if op == "**":
            try:
                return float(x ** y)
            except (OverflowError, ZeroDivisionError, ValueError):
                return 0.0
        if op == "==":
            return 1.0 if abs(x - y) < EPSILON else 0.0
        if op == "~~":
            return 1.0 if int(x + 0.5) == int(y + 0.5) else 0.0
        if op == "!=":
            return 1.0 if x != y else 0.0
        if op == "<":
            return 1.0 if x <= y - EPSILON else 0.0
        if op == "<=":
            return 1.0 if x < y + EPSILON else 0.0
        if op == ">":
            return 1.0 if x >= y + EPSILON else 0.0
        if op == ">=":
            return 1.0 if x > y - EPSILON else 0.0
        if op == "^^":
            return 1.0 if bool(x) != bool(y) else 0.0
        if op == "&":
            return float(int(x) & int(y))
        if op == "|":
            return float(int(x) | int(y))
        if op == "^":
            return float(int(x) ^ int(y))
        if op == "<<":
            return float((int(x) << int(y)) & 0xFFFFFFFF) if 0 <= int(y) < 32 else 0.0
        if op == ">>":
            return float(int(x) >> int(y)) if 0 <= int(y) < 32 else 0.0
        if op == "%pct":
            return x * y * 0.01
        raise ParseError("Opérateur binaire inconnu " + op)


class Ternary(Node):
    __slots__ = ("c", "a", "b")

    def __init__(self, c, a, b):
        self.c = c
        self.a = a
        self.b = b

    def ev(self, ctx):
        return self.a.ev(ctx) if self.c.ev(ctx) else self.b.ev(ctx)


class UserVarSet(Node):
    """``Set user_xxx`` / ``Set me_st_...`` : positionne puis continue."""
    __slots__ = ("name",)
    kind = "uservar"

    def __init__(self, name):
        self.name = name

    def ev(self, ctx):
        return ctx.set_variable(self.name)


class EndOfFunction(Node):
    """Fin de séquence de WHEN : DefaultPreflop... ou 0 (= check/fold)."""
    __slots__ = ("fallback",)
    kind = "end"

    def __init__(self, fallback):
        self.fallback = fallback

    def ev(self, ctx):
        if self.fallback and ctx.has_function(self.fallback):
            return ctx.symbol(self.fallback)
        return 0.0


class BetsizeAction(Node):
    """RaiseTo N / RaiseBy N / RaiseBy N% (en big blinds ; renvoie une valeur positive)."""
    __slots__ = ("mode", "expr")
    kind = "betsize"

    def __init__(self, mode, expr):
        self.mode = mode
        self.expr = expr

    def ev(self, ctx):
        v = self.expr.ev(ctx)
        if self.mode == "to":
            return v
        if self.mode == "by":
            return ctx.symbol("ncallbets") + v
        # raiseby X% : pourcentage du pot après call
        bet = ctx.symbol("bet")
        pot_after_call_bb = (ctx.symbol("pot") / bet if bet else 0.0) + ctx.symbol("nbetstocall")
        return ctx.symbol("ncallbets") + 0.01 * v * pot_after_call_bb


class When(Node):
    """Noeud WHEN avec les mêmes liens que OpenHoldem (cond, then, else)."""
    __slots__ = ("cond", "second", "third")
    kind = "when"

    def __init__(self, cond):
        self.cond = cond
        self.second = None
        self.third = None

    def is_open_ended(self):
        if self.second is None and self.third is None:
            return True
        return isinstance(self.second, When)

    def ev(self, ctx):
        if self.cond.ev(ctx):
            val = self.second.ev(ctx) if self.second is not None else 0.0
            if not isinstance(self.second, UserVarSet):
                return val
            # variable utilisateur : on continue avec la condition suivante
        return self.third.ev(ctx) if self.third is not None else 0.0


# --------------------------------------------------------------------------
#  Parseur
# --------------------------------------------------------------------------
class Parser:
    def __init__(self, tokens, fname="", fallback_name=None):
        self.toks = tokens
        self.pos = 0
        self.fname = fname
        self.fallback_name = fallback_name

    # --- utilitaires
    def peek(self, k=0):
        p = self.pos + k
        if p >= len(self.toks):
            return self.toks[-1]
        return self.toks[p]

    def get(self):
        t = self.toks[self.pos]
        if t.kind != T_EOF:
            self.pos += 1
        return t

    def error(self, msg, tok=None):
        tok = tok or self.peek()
        raise ParseError("%s [fonction %s, ligne %d, près de %r]" % (msg, self.fname, tok.line, tok.text))

    # --- point d'entrée
    def parse_function_body(self):
        t = self.peek()
        if t.kind == T_EOF:
            return EndOfFunction(self.fallback_name)
        if t.kind == T_WHEN:
            first = self.parse_when_sequence()
            self.backpatch(first)
            return first
        expr = self.parse_expression(0)
        if self.peek().kind != T_EOF:
            if self.peek().kind == T_WHEN:
                self.error("Condition WHEN après une expression OH-script")
            self.error("Tokens superflus après la fin de la fonction")
        return expr

    # --- expressions (Pratt)
    def parse_expression(self, min_prio):
        left = self.parse_prefix()
        while True:
            t = self.peek()
            if t.kind != T_OP or t.op not in PRIORITY:
                break
            op = t.op
            prio = PRIORITY[op]
            if prio < min_prio:
                break
            if op == "%pct":
                nxt = self.peek(1)
                if nxt.kind in (T_FORCE, T_WHEN, T_EOF):
                    break   # "RaiseBy 60% Force"
            if op == "?":
                self.get()
                a = self.parse_expression(0)
                c = self.get()
                if not (c.kind == T_OP and c.op == ":"):
                    self.error("Attendu ':' dans l'expression conditionnelle", c)
                b = self.parse_expression(PRIORITY["?"])
                left = Ternary(left, a, b)
                continue
            self.get()
            nxt_min = prio if op in RIGHT_ASSOC else prio + 1
            right = self.parse_expression(nxt_min)
            left = Binary(op, left, right)
        return left

    def parse_prefix(self):
        t = self.peek()
        if t.kind == T_OPEN:
            self.get()
            e = self.parse_expression(0)
            c = self.get()
            if c.kind != T_CLOSE:
                self.error("Parenthèse fermante attendue", c)
            return Bracket(e)
        if t.kind == T_OP and t.op in UNARY_OPS:
            self.get()
            # priorité unaire 13 ; ln = 14
            prio = 14 if t.op == "ln" else 13
            e = self.parse_expression(prio)
            return Unary(t.op, e)
        if t.kind == T_NUM:
            self.get()
            try:
                return Num(string_to_number(t.text))
            except ValueError:
                self.error("Nombre invalide", t)
        if t.kind == T_IDENT:
            self.get()
            return Ident(t.text)
        if t.kind == T_CARDS:
            self.get()
            return Ident(t.text)
        self.error("Token inattendu dans une expression", t)

    # --- OpenPPL
    def parse_when_sequence(self):
        last = None
        last_open = False
        first = None
        while self.peek().kind == T_WHEN:
            self.get()
            nt = self.peek().kind
            if nt in (T_EOF, T_WHEN):
                self.error("Condition WHEN vide")
            cond = self.parse_expression(0)
            w = When(cond)
            if first is None:
                first = w
            if last is not None:
                if last_open:
                    last.second = w
                else:
                    last.third = w
            last = w
            t = self.peek()
            if self.is_action_token(t):
                w.second = self.parse_action()
                last_open = False
            elif t.kind == T_WHEN:
                last_open = True
                continue
            elif t.kind == T_EOF:
                break
            else:
                self.error("Action manquante après la condition WHEN", t)
        return first

    def is_action_token(self, t):
        if t.kind in (T_RETURN, T_SET):
            return True
        if t.kind == T_IDENT and t.text.lower() in _ACTION_LOWER:
            return True
        return False

    def expect_force(self):
        t = self.peek()
        if t.kind != T_FORCE:
            self.error("Mot-clé FORCE manquant après l'action", t)
        self.get()
        if self.peek().kind == T_FORCE:
            self.error("FORCE en double")
        # Delay Shanky ignoré s'il existe
        t = self.peek()
        if t.kind == T_IDENT and t.text.lower() == "delay":
            self.get()
            if self.peek().kind == T_NUM:
                self.get()

    def parse_action(self):
        t = self.get()
        if t.kind == T_RETURN:
            e = self.parse_expression(0)
            self.expect_force()
            return e
        if t.kind == T_SET:
            v = self.get()
            if v.kind != T_IDENT:
                self.error("Variable utilisateur attendue après SET", v)
            name = v.text
            # Commande avec chaîne (prw1326_cmd "RECALC") : on ignore l'argument
            if self.peek().kind == T_IDENT and self.peek().text.startswith('"'):
                self.get()
            return UserVarSet(name)
        name = _ACTION_LOWER[t.text.lower()]
        low = name.lower()
        if low == "raiseto":
            e = self.parse_expression(0)
            self.expect_force()
            return BetsizeAction("to", e)
        if low == "raiseby":
            e = self.parse_expression(0)
            if self.peek().kind == T_OP and self.peek().op == "%pct":
                self.get()
                self.expect_force()
                return BetsizeAction("pct", e)
            self.expect_force()
            return BetsizeAction("by", e)
        if low == "raise" and self.peek().kind != T_FORCE:
            # Raise N (style Shanky) -> raiseby
            e = self.parse_expression(0)
            if self.peek().kind == T_OP and self.peek().op == "%pct":
                self.get()
                self.expect_force()
                return BetsizeAction("pct", e)
            self.expect_force()
            return BetsizeAction("by", e)
        self.expect_force()
        return Ident(name)

    def backpatch(self, first):
        last_open = None
        cur = first
        last_when = cur
        guard = 0
        while cur is not None:
            guard += 1
            if guard > 100000:
                break
            last_when = cur
            if not isinstance(cur, When):
                break
            if cur.is_open_ended():
                if last_open is not None and last_open is not cur:
                    last_open.third = cur
                last_open = cur
                cur = cur.second
            else:
                right = cur.third
                if right is None or not isinstance(right, When):
                    break
                cur = right
        end = EndOfFunction(self.fallback_name)
        if isinstance(last_when, When):
            last_when.third = end
        if last_open is not None and last_open.is_open_ended():
            last_open.third = end


# --------------------------------------------------------------------------
#  Listes de mains
# --------------------------------------------------------------------------
_RANKS = "23456789TJQKA"


def _expand_hand_token(tok):
    """'AKs' -> {'AKs'}, 'QQ' -> {'QQo'}, 'AK' -> {'AKs','AKo'} ; renvoie un ensemble."""
    tok = tok.strip()
    if len(tok) < 2:
        return set()
    r0, r1 = tok[0].upper(), tok[1].upper()
    if r0 not in _RANKS or r1 not in _RANKS:
        return set()
    suffix = tok[2:].lower()
    if r0 == r1:
        return {r0 + r1 + "o"}
    if r0 < r1 and _RANKS.index(r0) < _RANKS.index(r1):
        r0, r1 = r1, r0
    if suffix == "s":
        return {r0 + r1 + "s"}
    if suffix == "o":
        return {r0 + r1 + "o"}
    return {r0 + r1 + "s", r0 + r1 + "o"}


def parse_hand_list(text):
    """Convertit le contenu d'une ``listXYZ`` en ensemble de mains ('AKs', 'QQo'...)."""
    # Normalise : on retire les poids ":nn" qui suivent un token
    cleaned = re.sub(r"//[^\n]*", " ", text)
    cleaned = re.sub(r"/\*.*?\*/", " ", cleaned, flags=re.S)
    cleaned = cleaned.replace(",", " ")
    result = set()
    raw = cleaned.split()
    # Fusionne "AA:100" -> "AA", "QQ-99" gère les groupes, "A2s+" les plages ouvertes
    for tok in raw:
        weight = None
        if ":" in tok:
            base, w = tok.split(":", 1)
            tok = base
            try:
                weight = float(w)
            except ValueError:
                weight = None
        if weight is not None and weight <= 0:
            continue
        if "-" in tok:
            a, b = tok.split("-", 1)
            result |= _expand_range(a, b)
            continue
        if tok.endswith("+"):
            result |= _expand_plus(tok[:-1])
            continue
        result |= _expand_hand_token(tok)
    return result


def _expand_plus(tok):
    tok = tok.strip()
    if len(tok) < 2:
        return set()
    r0, r1 = tok[0].upper(), tok[1].upper()
    if r0 not in _RANKS or r1 not in _RANKS:
        return set()
    i0, i1 = _RANKS.index(r0), _RANKS.index(r1)
    suffix = tok[2:].lower()
    out = set()
    if r0 == r1:
        for i in range(i0, 13):
            out.add(_RANKS[i] * 2 + "o")
        return out
    hi, lo = (i0, i1) if i0 > i1 else (i1, i0)
    for j in range(lo, hi):
        out |= _expand_hand_token(_RANKS[hi] + _RANKS[j] + suffix)
    return out


def _expand_range(a, b):
    a, b = a.strip(), b.strip()
    if len(a) < 2 or len(b) < 2:
        return set()
    try:
        a0, a1 = _RANKS.index(a[0].upper()), _RANKS.index(a[1].upper())
        b0, b1 = _RANKS.index(b[0].upper()), _RANKS.index(b[1].upper())
    except ValueError:
        return set()
    suffix = a[2:].lower()
    out = set()
    if a0 == a1 and b0 == b1:
        for i in range(min(a0, b0), max(a0, b0) + 1):
            out.add(_RANKS[i] * 2 + "o")
        return out
    if a0 == b0:  # même première carte : T7s-T3s
        for j in range(min(a1, b1), max(a1, b1) + 1):
            out |= _expand_hand_token(_RANKS[a0] + _RANKS[j] + suffix)
        return out
    if a0 - a1 == b0 - b1:  # connecteurs : 98s-54s
        for k in range(min(a0, b0), max(a0, b0) + 1):
            out |= _expand_hand_token(_RANKS[k] + _RANKS[k - (a0 - a1)] + suffix)
    return out


# --------------------------------------------------------------------------
#  Collection de fonctions
# --------------------------------------------------------------------------
class OHFunction:
    __slots__ = ("name", "text", "path", "line", "tree", "parsed", "is_list",
                 "hand_list", "read_only", "error")

    def __init__(self, name, text, path="", line=0, read_only=False):
        self.name = name
        self.text = text
        self.path = path
        self.line = line
        self.tree = None
        self.parsed = False
        self.is_list = name.startswith("list")
        self.hand_list = None
        self.read_only = read_only
        self.error = None


_HEADER_RE = re.compile(r"^##(.*?)##")


def split_formula_text(text, path="", read_only=False):
    """Découpe un fichier .ohf en objets OHFunction."""
    out = []
    cur_name = None
    cur_lines = []
    cur_line = 0
    for idx, raw in enumerate(text.splitlines(), 1):
        m = _HEADER_RE.match(raw.lstrip("﻿"))
        if m:
            if cur_name is not None:
                out.append(OHFunction(cur_name, "\n".join(cur_lines), path, cur_line, read_only))
            cur_name = m.group(1).strip()
            cur_lines = []
            cur_line = idx
            rest = raw.lstrip("﻿")[m.end():]
            if rest.strip():
                cur_lines.append(rest)
            continue
        if cur_name is not None:
            cur_lines.append(raw)
    if cur_name is not None:
        out.append(OHFunction(cur_name, "\n".join(cur_lines), path, cur_line, read_only))
    return out


MAIN_OPENPPL = ("f$preflop", "f$flop", "f$turn", "f$river")


def default_function_for(name):
    if name in MAIN_OPENPPL:
        return "Default" + name[2:].capitalize()
    return None


class FormulaCollection:
    """Ensemble de fonctions (bibliothèque OpenPPL + profil utilisateur)."""

    def __init__(self):
        self.functions = {}
        self.parse_errors = []
        self.profile_path = None
        self.profile_name = ""

    def add_text(self, text, path="", read_only=False):
        for f in split_formula_text(text, path, read_only):
            self.functions[f.name] = f

    def add_file(self, path, read_only=False):
        with open(path, "r", encoding="latin-1", errors="replace") as fh:
            text = fh.read()
        self.add_text(text, path, read_only)

    def has(self, name):
        return name in self.functions

    def get(self, name):
        return self.functions.get(name)

    # --- analyse
    def parse_all(self):
        for f in self.functions.values():
            self.parse_one(f)

    def parse_one(self, f):
        if f.parsed:
            return
        f.parsed = True
        if f.name[:2].isdigit() or f.name.lower() in ("notes", "dll"):
            return
        try:
            if f.is_list:
                f.hand_list = parse_hand_list(f.text)
            else:
                toks = tokenize(f.text)
                p = Parser(toks, f.name, default_function_for(f.name))
                f.tree = p.parse_function_body()
        except ParseError as e:
            f.error = str(e)
            self.parse_errors.append((f.name, str(e)))
            f.tree = None
        except RecursionError:
            f.error = "Récursion trop profonde"
            self.parse_errors.append((f.name, f.error))

    def is_openppl_profile(self):
        """Un profil est OpenPPL s'il définit f$preflop / f$flop / f$turn / f$river
        avec des conditions WHEN (ou simplement s'il déclare l'une de ces fonctions)."""
        for n in MAIN_OPENPPL:
            f = self.functions.get(n)
            if f is not None and not f.read_only and f.text.strip():
                return True
        return False
