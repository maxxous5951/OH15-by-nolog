"""Statistiques de joueurs calculées à partir des résultats de parties."""
from collections import defaultdict


class PlayerStats:
    def __init__(self, name):
        self.name = name
        self.games = 0
        self.places = [0, 0, 0]
        self.gain = 0.0
        self.net = 0.0
        self.itm = 0
        self.mult_sum = 0.0
        self.hands = 0
        self.vpip = self.pfr = self.three_bet = self.three_bet_opp = 0
        self.flop_seen = self.sd = self.sd_won = 0
        self.agg = self.calls_post = 0
        self.allin_hands = 0
        self.chips_net = 0.0
        self.bb_net = 0.0
        self.cbet = self.cbet_opp = 0
        self.fold_to_3bet = self.faced_3bet = 0
        self.steal = self.steal_opp = 0
        self.pf_fold = 0
        self.hands_by_level = defaultdict(int)

    def row(self, buyin=1.0):
        g = max(self.games, 1)
        h = max(self.hands, 1)
        pct = lambda a, b: round(100.0 * a / b, 1) if b else 0.0
        return {
            "name": self.name, "games": self.games, "hands": self.hands,
            "1st": self.places[0], "2nd": self.places[1], "3rd": self.places[2],
            "win%": pct(self.places[0], g), "itm%": pct(self.itm, g),
            "avg_place": round(sum((i + 1) * n for i, n in enumerate(self.places)) / float(g), 2),
            "net_eur": round(self.net, 2), "roi%": pct(self.net, g * buyin) if buyin else 0.0,
            "avg_gain": round(self.gain / g, 3), "avg_mult_played": round(self.mult_sum / g, 2),
            "vpip%": pct(self.vpip, h), "pfr%": pct(self.pfr, h),
            "3bet%": pct(self.three_bet, self.three_bet_opp),
            "fold_to_3bet%": pct(self.fold_to_3bet, self.faced_3bet),
            "steal%": pct(self.steal, self.steal_opp),
            "AF": round(self.agg / float(self.calls_post), 2) if self.calls_post else (float(self.agg) if self.agg else 0.0),
            "cbet%": pct(self.cbet, self.cbet_opp),
            "WTSD%": pct(self.sd, self.flop_seen), "W$SD%": pct(self.sd_won, self.sd),
            "allin%": pct(self.allin_hands, h),
            "bb/100": round(100.0 * self.bb_net / h, 1),
        }


def update_from_game(stats, res, buyin, paid_places=3):
    """stats: dict name -> PlayerStats (clé = profile_id)."""
    for p in res["players"]:
        st = stats[p["profile_id"]]
        st.games += 1
        if 1 <= p["place"] <= 3:
            st.places[p["place"] - 1] += 1
        st.gain += p["gain"]
        st.net += p["net"]
        st.mult_sum += res["multiplier"]
        if p["gain"] > 0:
            st.itm += 1
    pid_of = {p["chair"]: p["profile_id"] for p in res["players"]}
    for h in res["hand_records"]:
        bb = float(h["bb"])
        acts = h["actions"]
        pre = [a for a in acts if a["r"] == 1 and a["k"] not in ("sb", "bb")]
        raises_pre = [a for a in pre if a["k"] == "raise"]
        first_raiser = raises_pre[0]["c"] if raises_pre else None
        three_bet_actor = raises_pre[1]["c"] if len(raises_pre) > 1 else None
        n = len(h["seats"])
        for s in h["seats"]:
            ch = s["chair"]
            st = stats[pid_of[ch]]
            st.hands += 1
            st.chips_net += s["net"]
            st.bb_net += s["net"] / bb
            mine = [a for a in pre if a["c"] == ch]
            if any(a["k"] in ("call", "raise") for a in mine):
                st.vpip += 1
            if any(a["k"] == "raise" for a in mine):
                st.pfr += 1
            # 3-bet : relance alors qu'une relance précède
            seen_raise = False
            opp = False
            did3 = False
            for a in pre:
                if a["c"] == ch:
                    if seen_raise and not opp:
                        opp = True
                        if a["k"] == "raise":
                            did3 = True
                if a["k"] == "raise":
                    seen_raise = True
            if opp:
                st.three_bet_opp += 1
            if did3:
                st.three_bet += 1
            # fold to 3bet : j'ai ouvert et un 3-bet suit mon open
            if first_raiser == ch and three_bet_actor is not None and three_bet_actor != ch:
                st.faced_3bet += 1
                after = False
                for a in pre:
                    if a["k"] == "raise" and a["c"] == three_bet_actor:
                        after = True
                    elif after and a["c"] == ch:
                        if a["k"] == "fold":
                            st.fold_to_3bet += 1
                        break
            # steal : ouverture au bouton/SB en 3-max sans action avant
            if n >= 3 and ch in (h["dealer"], h["sb_chair"]):
                before = []
                for a in pre:
                    if a["c"] == ch:
                        break
                    before.append(a)
                if not any(a["k"] in ("call", "raise") for a in before):
                    st.steal_opp += 1
                    if mine and mine[0]["k"] == "raise":
                        st.steal += 1
            post = [a for a in acts if a["r"] > 1 and a["c"] == ch]
            st.agg += sum(1 for a in post if a["k"] in ("bet", "raise"))
            st.calls_post += sum(1 for a in post if a["k"] == "call")
            folded_pre = any(a["k"] == "fold" and a["c"] == ch and a["r"] == 1 for a in acts)
            if len(h["board"]) >= 3 and not folded_pre:
                st.flop_seen += 1
                if h["showdown"] and not s["folded"]:
                    st.sd += 1
                    if s["collected"] > 0:
                        st.sd_won += 1
            if any(a["allin"] for a in acts if a["c"] == ch and a["k"] not in ("sb", "bb")):
                st.allin_hands += 1
            # c-bet : dernier relanceur préflop mise le premier au flop
            if raises_pre and raises_pre[-1]["c"] == ch:
                fl = [a for a in acts if a["r"] == 2]
                if fl and any(a["c"] == ch for a in fl):
                    first_mine = [a for a in fl if a["c"] == ch][0]
                    if first_mine is fl[0] or not any(a["k"] in ("bet", "raise") for a in fl[:fl.index(first_mine)]):
                        st.cbet_opp += 1
                        if first_mine["k"] in ("bet", "raise"):
                            st.cbet += 1
