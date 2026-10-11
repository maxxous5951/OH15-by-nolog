// Replays the "snapshots" trace of a rejected-hand log through the reconstructor
#include "HandHistoryCore.h"
#include "HandHistoryFormat.h"
#include <fstream>
#include <iostream>
#include <sstream>
#include <cstdlib>
using namespace hh;

static bool ParseLine(const std::string &line, Snapshot *out, bool *is_next) {
  size_t first = line.find(" | ");
  std::string head = (first == std::string::npos) ? line : line.substr(0, first);
  *is_next = (head.compare(0, 5, "NEXT>") == 0);
  size_t p = head.find('+'); if (p == std::string::npos) return false;
  Snapshot s; s.nseats = 10;   // DDPoker has 10 seats
  int secs = 0, btn = 0; char board[128] = {0};
  if (sscanf(head.c_str() + p, "+%ds br=%d btn=%d pot=%lf blinds=%lf/%lf board=%127[^\n]",
             &secs, &s.betround, &btn, &s.pot, &s.sblind, &s.bblind, board) < 6) return false;
  s.dealer = btn - 1; s.time = 1790000000LL + secs;
  std::istringstream bs(board); std::string c; int n = 0;
  while (bs >> c && n < 5) { if (c != "-") s.board[n++] = c; }
  size_t pos = first;
  while (pos != std::string::npos) {
    size_t next = line.find(" | ", pos + 3);
    std::string seg = line.substr(pos + 3, next == std::string::npos ? std::string::npos : next - pos - 3);
    pos = next;
    int seat = 0; char name[64] = {0}; double bal = 0, bet = 0;
    const char *t = seg.c_str(); if (t[0] != 'S') continue;
    int consumed = 0;
    if (sscanf(t, "S%d%n", &seat, &consumed) < 1) continue;
    bool hero = (t[consumed] == '*'); if (hero) consumed++;
    if (sscanf(t + consumed, " %63s bal=%lf bet=%lf", name, &bal, &bet) < 3) continue;
    std::string nm = name; bool out = false;
    if (nm.size() > 5 && nm.substr(nm.size() - 5) == "(out)") { out = true; nm.resize(nm.size() - 5); }
    SeatSnapshot &ss = s.seat[seat - 1];
    ss.seated = true; ss.active = !out; ss.name = nm; ss.balance = bal; ss.bet = bet;
    if (hero) s.hero = seat - 1;
    size_t br = seg.find('[');
    if (br != std::string::npos) { std::istringstream cs(seg.substr(br + 1)); std::string a, b; cs >> a >> b; if (!b.empty() && b[b.size()-1] == ']') b.resize(b.size()-1); ss.cards[0] = a; ss.cards[1] = b; }
  }
  *out = s; return true;
}

int main(int argc, char **argv) {
  std::ifstream f(argv[1]); std::string line;
  bool tournament = argc > 2 && std::string(argv[2]) == "t";
  Reconstructor r; Options o; if (tournament) o.max_rake = 0.01; r.SetOptions(o);
  int fed = 0;
  std::vector<Hand> all;
  // The trace of a rejected log only contains the snapshots of the hands that were
  // rejected, so every block ("######## REJECTED HAND") starts with a fresh reconstructor.
  struct Drain { static void Run(Reconstructor &rc, std::vector<Hand> &out) { Hand x; while (rc.PopCompleted(&x)) out.push_back(x); } };
  while (std::getline(f, line)) {
    if (!line.empty() && line[line.size()-1] == '\r') line.resize(line.size()-1);
    if (line.compare(0, 8, "########") == 0 && line.find("REJECTED") != std::string::npos) {
      Drain::Run(r, all);
      r = Reconstructor(); r.SetOptions(o);
      continue;
    }
    if (line.compare(0, 5, "START") != 0 && line.compare(0, 1, "+") != 0 && line.compare(0, 5, "NEXT>") != 0) continue;
    Snapshot s; bool is_next;
    if (!ParseLine(line, &s, &is_next)) { std::cerr << "unparsed: " << line.substr(0, 60) << "\n"; continue; }
    r.Feed(s); fed++;
  }
  // last hand: let the table idle
  Drain::Run(r, all);
  int count = 0;
  for (size_t n = 0; n < all.size(); n++) {
    Hand h = all[n];
    ++count;
    h.hand_id = std::string("1000") + char('0' + count); h.timestamp = "2026/10/11 10:00:00";
    h.tournament = tournament; h.tournament_id = "900000001"; h.table_name = "900000001 1";
    std::cout << "=== hand " << count << (h.valid() ? ": VALID" : ": REJECTED") << "\n";
    for (size_t i = 0; i < h.problems.size(); i++) std::cout << "  problem: " << h.problems[i] << "\n";
    std::cout << FormatPokerStars(h);
  }
  std::cout << "fed " << fed << " snapshots, " << count << " hands\n";
}
