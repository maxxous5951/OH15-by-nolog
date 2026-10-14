import os
import random
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))

from spingosim import cards as C, symbols as S, ohlang as L  # noqa: E402
from spingosim.config import SimConfig  # noqa: E402
from spingosim.engine import Game, RandomBrain, CallStationBrain  # noqa: E402
from spingosim.payouts import MultiplierTable, payouts  # noqa: E402
from spingosim.runner import run_simulation  # noqa: E402

PROFILES = os.path.join(os.path.dirname(HERE), "profiles")


def cs(s):
    return [C.str_to_card(x) for x in s.split()]


class Cards(unittest.TestCase):
    def test_ranking(self):
        order = ["2c 7d 9h Js 3c", "2c 2d 9h Js 3c", "2c 2d 9h 9s 3c", "2c 2d 2h Js 3c", "4c 5d 6h 7s 8c",
                 "2c 7c 9c Jc 3c", "2c 2d 2h 3s 3c", "2c 2d 2h 2s 3c", "4c 5c 6c 7c 8c"]
        vals = [C.evaluate(cs(h)) for h in order]
        self.assertEqual(vals, sorted(vals))

    def test_wheel(self):
        self.assertEqual(C.hand_type(C.evaluate(cs("Ac 2d 3h 4s 5c 9d Kh"))), C.HT_STRAIGHT)

    def test_equity(self):
        w, t, l = C.equity_vs_random(cs("Ac Ad"), [], 1, 3000, random.Random(1))
        self.assertAlmostEqual(w, 0.85, delta=0.04)


class Payouts(unittest.TestCase):
    def test_split(self):
        self.assertEqual([round(x, 2) for x in payouts(5.0, [0.75, 0.15, 0.10], 3)], [3.75, 0.75, 0.5])

    def test_mean(self):
        self.assertAlmostEqual(MultiplierTable(SimConfig().multipliers).expected(), 2.79, places=2)


class Language(unittest.TestCase):
    def test_library_parses(self):
        fc = S.load_library(SimConfig())
        errs = [e for e in fc.parse_errors if not e[0].startswith("f$debug")]
        self.assertEqual(errs, [])
        self.assertGreater(len(fc.functions), 1000)

    def test_precedence_and_when(self):
        fc = L.FormulaCollection()
        fc.add_text("##f$x##\nWHEN 1 + 2 * 3 = 7 RETURN 5 FORCE\nWHEN Others RETURN 9 FORCE\n")
        fc.parse_all()

        class Ctx:
            def symbol(self, n): return 0.0
            def set_variable(self, n): return 0.0
            def has_function(self, n): return False
        self.assertEqual(fc.get("f$x").tree.ev(Ctx()), 5)


class Engine(unittest.TestCase):
    def test_invariants(self):
        cfg = SimConfig()
        for g in range(150):
            rng = random.Random(g)
            n = 3 if g % 3 else 2
            brains = [RandomBrain(rng) if i % 2 else CallStationBrain() for i in range(n)]
            game = Game(cfg, brains, ["a", "b", "c"][:n], g, rng)
            res = game.play()
            self.assertEqual(sum(s.stack for s in game.t.seats), cfg.starting_stack * n)
            self.assertEqual(sorted(p["place"] for p in res["players"]), list(range(1, n + 1)))
            for h in res["hand_records"]:
                self.assertEqual(sum(s["net"] for s in h["seats"]), 0)

    def test_blind_levels_follow_clock(self):
        cfg = SimConfig(level_seconds=60)
        game = Game(cfg, [CallStationBrain()] * 3, ["a", "b", "c"], 1, random.Random(1))
        self.assertEqual(game.blinds_for_clock(0)[1:], (10, 20))
        self.assertEqual(game.blinds_for_clock(61)[1:], (15, 30))


class Symbols(unittest.TestCase):
    def ctx(self, hole, board):
        cfg = SimConfig()
        fc = S.get_profile("DefaultBot", cfg)
        c = S.SymbolContext(fc, cfg, random.Random(1))
        rng = random.Random(1)
        game = Game(cfg, [CallStationBrain()] * 3, ["a", "b", "c"], 1, rng)
        t = game.t
        t.seats[0].cards = cs(hole)
        t.board = cs(board)
        t.betround = {0: 1, 3: 2, 4: 3, 5: 4}[len(t.board)]
        t.seats[0].in_hand = t.seats[0].dealt = True
        c._bind(game, 0)
        return c

    def test_hand_strength_symbols(self):
        c = self.ctx("Ah Kh", "Qh Jh Th")
        self.assertEqual(c.symbol("isroyalflush"), 1.0)
        self.assertEqual(c.symbol("isstraightflush"), 1.0)
        c = self.ctx("Ah Ad", "As 7c 2d")
        self.assertEqual(c.symbol("isthreeofakind"), 1.0)
        self.assertEqual(c.symbol("nranked"), 3.0)
        self.assertEqual(c.symbol("$AA"), 1.0)
        self.assertEqual(c.symbol("hand$AA"), 1.0)
        self.assertEqual(c.symbol("board$A"), 1.0)
        self.assertEqual(int(c.symbol("pokerval")) & 0xff000000, S.PV_TRIPS)

    def test_pokerval_pair(self):
        c = self.ctx("Kh Kd", "7s 4c 2d")
        pv = int(c.symbol("pokerval"))
        self.assertEqual(pv & 0xfffff000, S.PV_ONEPAIR + (13 << 16) + (13 << 12))

    def test_handrank(self):
        c = self.ctx("Ah Ad", "")
        self.assertEqual(c.symbol("handrank169"), 1.0)


class EndToEnd(unittest.TestCase):
    def test_run_with_profiles(self):
        with tempfile.TemporaryDirectory() as d:
            cfg = SimConfig(num_games=12, output_dir=d, workers=2, seed=7)
            cfg.players = [{"name": "Nit", "path": os.path.join(PROFILES, "Tight_Nit.ohf")},
                           {"name": "LAG", "path": os.path.join(PROFILES, "Aggro_LAG.ohf")},
                           {"name": "Def", "path": "DefaultBot"}]
            pr = run_simulation(cfg)
            self.assertEqual(pr.done, 12)
            self.assertTrue(os.path.exists(os.path.join(d, "handhistory_global.txt")))
            self.assertTrue(os.path.exists(os.path.join(d, "players", "Nit_hands.txt")))
            with open(os.path.join(d, "handhistory_global.txt"), encoding="utf-8") as fh:
                txt = fh.read()
            self.assertIn("Dealt to Nit", txt)
            self.assertIn("Dealt to LAG", txt)
            self.assertEqual(sum(r["games"] for r in pr.rows), 36)

    def test_reproducible(self):
        res = []
        for _ in range(2):
            with tempfile.TemporaryDirectory() as d:
                cfg = SimConfig(num_games=6, output_dir=d, workers=1, seed=3)
                cfg.players = [{"name": "A", "path": os.path.join(PROFILES, "Tight_Nit.ohf")},
                               {"name": "B", "path": os.path.join(PROFILES, "CallingStation.ohf")},
                               {"name": "C", "path": os.path.join(PROFILES, "PushFold_Shorty.ohf")}]
                res.append([r["net_eur"] for r in run_simulation(cfg).rows])
        self.assertEqual(res[0], res[1])


if __name__ == "__main__":
    unittest.main()
