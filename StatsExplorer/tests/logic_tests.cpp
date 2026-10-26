// Tests of the logic of the explorer without GUI: equity, reports, importer.
// usage: logic_tests <output-dir>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <algorithm>
#include <string>
#include <vector>

#include "demo_data.h"
#include "equity.h"
#include "explorer_model.h"
#include "hh_import.h"
#include "HandHistoryFormat.h"
#include "reports.h"

using namespace explorer;

static int g_checks = 0, g_failures = 0;
#define CHECK(cond) do { ++g_checks; if (!(cond)) { ++g_failures; \
  printf("  FAILED %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define NEAR(a, b, tol) do { ++g_checks; double _a = (a), _b = (b); if (!(fabs(_a - _b) <= (tol))) { ++g_failures; \
  printf("  FAILED %s:%d: %s = %g, expected %g +- %g\n", __FILE__, __LINE__, #a, _a, _b, (double)(tol)); } } while (0)

static std::vector<std::string> V(const char *a = 0, const char *b = 0, const char *c = 0, const char *d = 0,
                                  const char *e = 0, const char *f = 0, const char *g = 0, const char *h = 0) {
  std::vector<std::string> v;
  const char *all[8] = { a, b, c, d, e, f, g, h };
  for (int i = 0; i < 8; ++i) if (all[i]) v.push_back(all[i]);
  return v;
}

static void TestEquity() {
  printf("equity\n");
  std::vector<double> eq; bool exact = false;
  // ranking of hands
  int sf = EvaluateCards(V("9h", "8h", "7h", "6h", "5h"));
  int quads = EvaluateCards(V("Ks", "Kh", "Kd", "Kc", "2s"));
  int full = EvaluateCards(V("Ks", "Kh", "Kd", "2c", "2s"));
  int flush = EvaluateCards(V("As", "9s", "7s", "4s", "2s"));
  int straight = EvaluateCards(V("As", "2h", "3d", "4c", "5s"));
  int trips = EvaluateCards(V("Qs", "Qh", "Qd", "4c", "5s"));
  int two = EvaluateCards(V("Qs", "Qh", "4d", "4c", "5s"));
  int pair = EvaluateCards(V("Qs", "Qh", "3d", "4c", "5s"));
  int high = EvaluateCards(V("As", "Qh", "3d", "4c", "7s"));
  CHECK(sf > quads && quads > full && full > flush && flush > straight && straight > trips);
  CHECK(trips > two && two > pair && pair > high && high > 0);
  CHECK(EvaluateCards(V("6s", "2h", "3d", "4c", "5s")) > straight);          // six high beats the wheel
  CHECK(EvaluateCards(V("Ks", "Kh", "Kd", "2c", "2s", "2h")) == EvaluateCards(V("Ks", "Kh", "Kd", "2c", "2s")));
  CHECK(EvaluateCards(V("Ah", "Kd")) == -1 && EvaluateCards(V("Ah", "Kd", "Xx", "2c", "3s")) == -1);
  CHECK(EvaluateCards(V("As", "Ah", "Kd", "Kc", "Qs")) > EvaluateCards(V("As", "Ah", "Kd", "Qc", "Js")));
  // kickers
  CHECK(EvaluateCards(V("As", "Ah", "Kd", "7c", "2s")) > EvaluateCards(V("As", "Ah", "Qd", "7c", "2s")));

  std::vector<PlayerCards> two_players;
  two_players.push_back(PlayerCards("As", "Ah"));
  two_players.push_back(PlayerCards("Ks", "Kh"));
  CHECK(ComputeEquity(two_players, V(), 40000, &eq, &exact));
  NEAR(eq[0], 0.82, 0.015); NEAR(eq[0] + eq[1], 1.0, 1e-9); CHECK(!exact);
  two_players[0] = PlayerCards("As", "Ks");
  two_players[1] = PlayerCards("Qh", "Qd");
  CHECK(ComputeEquity(two_players, V(), 40000, &eq));
  NEAR(eq[0], 0.46, 0.015);
  // river: exact, tie
  two_players[0] = PlayerCards("As", "Kh");
  two_players[1] = PlayerCards("Ad", "Kc");
  CHECK(ComputeEquity(two_players, V("2c", "7d", "9h", "Jh", "Qs"), 100, &eq, &exact));
  NEAR(eq[0], 0.5, 1e-9); CHECK(exact);
  // flop, exact: set vs overpair
  two_players[0] = PlayerCards("7s", "7h");
  two_players[1] = PlayerCards("Ks", "Kh");
  CHECK(ComputeEquity(two_players, V("7d", "Kd", "2c"), 100, &eq, &exact));
  CHECK(exact);
  NEAR(eq[0] + eq[1], 1.0, 1e-9);
  // unknown opponent
  std::vector<PlayerCards> vs_random;
  vs_random.push_back(PlayerCards("As", "Ah"));
  vs_random.push_back(PlayerCards());
  CHECK(ComputeEquity(vs_random, V(), 40000, &eq, &exact));
  NEAR(eq[0], 0.852, 0.015);
  // nuts: 100 %
  two_players[0] = PlayerCards("As", "Ks");
  two_players[1] = PlayerCards("2d", "3d");
  CHECK(ComputeEquity(two_players, V("Qs", "Js", "Ts", "4h", "9c"), 100, &eq) && eq[0] == 1.0);
  // invalid input
  two_players[1] = PlayerCards("As", "3d");
  CHECK(!ComputeEquity(two_players, V(), 100, &eq));           // card twice
  two_players[1] = PlayerCards("2d", "3d");
  CHECK(!ComputeEquity(two_players, V("2d"), 100, &eq));       // card also on the board
  NEAR(PotOdds(10, 5), 1.0 / 3.0, 1e-9);
  CHECK(PotOdds(10, 0) == 0);
}

static ohstats::Db *g_db = NULL;

static void TestReports() {
  printf("reports\n");
  Scope hero;                       // the hero, cash games
  Graph g;
  std::string error;
  CHECK(BuildGraph(g_db, hero, &g, &error));
  CHECK(g.hands > 1000 && g.points.size() == (size_t)g.hands);
  NEAR(g.points.back().total, g.net_bb, 1e-6);
  NEAR(g.points.back().showdown + g.points.back().no_showdown, g.net_bb, 1e-6);
  NEAR(g.bb_per_100, g.net_bb / g.hands * 100, 1e-9);
  CHECK(g.std_dev_bb > 1 && g.WinrateMargin() > 0);
  for (size_t i = 1; i < g.points.size(); ++i) if (g.points[i].time < g.points[i - 1].time) { CHECK(false); break; }
  Graph maniac;
  Scope s_maniac; s_maniac.player = "Maniac";
  CHECK(BuildGraph(g_db, s_maniac, &maniac, &error) && maniac.hands > 1000);

  const ReportKind kinds[] = { kReportPosition, kReportStakes, kReportTable, kReportHour, kReportWeekday,
                               kReportStartingHand, kReportPotType, kReportStackDepth };
  for (size_t k = 0; k < sizeof(kinds) / sizeof(kinds[0]); ++k) {
    std::vector<ReportRow> rows;
    CHECK(BuildReport(g_db, hero, kinds[k], &rows, &error));
    long long hands = 0; double net = 0;
    for (size_t i = 0; i < rows.size(); ++i) { hands += rows[i].hands; net += rows[i].net_bb; }
    if (hands != g.hands) printf("  %s: %lld hands instead of %lld\n", ReportName(kinds[k]), hands, g.hands);
    CHECK(hands == g.hands);
    NEAR(net, g.net_bb, 1e-6);
  }
  {
    std::vector<ReportRow> rows;
    CHECK(BuildReport(g_db, hero, kReportPosition, &rows, &error));
    CHECK(rows.size() == 5 && rows[0].label == "EP" && rows[4].label == "BB");
    CHECK(BuildReport(g_db, hero, kReportStartingHand, &rows, &error));
    CHECK(rows.size() > 100 && rows[0].label == "AA");
    CHECK(BuildReport(g_db, hero, kReportPotType, &rows, &error));
    CHECK(rows.size() >= 3);
  }
  HandMatrix m;
  CHECK(BuildHandMatrix(g_db, hero, &m, &error));
  CHECK(m.cell[0][0].name == "AA" && m.cell[0][1].name == "AKs" && m.cell[1][0].name == "AKo" && m.cell[12][12].name == "22");
  long long matrix_hands = 0;
  for (int r = 0; r < 13; ++r) for (int c = 0; c < 13; ++c) matrix_hands += m.cell[r][c].hands;
  CHECK(matrix_hands == g.hands);
  CHECK(m.cell[0][0].hands > 0 && m.cell[0][0].hands < 60);

  // Filters change the results
  Scope aa = hero; aa.filter.hole_class = "AA";
  Graph ga;
  CHECK(BuildGraph(g_db, aa, &ga, &error) && ga.hands == m.cell[0][0].hands);
  Scope btn = hero; btn.filter.position = ohstats::kPositionBTN;
  CHECK(BuildGraph(g_db, btn, &ga, &error) && ga.hands > 100 && ga.hands < g.hands);

  // Sessions: one hand per minute
  std::vector<Session> sessions;
  CHECK(BuildSessions(g_db, hero, 30 * 60, &sessions, &error));
  CHECK(sessions.size() == 1 && sessions[0].hands == g.hands);
  NEAR(sessions[0].net_bb, g.net_bb, 1e-6);
  CHECK(sessions[0].hands_per_hour > 50 && sessions[0].hands_per_hour < 70);
  CHECK(BuildSessions(g_db, hero, 30, &sessions, &error));
  CHECK((long long)sessions.size() == g.hands);
  CHECK(sessions.size() > 1 && sessions[0].start > sessions[1].start);        // newest first

  std::vector<TournamentRow> tournaments;
  CHECK(BuildTournaments(g_db, hero, &tournaments, &error));
  CHECK(tournaments.size() == 1 && tournaments[0].id == "1234567" && tournaments[0].hands > 50);
  CHECK(tournaments[0].start_bb > 50 && tournaments[0].max_level >= 1);

  // Leaks and types
  ohstats::StatFilter f; f.site = ""; f.any_game_type = false;
  std::vector<LeakRow> leaks;
  CHECK(BuildLeaks(g_db, "Maniac", f, &leaks, &error));
  bool vpip_high = false;
  for (size_t i = 0; i < leaks.size(); ++i) if (leaks[i].stat == "vpip") vpip_high = leaks[i].status == kLeakHigh;
  CHECK(vpip_high);
  CHECK(BuildLeaks(g_db, "Nit", f, &leaks, &error));
  bool vpip_low = false;
  for (size_t i = 0; i < leaks.size(); ++i) if (leaks[i].stat == "vpip") vpip_low = leaks[i].status == kLeakLow && !leaks[i].advice.empty();
  CHECK(vpip_low);
  CHECK(!BuildLeaks(g_db, "", f, &leaks, &error));
  CHECK(ClassifyPlayer(0.497, 0.378, 1500) == kTypeManiac);
  CHECK(ClassifyPlayer(0.113, 0.076, 1500) == kTypeNit);
  CHECK(ClassifyPlayer(0.392, 0.057, 1500) == kTypeCallingStation);
  CHECK(ClassifyPlayer(0.35, 0.15, 1500) == kTypeFish);
  CHECK(ClassifyPlayer(0.213, 0.142, 1500) == kTypeTag);
  CHECK(ClassifyPlayer(0.302, 0.226, 1500) == kTypeLag);
  CHECK(ClassifyPlayer(0.22, 0.05, 1500) == kTypePassive);
  CHECK(ClassifyPlayer(0.5, 0.4, 10) == kTypeUnknown);

  double lo, hi;
  WilsonInterval(50, 100, &lo, &hi);
  NEAR(lo, 0.404, 0.002); NEAR(hi, 0.596, 0.002);
  WilsonInterval(0, 0, &lo, &hi);
  CHECK(lo == 0 && hi == 1);
  WilsonInterval(0, 20, &lo, &hi);
  CHECK(lo == 0 && hi > 0.1 && hi < 0.2);
  CHECK(HasConfidenceInterval("vpip") && !HasConfidenceInterval("total_af") && !HasConfidenceInterval("hands"));
  double num = 0, den = 0;
  ohstats::StatFilter all;
  CHECK(g_db->GetStatFraction("vpip", "Maniac", all, &num, &den));
  double v = 0;
  CHECK(g_db->GetStat("vpip", "Maniac", all, &v));
  NEAR(num / den, v, 1e-9);
}

static void TestImport(const std::string &dir) {
  printf("import\n");
  // The stored hands -> hand history text -> parser -> same text, same statistics
  std::vector<ohstats::Row> rows;
  CHECK(g_db->Select("SELECT id FROM hands ORDER BY id LIMIT 400;", std::vector<ohstats::Cell>(), &rows));
  std::string all_text;
  std::vector<std::string> texts;
  int mismatches = 0;
  for (size_t i = 0; i < rows.size(); ++i) {
    hh::Hand original, parsed;
    CHECK(g_db->LoadHand(rows[i][0].AsInt(), &original));
    std::string text = hh::FormatPokerStars(original);
    std::vector<hh::Hand> hands;
    std::vector<std::string> errors;
    ParsePokerStars(text, &hands, &errors);
    CHECK(hands.size() == 1);
    if (hands.size() != 1) { if (!errors.empty()) printf("  %s\n", errors[0].c_str()); continue; }
    std::string again = hh::FormatPokerStars(hands[0]);
    if (again != text && ++mismatches < 3) printf("--- original\n%s--- parsed\n%s", text.c_str(), again.c_str());
    all_text += text;
  }
  CHECK(mismatches == 0);

  const std::string path = dir + "/import_test.db";
  unlink(path.c_str()); unlink((path + "-wal").c_str()); unlink((path + "-shm").c_str());
  ohstats::Db imported;
  std::string error;
  CHECK(imported.Open(path, &error));
  ImportResult result;
  CHECK(ImportText(&imported, all_text, "stars", &result));
  CHECK(result.hands_found == (int)rows.size() && result.imported == (int)rows.size() && result.failed == 0);
  ImportResult again;
  CHECK(ImportText(&imported, all_text, "stars", &again));
  CHECK(again.imported == 0 && again.duplicates == (int)rows.size());
  CHECK(imported.CountHands() == (long long)rows.size());

  // statistics are the same as in the original database for the same hands
  ohstats::PlayerQuery q;
  q.any_game_type = true;
  q.stats = V("hands", "vpip", "pfr", "3bet", "steal_attempt", "flop_cbet", "wtsd");
  std::vector<ohstats::PlayerRow> a, b;
  CHECK(imported.PlayerTable(q, &b, &error));
  // original: only the same hands -> rebuild a database of the first hands with the importer's input
  const std::string path2 = dir + "/import_ref.db";
  unlink(path2.c_str()); unlink((path2 + "-wal").c_str()); unlink((path2 + "-shm").c_str());
  ohstats::Db reference;
  CHECK(reference.Open(path2, &error));
  for (size_t i = 0; i < rows.size(); ++i) {
    hh::Hand h;
    CHECK(g_db->LoadHand(rows[i][0].AsInt(), &h));
    reference.InsertHand(h, "stars", &error);
  }
  CHECK(reference.PlayerTable(q, &a, &error));
  CHECK(a.size() == b.size() && a.size() == 8);
  for (size_t i = 0; i < a.size() && i < b.size(); ++i) {
    CHECK(a[i].name == b[i].name);
    for (size_t k = 0; k < a[i].values.size(); ++k) {
      bool same = (a[i].values[k] != a[i].values[k] && b[i].values[k] != b[i].values[k]) ||
                  fabs(a[i].values[k] - b[i].values[k]) < 1e-9;
      if (!same) printf("  %s %s: %g vs %g\n", a[i].name.c_str(), q.stats[k].c_str(), a[i].values[k], b[i].values[k]);
      CHECK(same);
    }
  }

  // real PokerStars details: currency symbols, side pots, sitting out, chat, names with spaces
  const char *real =
    "PokerStars Hand #123456789:  Hold'em No Limit ($0.02/$0.05 USD) - 2026/10/11 9:32:00 CET [2026/10/11 3:32:00 ET]\r\n"
    "Table 'Aludra II' 6-max Seat #3 is the button\r\n"
    "Seat 1: Good Guy ($5.00 in chips)\r\n"
    "Seat 2: Sitter ($5.00 in chips) is sitting out\r\n"
    "Seat 3: Villain: X ($4.95 in chips)\r\n"
    "Seat 5: Hero ($10 in chips)\r\n"
    "Good Guy: posts small blind $0.02\r\n"
    "Hero: posts big blind $0.05\r\n"
    "*** HOLE CARDS ***\r\n"
    "Dealt to Hero [Ah Kd]\r\n"
    "Villain: X: raises $0.10 to $0.15\r\n"
    "Good Guy: folds\r\n"
    "Hero: calls $0.10\r\n"
    "Good Guy said, hi there\r\n"
    "*** FLOP *** [2c 7d 9h]\r\n"
    "Hero: checks\r\n"
    "Villain: X: bets $0.20\r\n"
    "Hero: raises $0.40 to $0.60 and is all-in\r\n"
    "Uncalled bet ($0.40) returned to Hero\r\n"
    "*** SHOW DOWN ***\r\n"
    "Hero collected $0.77 from pot\r\n"
    "*** SUMMARY ***\r\n"
    "Total pot $0.80 | Rake $0.03\r\n"
    "Board [2c 7d 9h]\r\n"
    "Seat 1: Good Guy (small blind) folded before Flop\r\n"
    "Seat 5: Hero (big blind) collected ($0.77)\r\n\r\n";
  std::vector<hh::Hand> real_hands;
  std::vector<std::string> errors;
  ParsePokerStars(real, &real_hands, &errors);
  CHECK(real_hands.size() == 1);
  if (real_hands.size() == 1) {
    const hh::Hand &h = real_hands[0];
    CHECK(h.hand_id == "123456789" && h.table_name == "Aludra II" && h.nseats == 6 && h.dealer == 2);
    CHECK(h.name[0] == "Good Guy" && h.name[2] == "Villain: X" && !h.in_hand[1]);
    NEAR(h.sblind, 0.02, 1e-9); NEAR(h.bblind, 0.05, 1e-9);
    CHECK(h.hero == 4 && h.hole[4][0] == "Ah" && h.board_count == 3 && h.showdown);
    CHECK(h.sb_seat == 0 && h.bb_seat == 4);
    CHECK(h.actions.size() == 8);
    NEAR(h.actions[2].to, 0.15, 1e-9); NEAR(h.actions[2].raise_by, 0.10, 1e-9);
    NEAR(h.uncalled, 0.40, 1e-9); CHECK(h.uncalled_seat == 4);
    NEAR(h.total_pot, 0.80, 1e-9); NEAR(h.rake, 0.03, 1e-9);
    NEAR(h.contrib[4], 0.05 + 0.10 + 0.60, 1e-9);
    NEAR(h.end_stack[4], 10 - 0.75 + 0.40 + 0.77, 1e-9);
  }
  // garbage is reported, not imported
  ImportResult junk;
  CHECK(ImportText(&imported, "PokerStars Hand #1: broken\r\nnothing\r\n", "stars", &junk));
  CHECK(junk.hands_found == 1 && junk.imported == 0 && junk.failed == 1 && !junk.errors.empty());
  ImportResult nothing;
  CHECK(ImportText(&imported, "just some text", "stars", &nothing) && nothing.hands_found == 0);
  CHECK(!ImportFile(&imported, dir + "/does_not_exist.txt", "stars", &nothing));
}

int main(int argc, char **argv) {
  if (argc < 2) { printf("usage: %s <output-dir>\n", argv[0]); return 2; }
  const std::string dir = argv[1];
  TestEquity();
  const std::string path = dir + "/logic_test.db";
  unlink(path.c_str()); unlink((path + "-wal").c_str()); unlink((path + "-shm").c_str());
  static ohstats::Db db;
  g_db = &db;
  std::string error;
  CHECK(db.Open(path, &error));
  CHECK(AddDemoHands(&db, 1800, 5, &error));
  TestReports();
  TestImport(dir);
  printf("\n%d checks, %d failures\n", g_checks, g_failures);
  return g_failures ? 1 : 0;
}
