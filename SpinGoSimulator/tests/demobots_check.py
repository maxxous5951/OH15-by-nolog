"""Fait jouer chaque DemoBot d'OpenHoldem (robustesse : aucune exception attendue)."""
import glob, os, random, sys, collections
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from spingosim import symbols as S
from spingosim.config import SimConfig
from spingosim.engine import Game
cfg = SimConfig()
root = S.find_repo_root()
paths = sorted(glob.glob(os.path.join(root, "DemoBots", "**", "*.ohf"), recursive=True))
bad = 0
for p in paths:
    fc = S.get_profile(p, cfg)
    errs = S.profile_errors(fc)
    try:
        unk = collections.Counter(); fe = 0; hands = 0
        for g in range(4):
            rng = random.Random(g)
            brains = [S.make_brain(p, cfg, random.Random(1)), S.make_brain("DefaultBot", cfg, random.Random(2)), S.make_brain("DefaultBot", cfg, random.Random(3))]
            game = Game(cfg, brains, ["x", "y", "z"], g, rng); r = game.play(); hands += r["hands"]
            unk.update(brains[0].ctx.unknown); fe += brains[0].ctx.fc_errors
        print("OK  %-55s %s erreurs-formule=%d runtime=%d hands=%d inconnus=%s" % (os.path.relpath(p, root + "/DemoBots"), "OH-script" if S.is_ohscript(fc) else "OpenPPL", len(errs), fe, hands, sorted(unk)[:5]))
    except Exception as e:
        bad += 1
        import traceback; traceback.print_exc()
        print("KO ", p, type(e).__name__, e)
print("échecs:", bad, "sur", len(paths))
