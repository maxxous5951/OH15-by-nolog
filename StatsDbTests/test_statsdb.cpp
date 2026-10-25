// Tests of the OpenHoldem stats database (StatsCore.h / StatsDb.cpp).
// Build and run: see build_and_run.sh
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <vector>
#include <unistd.h>

#include "StatsDb.h"

using namespace ohstats;

static int g_checks = 0, g_failures = 0;
#define CHECK(cond) do { ++g_checks; if (!(cond)) { ++g_failures; \
  printf("  FAILED %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define CHECK_NEAR(a, b) do { ++g_checks; double _a = (a), _b = (b); \
  if (_a < _b - 1e-9 || _a > _b + 1e-9) { ++g_failures; \
  printf("  FAILED %s:%d: %s = %f, expected %f\n", __FILE__, __LINE__, #a, _a, _b); } } while (0)

//------------------------------------------------------------------------------
// Test hands: 6 seats. 0 = BTN, 1 = SB, 2 = BB, 3 = UTG, 4 = MP, 5 = CO
//------------------------------------------------------------------------------

struct HandBuilder {
  hh::Hand h;
  HandBuilder(const std::string &id, long long when = 1000000) {
    h.hand_id = id;
    h.start_time = when;
    h.nseats = 6; h.dealer = 0; h.sb_seat = 1; h.bb_seat = 2;
    h.sblind = 0.5; h.bblind = 1;
    for (int i = 0; i < 6; ++i) {
      h.in_hand[i] = true;
      char name[16];
      snprintf(name, sizeof(name), "P%d", i);
      h.name[i] = name;
      h.start_stack[i] = h.end_stack[i] = 100;
    }
    Act(1, hh::kPostSmallBlind, 1);
    Act(2, hh::kPostBigBlind, 1);
  }
  HandBuilder &Act(int seat, hh::ActionType type, int street) {
    hh::Action a;
    a.seat = seat; a.type = type; a.street = street;
    h.actions.push_back(a);
    if (type == hh::kFold) h.folded[seat] = true;
    return *this;
  }
  HandBuilder &Board(int cards) { h.board_count = cards; return *this; }
};

static int Count(const std::vector<PlayerStats> &players, int seat, int counter) {
  for (size_t i = 0; i < players.size(); ++i) {
    if (players[i].seat == seat) return players[i].counters[counter];
  }
  return -999;
}
#define C(seat, name) Count(p, seat, c_##name)

static void TestPositions() {
  printf("positions\n");
  HandBuilder b("pos");
  std::vector<int> pos = ComputePositions(b.h);
  CHECK(pos[0] == kPositionBTN);
  CHECK(pos[1] == kPositionSB);
  CHECK(pos[2] == kPositionBB);
  CHECK(pos[3] == kPositionEP);
  CHECK(pos[4] == kPositionEP);
  CHECK(pos[5] == kPositionCO);
  // Heads-up: the button is the small blind, button wins like in the PT-DLL
  HandBuilder hu("hu");
  for (int i = 2; i < 6; ++i) hu.h.in_hand[i] = false;
  hu.h.dealer = 1; hu.h.sb_seat = 1; hu.h.bb_seat = 0;
  pos = ComputePositions(hu.h);
  CHECK(pos[1] == kPositionBTN);
  CHECK(pos[0] == kPositionBB);
  CHECK(pos[2] == kPositionUnknown);
}

static void TestStealAndCbet() {
  printf("steal + continuation bet\n");
  HandBuilder b("steal");
  b.Act(3, hh::kFold, 1).Act(4, hh::kFold, 1)
   .Act(5, hh::kRaise, 1)                       // CO steals
   .Act(0, hh::kFold, 1).Act(1, hh::kFold, 1)
   .Act(2, hh::kCall, 1)                        // BB defends
   .Board(3)
   .Act(2, hh::kCheck, 2).Act(5, hh::kBet, 2)   // cbet
   .Act(2, hh::kFold, 2);
  std::vector<PlayerStats> p = Analyze(b.h);
  CHECK(p.size() == 6);
  // CO
  CHECK(C(5, vpip) == 1 && C(5, pfr) == 1 && C(5, pfr_opp) == 1);
  CHECK(C(5, rfi_opp) == 1 && C(5, rfi) == 1);
  CHECK(C(5, steal_opp) == 1 && C(5, steal) == 1);
  CHECK(C(5, flop_cbet_opp) == 1 && C(5, flop_cbet) == 1);
  CHECK(C(5, flop_br) == 1);
  CHECK(C(5, seen_flop) == 1 && C(5, seen_turn) == 0);
  // BB
  CHECK(C(2, vpip) == 1 && C(2, pfr) == 0);
  CHECK(C(2, called_raise_opp) == 1 && C(2, called_raise) == 1);
  CHECK(C(2, three_bet_opp) == 1 && C(2, three_bet) == 0);
  CHECK(C(2, bb_steal_opp) == 1 && C(2, bb_fold_steal) == 0);
  CHECK(C(2, bb_3bet_steal_opp) == 1 && C(2, bb_3bet_steal) == 0);
  CHECK(C(2, flop_cbet_faced) == 1 && C(2, flop_fold_cbet) == 1 && C(2, flop_raise_cbet) == 0);
  CHECK(C(2, flop_check) == 1 && C(2, flop_fold) == 1);
  CHECK(C(2, seen_flop) == 1 && C(2, seen_turn) == 0);
  // SB folds to the steal
  CHECK(C(1, sb_steal_opp) == 1 && C(1, sb_fold_steal) == 1);
  CHECK(C(1, vpip) == 0 && C(1, seen_flop) == 0);
  // UTG: first in, folds
  CHECK(C(3, rfi_opp) == 1 && C(3, rfi) == 0 && C(3, pfr_opp) == 1);
  // BTN: opportunity to 3bet, folds
  CHECK(C(0, three_bet_opp) == 1 && C(0, three_bet) == 0);
  CHECK(C(0, rfi_opp) == 0);   // there was a raise before
}

static void TestThreeBetFourBetSqueeze() {
  printf("3bet / 4bet / fold to 3bet / squeeze\n");
  HandBuilder b("3bet");
  b.Act(3, hh::kRaise, 1)                        // UTG opens
   .Act(4, hh::kRaise, 1)                        // MP 3bets
   .Act(5, hh::kFold, 1).Act(0, hh::kFold, 1).Act(1, hh::kFold, 1).Act(2, hh::kFold, 1)
   .Act(3, hh::kFold, 1);                        // UTG folds to the 3bet
  std::vector<PlayerStats> p = Analyze(b.h);
  CHECK(C(3, rfi) == 1 && C(3, steal_opp) == 0);       // UTG is no steal position
  CHECK(C(3, fold_3bet_opp) == 1 && C(3, fold_3bet) == 1);
  CHECK(C(3, fold_resteal_opp) == 0);
  CHECK(C(4, three_bet_opp) == 1 && C(4, three_bet) == 1);
  CHECK(C(4, four_bet_opp) == 0);
  CHECK(C(5, four_bet_opp) == 1 && C(5, four_bet) == 0);   // CO could 4bet
  CHECK(C(5, three_bet_opp) == 0);

  HandBuilder s("squeeze");
  s.Act(3, hh::kRaise, 1).Act(4, hh::kCall, 1)
   .Act(5, hh::kRaise, 1)                        // CO squeezes
   .Act(0, hh::kFold, 1).Act(1, hh::kFold, 1).Act(2, hh::kFold, 1)
   .Act(3, hh::kFold, 1).Act(4, hh::kFold, 1);
  p = Analyze(s.h);
  CHECK(C(5, squeeze_opp) == 1 && C(5, squeeze) == 1 && C(5, three_bet) == 1);
  CHECK(C(4, called_raise) == 1);

  // Steal from the CO, BTN 3bets (3bet vs steal), CO folds (fold to resteal)
  HandBuilder r("resteal");
  r.Act(3, hh::kFold, 1).Act(4, hh::kFold, 1).Act(5, hh::kRaise, 1)
   .Act(0, hh::kRaise, 1).Act(1, hh::kFold, 1).Act(2, hh::kFold, 1)
   .Act(5, hh::kFold, 1);
  p = Analyze(r.h);
  CHECK(C(0, three_bet_steal_opp) == 1 && C(0, three_bet_steal) == 1);
  CHECK(C(5, fold_resteal_opp) == 1 && C(5, fold_resteal) == 1 && C(5, fold_3bet) == 1);
  // The BB faces a steal AND a 3bet (squeeze), which is no "steal defence" spot
  CHECK(C(2, bb_steal_opp) == 0 && C(2, bb_fold_steal) == 0);
  CHECK(C(2, three_bet_opp) == 0 && C(2, four_bet_opp) == 1);
}

static void TestPostflop() {
  printf("postflop: check-raise, donk, turn cbet, aggression, showdown\n");
  // Heads-up pot between CO (aggressor) and BB
  HandBuilder b("post");
  b.Act(3, hh::kFold, 1).Act(4, hh::kFold, 1).Act(5, hh::kRaise, 1)
   .Act(0, hh::kFold, 1).Act(1, hh::kFold, 1).Act(2, hh::kCall, 1)
   .Board(5)
   // flop: BB checks, CO cbets, BB check-raises, CO calls
   .Act(2, hh::kCheck, 2).Act(5, hh::kBet, 2).Act(2, hh::kRaise, 2).Act(5, hh::kCall, 2)
   // turn: BB checks, CO bets (second barrel), BB calls
   .Act(2, hh::kCheck, 3).Act(5, hh::kBet, 3).Act(2, hh::kCall, 3)
   // river: both check
   .Act(2, hh::kCheck, 4).Act(5, hh::kCheck, 4);
  b.h.showdown = true;
  b.h.collected[2] = 20;
  std::vector<PlayerStats> p = Analyze(b.h);
  CHECK(C(2, flop_cr_opp) == 1 && C(2, flop_cr) == 1);
  CHECK(C(2, flop_cbet_faced) == 1 && C(2, flop_raise_cbet) == 1 && C(2, flop_fold_cbet) == 0);
  CHECK(C(2, flop_donk_opp) == 1 && C(2, flop_donk) == 0);   // BB could donk, but checked
  CHECK(C(5, turn_cbet_opp) == 1 && C(5, turn_cbet) == 1);
  CHECK(C(2, turn_cbet_faced) == 1 && C(2, turn_fold_cbet) == 0);
  CHECK(C(2, turn_cr_opp) == 1 && C(2, turn_cc) == 1 && C(2, turn_cr) == 0);   // check, bet, call
  CHECK(C(5, river_bet_opp) == 1 && C(5, river_bet) == 0);
  CHECK(C(2, river_bet_opp) == 1 && C(2, river_bet) == 0);
  CHECK(C(2, flop_br) == 1 && C(2, flop_check) == 1 && C(2, turn_call) == 1);
  CHECK(C(5, flop_call) == 1 && C(5, flop_br) == 1);
  CHECK(C(2, seen_river) == 1 && C(5, seen_river) == 1);
  CHECK(C(2, wtsd) == 1 && C(2, wssd) == 1);
  CHECK(C(5, wtsd) == 1 && C(5, wssd) == 0);
  CHECK(C(0, wtsd) == 0 && C(0, seen_flop) == 0);

  // Donk bet: BB bets into the aggressor
  HandBuilder d("donk");
  d.Act(3, hh::kFold, 1).Act(4, hh::kFold, 1).Act(5, hh::kRaise, 1)
   .Act(0, hh::kFold, 1).Act(1, hh::kFold, 1).Act(2, hh::kCall, 1)
   .Board(3)
   .Act(2, hh::kBet, 2).Act(5, hh::kFold, 2);
  p = Analyze(d.h);
  CHECK(C(2, flop_donk_opp) == 1 && C(2, flop_donk) == 1);
  CHECK(C(5, flop_cbet_opp) == 0);   // the aggressor never had the chance
  CHECK(C(5, flop_fold) == 1);
  CHECK(C(2, flop_cbet_faced) == 0);

  // Turn check-call and "no cbet chain": flop checked through, bet on the turn
  HandBuilder t("turn");
  t.Act(3, hh::kFold, 1).Act(4, hh::kFold, 1).Act(5, hh::kRaise, 1)
   .Act(0, hh::kFold, 1).Act(1, hh::kFold, 1).Act(2, hh::kCall, 1)
   .Board(4)
   .Act(2, hh::kCheck, 2).Act(5, hh::kCheck, 2)
   .Act(2, hh::kCheck, 3).Act(5, hh::kBet, 3).Act(2, hh::kCall, 3);
  p = Analyze(t.h);
  CHECK(C(5, flop_cbet_opp) == 1 && C(5, flop_cbet) == 0);
  CHECK(C(5, turn_cbet_opp) == 0);                 // no flop cbet -> no turn cbet
  CHECK(C(2, turn_cbet_faced) == 0);
  CHECK(C(2, turn_cr_opp) == 1 && C(2, turn_cc) == 1 && C(2, turn_cr) == 0);
}

//------------------------------------------------------------------------------
// Database
//------------------------------------------------------------------------------

static std::string TempPath(const char *name) {
  char buffer[256];
  snprintf(buffer, sizeof(buffer), "/tmp/claude-0/statsdb_test_%d_%s", (int)getpid(), name);
  return buffer;
}

static void RemoveDb(const std::string &path) {
  unlink(path.c_str());
  unlink((path + "-wal").c_str());
  unlink((path + "-shm").c_str());
}

// "Villain" (seat 5) opens from the CO and cbets; BB folds. 10 hands, 4 of them
// with a raise of Villain.
static void AddVillainHand(Db &db, int index, bool raise, long long when,
                           const std::string &site, bool tournament) {
  char id[32];
  snprintf(id, sizeof(id), "%d", index);
  HandBuilder b(id, when);
  b.h.tournament = tournament;
  b.h.name[5] = "Villain";
  b.Act(3, hh::kFold, 1).Act(4, hh::kFold, 1);
  if (raise) {
    b.Act(5, hh::kRaise, 1).Act(0, hh::kFold, 1).Act(1, hh::kFold, 1).Act(2, hh::kFold, 1);
  } else {
    b.Act(5, hh::kFold, 1).Act(0, hh::kFold, 1).Act(1, hh::kFold, 1).Act(2, hh::kCheck, 1);
  }
  std::string error;
  bool ok = db.InsertHand(b.h, site, &error);
  CHECK(ok);
  if (!ok) printf("    %s\n", error.c_str());
}

static void TestDatabase() {
  printf("database\n");
  std::string path = TempPath("a.db");
  RemoveDb(path);
  Db db;
  std::string error;
  CHECK(db.Open(path, &error));
  CHECK(db.IsOpen());
  CHECK(db.CountHands() == 0);
  CHECK(db.SelfTest(&error));
  if (!error.empty()) printf("    %s\n", error.c_str());

  // Duplicates are rejected
  HandBuilder first("dup");
  CHECK(db.InsertHand(first.h, "stars"));
  CHECK(!db.InsertHand(first.h, "stars"));
  CHECK(db.CountHands() == 1);
  CHECK(db.InsertHand(first.h, "party"));        // same ID at another site is fine
  CHECK(db.CountHands() == 2);

  // 10 hands of Villain at "stars" (cash), the raises in hands 2, 5, 7, 9
  for (int i = 0; i < 10; ++i) {
    bool raise = (i == 2 || i == 5 || i == 7 || i == 9);
    AddVillainHand(db, 100 + i, raise, 2000000 + i * 100, "stars", false);
  }
  // 2 tournament hands that must not count for cash stats, 3 hands at another site
  AddVillainHand(db, 200, true, 2000000, "stars", true);
  AddVillainHand(db, 201, true, 2000100, "stars", true);
  for (int i = 0; i < 3; ++i) AddVillainHand(db, 300 + i, true, 2000000 + i, "party", false);

  StatFilter f;
  f.site = "stars";
  double v = -1;
  CHECK(db.GetStat("hands", "Villain", f, &v));
  CHECK_NEAR(v, 10);
  CHECK(db.GetStat("vpip", "villain", f, &v));            // name: ignoring case
  CHECK_NEAR(v, 0.4);
  CHECK(db.GetStat("pfr", "Villain", f, &v));
  CHECK_NEAR(v, 0.4);                                     // 4 raises / 10 chances
  CHECK(db.GetStat("rfi", "Villain", f, &v));
  CHECK_NEAR(v, 0.4);
  CHECK(db.GetStat("steal_attempt", "Villain", f, &v));
  CHECK_NEAR(v, 0.4);

  // Without a site filter the other site counts as well
  StatFilter any;
  CHECK(db.GetStat("hands", "Villain", any, &v));
  CHECK_NEAR(v, 13);
  // Tournament
  StatFilter t = f;
  t.tournament = true;
  CHECK(db.GetStat("hands", "Villain", t, &v));
  CHECK_NEAR(v, 2);
  CHECK(db.GetStat("vpip", "Villain", t, &v));
  CHECK_NEAR(v, 1.0);
  // Last N hands: the 3 newest hands are index 7, 8, 9 -> 2 raises
  StatFilter last = f;
  last.nb_hands = 3;
  CHECK(db.GetStat("vpip", "Villain", last, &v));
  CHECK_NEAR(v, 2.0 / 3.0);
  // Since: hands 5..9 (played_at >= 2000500) -> raises at 5, 7, 9
  StatFilter since = f;
  since.since = 2000500;
  CHECK(db.GetStat("hands", "Villain", since, &v));
  CHECK_NEAR(v, 5);
  CHECK(db.GetStat("vpip", "Villain", since, &v));
  CHECK_NEAR(v, 0.6);
  // Position: always CO
  StatFilter co = f;
  co.position = kPositionCO;
  CHECK(db.GetStat("hands", "Villain", co, &v));
  CHECK_NEAR(v, 10);
  StatFilter btn = f;
  btn.position = kPositionBTN;
  CHECK(!db.GetStat("vpip", "Villain", btn, &v));       // never played from the button
  // Undefined: nobody ever 4bet him
  CHECK(!db.GetStat("4bet", "Villain", f, &v));
  CHECK(!db.GetStat("flop_cbet", "Villain", f, &v));
  // Unknown stat / player
  CHECK(!db.GetStat("no_such_stat", "Villain", f, &v));
  CHECK(!db.GetStat("vpip", "Nobody", f, &v));
  CHECK(Db::IsSupportedStat("3bet") && !Db::IsSupportedStat("icon"));
  CHECK(Db::SupportedStats().size() > 40);

  // Names
  CHECK(db.ResolveName("Villain", f) == "Villain");
  CHECK(db.ResolveName("VILLAIN", f) == "Villain");
  CHECK(db.ResolveName("Vi1lain", f) == "Villain");       // one wrong character
  CHECK(db.ResolveName("Vilain", f) == "Villain");        // one lost character
  CHECK(db.ResolveName("Xyzabc", f) == "");
  CHECK(db.ResolveName("P2", f) == "P2");                 // exact
  CHECK(db.ResolveName("P9", f) == "");                   // too short for fuzzy matching
  CHECK(db.ResolveName("", f) == "");

  // The file survives re-opening
  db.Close();
  CHECK(!db.IsOpen());
  CHECK(db.Open(path, &error));
  CHECK(db.CountHands() == 2 + 10 + 2 + 3);
  CHECK(db.GetStat("vpip", "Villain", f, &v));
  CHECK_NEAR(v, 0.4);
  // Hands without hand-ID are rejected
  hh::Hand bad;
  CHECK(!db.InsertHand(bad, "stars"));
  db.Close();
  RemoveDb(path);
}

static void TestHelpers() {
  printf("helpers\n");
  long long s = 0;
  CHECK(ParsePeriodSeconds("12months", &s) && s == 12LL * 30 * 86400);
  CHECK(ParsePeriodSeconds("1year", &s) && s == 365LL * 86400);
  CHECK(ParsePeriodSeconds("2days", &s) && s == 2 * 86400);
  CHECK(ParsePeriodSeconds("90minutes", &s) && s == 5400);
  CHECK(ParsePeriodSeconds("3Weeks", &s) && s == 3LL * 7 * 86400);
  CHECK(!ParsePeriodSeconds("months", &s));
  CHECK(!ParsePeriodSeconds("12", &s));
  CHECK(!ParsePeriodSeconds("12parsecs", &s));
  CHECK(!ParsePeriodSeconds("", &s));
  CHECK(EditDistance("abc", "abc") == 0);
  CHECK(EditDistance("abc", "ABC") == 0);
  CHECK(EditDistance("abc", "abd") == 1);
  CHECK(EditDistance("abc", "ac") == 1);
  CHECK(EditDistance("", "abc") == 3);
  CHECK(EditDistance("kitten", "sitting") == 3);
}

static void TestStatNamesMatchPokerTracker(const char *header_path) {
  // Every statistic that we offer has to exist in the PokerTracker definitions,
  // otherwise the pt_-symbol would be rejected as invalid before we are asked.
  printf("stat names exist in the PokerTracker definitions\n");
  FILE *f = fopen(header_path, "rb");
  if (!f) { printf("  (skipped, %s not found)\n", header_path); return; }
  std::string text;
  char buffer[4096];
  size_t n;
  while ((n = fread(buffer, 1, sizeof(buffer), f)) > 0) text.append(buffer, n);
  fclose(f);
  std::vector<std::string> names = Db::SupportedStats();
  for (size_t i = 0; i < names.size(); ++i) {
    std::string quoted = "\"" + names[i] + "\"";
    bool found = text.find("// name\n\t\t" + quoted) != std::string::npos ||
                 text.find("// name\r\n\t\t" + quoted) != std::string::npos;
    CHECK(found);
    if (!found) printf("    missing: %s\n", names[i].c_str());
  }
}

int main(int argc, char **argv) {
  TestHelpers();
  TestPositions();
  TestStealAndCbet();
  TestThreeBetFourBetSqueeze();
  TestPostflop();
  TestDatabase();
  if (argc > 1) TestStatNamesMatchPokerTracker(argv[1]);
  printf("\n%d checks, %d failures\n", g_checks, g_failures);
  return g_failures ? 1 : 0;
}
