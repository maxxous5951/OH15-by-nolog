"""Évalue toutes les fonctions de la bibliothèque OpenPPL sur des états de jeu réels et
liste les symboles inconnus rencontrés (doit rester vide hors fautes de frappe de la bibliothèque)."""
import os, random, sys, collections
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from spingosim import symbols as S
from spingosim.config import SimConfig
from spingosim.engine import Game, Decision, Brain, PA_CALL

cfg = SimConfig()


class Probe(S.OHBrain):
    n = 0
    def decide(self, game, chair):
        if not hasattr(self.ctx, "_rounds_seen") or self.ctx.game is not game or self.ctx._hand_no != game.t.hand_no:
            self.ctx.new_hand(game, chair); self.ctx._hand_no = game.t.hand_no
        self.ctx.prepare(game, chair)
        for f in list(self.fc.functions.values()):
            if f.is_list or f.name.startswith("f$debug") or f.error:
                continue
            try:
                self.ctx._fn_cache.pop(f.name, None)
                self.ctx._call_function(f)
            except Exception as e:
                Probe.errs[(f.name, type(e).__name__, str(e)[:60])] += 1
        Probe.n += 1
        return Decision("call", code=PA_CALL)
Probe.errs = collections.Counter()

unk = collections.Counter()
for g in range(int(sys.argv[1]) if len(sys.argv) > 1 else 6):
    rng = random.Random(g)
    brains = []
    for i in range(3):
        b = Probe(S.get_profile("DefaultBot", cfg), cfg, random.Random(i), "p%d" % i)
        b.ctx._hand_no = -1
        brains.append(b)
    Game(cfg, brains, ["a", "b", "c"], g, rng).play() if False else None
    game = Game(cfg, brains, ["a", "b", "c"], g, rng)
    game.cfg.max_hands_per_game = 6
    game.play()
    for b in brains:
        unk.update(b.ctx.unknown)
print("décisions évaluées :", Probe.n)
print("exceptions :", Probe.errs.most_common(10))
print("symboles inconnus :", sorted(unk))
