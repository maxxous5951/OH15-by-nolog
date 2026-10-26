//******************************************************************************
//
// This file is part of the OpenHoldem project
//    Licensed under GPL v3: http://www.gnu.org/licenses/gpl.html
//
//******************************************************************************
//
// Purpose: Model of the stats explorer, see explorer_model.h
//
//******************************************************************************

#include "explorer_model.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <algorithm>
#include <limits>
#include <set>
#include <sstream>

#include "HandHistoryFormat.h"

namespace explorer {

//------------------------------------------------------------------------------
// Statistics: display
//------------------------------------------------------------------------------

static bool EndsWith(const std::string &s, const char *suffix) {
  size_t n = strlen(suffix);
  return s.size() >= n && s.compare(s.size() - n, n, suffix) == 0;
}

bool IsUndefined(double value) { return value != value; }

StatKind KindOf(const std::string &stat) {
  if (stat == "hands" || EndsWith(stat, "_opp")) return kKindCount;
  if (EndsWith(stat, "_af")) return kKindRatio;
  return kKindPercent;
}

std::string FormatStat(const std::string &stat, double value) {
  if (IsUndefined(value)) return "-";
  char buffer[32];
  switch (KindOf(stat)) {
    case kKindCount:   snprintf(buffer, sizeof(buffer), "%.0f", value); break;
    case kKindRatio:   snprintf(buffer, sizeof(buffer), "%.2f", value); break;
    default:           snprintf(buffer, sizeof(buffer), "%.1f", value * 100.0); break;
  }
  return buffer;
}

std::string StatLabel(const std::string &stat) {
  static const char *kWords[][2] = {
    {"vpip", "VPIP"}, {"pfr", "PFR"}, {"rfi", "RFI"}, {"wtsd", "WTSD"}, {"wssd", "WSSD"},
    {"af", "AF"}, {"afq", "AFq"}, {"cbet", "CBet"}, {"3bet", "3Bet"}, {"4bet", "4Bet"},
    {"bb", "BB"}, {"sb", "SB"}, {"pf", "PF"}, {"2bb", "2bb"}, {"25bb", "2.5bb"}, {"3bb", "3bb"},
    {"3b", "3B"}, {"l", "L"}, {"f", "F"}, {"vs", "vs"}, {"to", "to"}, {"in", "in"}, {"as", "as"},
    {"afterR", "after R"}, {"opp", "Opp"}, {"pot", "pot"}, {"donkbet", "Donk"},
    {"checkraise", "Check-raise"}, {"checkcall", "Check-call"},
  };
  std::string label;
  std::stringstream parts(stat);
  std::string word;
  while (std::getline(parts, word, '_')) {
    if (word.empty()) continue;   // "flop_fold_to_cbet__flop_raise_cbet_opp"
    std::string shown;
    for (size_t i = 0; i < sizeof(kWords) / sizeof(kWords[0]); ++i) {
      if (word == kWords[i][0]) { shown = kWords[i][1]; break; }
    }
    if (shown.empty()) {
      shown = word;
      shown[0] = (char)toupper((unsigned char)shown[0]);
    }
    if (!label.empty()) label += " ";
    label += shown;
  }
  return label;
}

static std::vector<Section> MakeSections() {
  struct Def { const char *title; const char *stats; };
  static const Def kDefs[] = {
    {"General", "hands vpip pfr rfi wtsd wssd"},
    {"Preflop", "steal_attempt bb_fold_to_steal sb_fold_to_steal 3bet_vs_steal bb_3bet_vs_steal "
                "sb_3bet_vs_steal preflop_called_raise 3bet fold_to_3bet fold_to_3bet_afterR "
                "fold_to_resteal 4bet fold_to_4bet preflop_squeeze 4bet_plus_ratio"},
    {"Aggression", "flop_af turn_af river_af total_af flop_afq turn_afq river_afq total_afq"},
    {"Streets seen", "flop_seen turn_seen river_seen"},
    {"Continuation bets", "flop_cbet flop_fold_to_cbet flop_raise_cbet turn_cbet turn_fold_to_cbet "
                          "river_fold_to_cbet"},
    {"Other postflop", "flop_checkraise turn_checkraise turn_checkcall flop_donkbet river_bet"},
    {"BB vs SB", "bb_3bet_vs_sb bb_fold_vs_sb bb_call_vs_sb bb_3bet_vs_sb_2bb bb_3bet_vs_sb_25bb "
                 "bb_3bet_vs_sb_3bb"},
    {"Open size 2bb / 2.5bb / 3bb",
                 "rfi_2bb rfi_25bb rfi_3bb steal_attempt_2bb steal_attempt_25bb steal_attempt_3bb "
                 "3bet_vs_2bb 3bet_vs_25bb 3bet_vs_3bb 3bet_vs_steal_2bb 3bet_vs_steal_25bb "
                 "3bet_vs_steal_3bb bb_3bet_vs_steal_2bb bb_3bet_vs_steal_25bb bb_3bet_vs_steal_3bb "
                 "sb_3bet_vs_steal_2bb sb_3bet_vs_steal_25bb sb_3bet_vs_steal_3bb"},
    {"Squeeze", "fold_to_pf_squeeze fold_to_squeeze_as_raiser fold_to_squeeze_as_caller"},
    {"3-bet pots", "cbet_flop_in_3bet_pot fold_to_f_cbet_in_3bet_pot donk_flop_in_3bet_plus_pot "
                   "flop_check_raise_in_3bet_pot flop_check_raise_in_3bet_plus_pot"},
    {"Check-raise faced, 3-bet postflop", "fold_to_f_check_raise fold_to_f_check_raise_3b_plus_pot "
                   "flop_fold_to_3bet turn_fold_to_3bet river_fold_to_3bet postflop_fold_to_3bet"},
    {"Float", "float_flop flop_float float_flop_in_3bet_pot call_f_float_bet call_f_float_bet_in_3b_pot "
              "fold_to_f_float_bet_in_3b_pot fold_to_f_float_bet_in_3b_plus_pot"},
  };
  std::vector<Section> sections;
  std::set<std::string> used;
  for (size_t i = 0; i < sizeof(kDefs) / sizeof(kDefs[0]); ++i) {
    Section section;
    section.title = kDefs[i].title;
    std::stringstream names(kDefs[i].stats);
    std::string name;
    while (names >> name) {
      if (!ohstats::Db::IsSupportedStat(name)) continue;
      section.stats.push_back(name);
      used.insert(name);
    }
    sections.push_back(section);
  }
  Section other;
  other.title = "Other (including counters)";
  std::vector<std::string> all = ohstats::Db::SupportedStats();
  for (size_t i = 0; i < all.size(); ++i) {
    if (!used.count(all[i]) && all[i] != "vpip_L" && all[i] != "pfr_L") other.stats.push_back(all[i]);
  }
  sections.push_back(other);
  return sections;
}

const std::vector<Section> &Sections() {
  static const std::vector<Section> sections = MakeSections();
  return sections;
}

static std::vector<std::string> SplitNames(const char *names) {
  std::vector<std::string> out;
  std::stringstream s(names);
  std::string n;
  while (s >> n) if (ohstats::Db::IsSupportedStat(n)) out.push_back(n);
  return out;
}

const std::vector<std::string> &DefaultColumns() {
  static const std::vector<std::string> columns = SplitNames(
    "vpip pfr 3bet fold_to_3bet 4bet steal_attempt bb_fold_to_steal flop_cbet flop_fold_to_cbet "
    "total_af wtsd wssd");
  return columns;
}

const std::vector<std::string> &PresetNames() {
  static std::vector<std::string> names;
  if (names.empty()) {
    names.push_back("General"); names.push_back("Preflop");
    names.push_back("Postflop"); names.push_back("Sizes");
  }
  return names;
}

std::vector<std::string> PresetColumns(const std::string &name) {
  if (name == "General")  return DefaultColumns();
  if (name == "Preflop")  return SplitNames("vpip pfr rfi steal_attempt bb_fold_to_steal sb_fold_to_steal "
                                            "3bet 3bet_vs_steal fold_to_3bet 4bet fold_to_4bet "
                                            "preflop_squeeze preflop_called_raise bb_3bet_vs_sb");
  if (name == "Postflop") return SplitNames("flop_seen flop_cbet flop_fold_to_cbet flop_raise_cbet "
                                            "turn_cbet turn_fold_to_cbet flop_checkraise flop_donkbet "
                                            "flop_af turn_af river_af total_afq wtsd wssd");
  if (name == "Sizes")    return SplitNames("rfi_2bb rfi_25bb rfi_3bb steal_attempt_2bb steal_attempt_25bb "
                                            "steal_attempt_3bb 3bet_vs_2bb 3bet_vs_25bb 3bet_vs_3bb "
                                            "bb_3bet_vs_sb_2bb bb_3bet_vs_sb_25bb bb_3bet_vs_sb_3bb");
  return DefaultColumns();
}

//------------------------------------------------------------------------------
// Amounts, positions
//------------------------------------------------------------------------------

std::string FormatAmount(double value, bool tournament) {
  char buffer[32];
  if (tournament) snprintf(buffer, sizeof(buffer), "%.0f", value);
  else            snprintf(buffer, sizeof(buffer), "%.2f", value);
  return buffer;
}

const char *PositionName(int position) {
  switch (position) {
    case ohstats::kPositionEP:  return "EP";
    case ohstats::kPositionCO:  return "CO";
    case ohstats::kPositionBTN: return "BTN";
    case ohstats::kPositionSB:  return "SB";
    case ohstats::kPositionBB:  return "BB";
  }
  return "";
}

std::string FormatDate(long long seconds) {
  std::string t = ohstats::FormatTimestamp(seconds);   // "2026/10/25 19:24:00"
  for (size_t i = 0; i < t.size(); ++i) if (t[i] == '/') t[i] = '-';
  return t.substr(0, 16);
}

//------------------------------------------------------------------------------
// Replayer
//------------------------------------------------------------------------------

static int BoardCardsForStreet(int street, int board_count) {
  int cards = street <= 1 ? 0 : street == 2 ? 3 : street == 3 ? 4 : 5;
  return std::min(cards, board_count);
}

static std::string SeatName(const hh::Hand &h, int seat) {
  if (seat >= 0 && seat < hh::kMaxSeats && !h.name[seat].empty()) return h.name[seat];
  char buffer[32];
  snprintf(buffer, sizeof(buffer), "Seat %d", seat + 1);
  return buffer;
}

std::vector<ReplayFrame> BuildReplay(const hh::Hand &h) {
  std::vector<ReplayFrame> frames;
  ReplayFrame f;
  for (int i = 0; i < hh::kMaxSeats; ++i) f.stack[i] = h.start_stack[i];
  frames.push_back(f);
  int street = 1;
  for (size_t i = 0; i < h.actions.size(); ++i) {
    const hh::Action &a = h.actions[i];
    if (a.seat < 0 || a.seat >= hh::kMaxSeats) continue;
    if (a.street != street) {                      // new betting round: bets go to the pot
      street = a.street;
      for (int k = 0; k < hh::kMaxSeats; ++k) f.bet[k] = 0;
    }
    f.street = street;
    f.actor = a.seat;
    f.board_cards = BoardCardsForStreet(street, h.board_count);
    f.stack[a.seat] -= a.amount;
    if (f.stack[a.seat] < 0) f.stack[a.seat] = 0;
    f.pot += a.amount;
    if (a.type == hh::kPostAnte) {
      // antes are dead money, not a bet
    } else if (a.type != hh::kFold && a.type != hh::kCheck) {
      f.bet[a.seat] += a.amount;
    }
    if (a.type == hh::kFold) f.folded[a.seat] = true;
    const bool t = h.tournament;
    std::string who = SeatName(h, a.seat);
    switch (a.type) {
      case hh::kPostAnte:       f.text = who + " posts ante " + FormatAmount(a.amount, t); break;
      case hh::kPostSmallBlind: f.text = who + " posts small blind " + FormatAmount(a.amount, t); break;
      case hh::kPostBigBlind:   f.text = who + " posts big blind " + FormatAmount(a.amount, t); break;
      case hh::kFold:           f.text = who + " folds"; break;
      case hh::kCheck:          f.text = who + " checks"; break;
      case hh::kCall:           f.text = who + " calls " + FormatAmount(a.amount, t); break;
      case hh::kBet:            f.text = who + " bets " + FormatAmount(a.amount, t); break;
      case hh::kRaise:          f.text = who + " raises to " + FormatAmount(a.to, t); break;
    }
    if (a.allin) f.text += " (all-in)";
    frames.push_back(f);
  }
  // Result
  ReplayFrame last = f;
  last.actor = -1;
  last.board_cards = h.board_count;
  last.pot = h.total_pot;
  for (int i = 0; i < hh::kMaxSeats; ++i) {
    last.bet[i] = 0;
    if (h.in_hand[i]) last.stack[i] = h.end_stack[i];
  }
  std::string winners;
  for (int i = 0; i < hh::kMaxSeats; ++i) {
    if (h.in_hand[i] && h.collected[i] > hh::kEps) {
      if (!winners.empty()) winners += ", ";
      winners += SeatName(h, i) + " wins " + FormatAmount(h.collected[i], h.tournament);
    }
  }
  last.text = winners.empty() ? "Hand over" : winners;
  frames.push_back(last);
  return frames;
}

//------------------------------------------------------------------------------
// Explorer
//------------------------------------------------------------------------------

const std::vector<std::string> &PeriodNames() {
  static std::vector<std::string> names;
  if (names.empty()) {
    names.push_back("All time");   names.push_back("Last 24 hours"); names.push_back("Last 7 days");
    names.push_back("Last 30 days"); names.push_back("Last 90 days"); names.push_back("Last year");
  }
  return names;
}

long long PeriodSeconds(int index) {
  static const long long seconds[] = { 0, 86400LL, 7 * 86400LL, 30 * 86400LL, 90 * 86400LL, 365 * 86400LL };
  if (index < 0 || index >= (int)(sizeof(seconds) / sizeof(seconds[0]))) return 0;
  return seconds[index];
}

Explorer::Explorer()
    : sort_column(-1), sort_descending(true), hands_total(0), hands_page(0), selected_hand(0),
      replay_step(0), total_hands_in_db(0), players_dirty(false), hands_dirty(false) {
  columns = DefaultColumns();
}

bool Explorer::Open(const std::string &new_path, std::string *err) {
  std::string message;
  if (!db.Open(new_path, &message)) {
    error = message;
    if (err) *err = message;
    return false;
  }
  path = new_path;
  error.clear();
  sites = db.Sites();
  filters = Filters();
  selected_player.clear();
  selected_hand = 0;
  hand_text.clear();
  replay.clear();
  hands_page = 0;
  RefreshPlayers();
  RefreshHands();
  return true;
}

long long Explorer::Since() const {
  long long seconds = PeriodSeconds(filters.period_index);
  if (seconds <= 0) return 0;
  long long since = (long long)time(NULL) - seconds;
  return since < 1 ? 1 : since;
}

ohstats::StatFilter Explorer::MakeStatFilter() const {
  ohstats::StatFilter f;
  if (filters.site_index > 0 && filters.site_index <= (int)sites.size()) f.site = sites[filters.site_index - 1];
  f.tournament = filters.game_type == 1;
  f.any_game_type = filters.game_type == 2;
  f.nb_hands = filters.last_hands > 0 ? filters.last_hands : 0;
  f.since = Since();
  f.position = filters.position;
  return f;
}

void Explorer::RefreshPlayers() {
  players_dirty = false;
  sites = db.Sites();
  total_hands_in_db = db.CountHands();
  ohstats::PlayerQuery q;
  q.filter = MakeStatFilter();
  q.name_contains = filters.name;
  q.min_hands = filters.min_hands < 1 ? 1 : filters.min_hands;
  q.stats = columns;
  q.limit = 5000;
  std::string message;
  if (!db.PlayerTable(q, &players, &message)) {
    players.clear();
    error = message;
  } else {
    error.clear();
  }
  SortPlayers();
}

void Explorer::SortPlayers() {
  const int column = sort_column;
  const bool desc = sort_descending;
  struct Less {
    int column; bool desc;
    bool operator()(const ohstats::PlayerRow &a, const ohstats::PlayerRow &b) const {
      if (column == -2) {
        int c = strcasecmp_(a.name, b.name);
        return desc ? c > 0 : c < 0;
      }
      double x, y;
      if (column == -1)      { x = (double)a.hands; y = (double)b.hands; }
      else if (column == -3) { x = a.won; y = b.won; }
      else {
        x = (column < (int)a.values.size()) ? a.values[column] : std::numeric_limits<double>::quiet_NaN();
        y = (column < (int)b.values.size()) ? b.values[column] : std::numeric_limits<double>::quiet_NaN();
        // undefined values always at the end
        if (IsUndefined(x) || IsUndefined(y)) {
          if (IsUndefined(x) && IsUndefined(y)) return a.name < b.name;
          return IsUndefined(y);
        }
      }
      if (x != y) return desc ? x > y : x < y;
      return a.name < b.name;
    }
    static int strcasecmp_(const std::string &a, const std::string &b) {
      size_t n = std::min(a.size(), b.size());
      for (size_t i = 0; i < n; ++i) {
        int ca = tolower((unsigned char)a[i]), cb = tolower((unsigned char)b[i]);
        if (ca != cb) return ca < cb ? -1 : 1;
      }
      return a.size() == b.size() ? 0 : (a.size() < b.size() ? -1 : 1);
    }
  };
  Less less = { column, desc };
  std::stable_sort(players.begin(), players.end(), less);
}

void Explorer::RefreshHands() {
  hands_dirty = false;
  ohstats::HandQuery q;
  if (filters.site_index > 0 && filters.site_index <= (int)sites.size()) q.site = sites[filters.site_index - 1];
  q.game_type = filters.game_type == 2 ? -1 : filters.game_type;
  q.since = Since();
  q.player = selected_player;
  q.limit = kHandsPerPage;
  long long total = 0;
  long long last_page = 0;
  q.offset = hands_page * kHandsPerPage;
  std::string message;
  if (!db.ListHands(q, &hands, &total, &message)) {
    hands.clear();
    total = 0;
    error = message;
  }
  hands_total = total;
  last_page = total > 0 ? (total - 1) / kHandsPerPage : 0;
  if (hands_page > last_page) {          // the list became shorter
    hands_page = (int)last_page;
    q.offset = hands_page * kHandsPerPage;
    db.ListHands(q, &hands, &total, &message);
  }
}

void Explorer::SelectPlayer(const std::string &name) {
  selected_player = name;
  hands_page = 0;
  RefreshHands();
}

bool Explorer::SelectHand(long long pk) {
  hh::Hand loaded;
  std::string site, message;
  if (!db.LoadHand(pk, &loaded, &site, &message)) {
    error = message;
    return false;
  }
  selected_hand = pk;
  hand = loaded;
  hand_site = site;
  hh::FormatOptions options;
  if (loaded.tournament) options.currency_symbol = "";
  hand_text = hh::FormatPokerStars(hand, options);
  replay = BuildReplay(hand);
  replay_step = 0;
  return true;
}

double Explorer::SheetValue(const std::string &stat) {
  const double kNaN = std::numeric_limits<double>::quiet_NaN();
  if (selected_player.empty()) return kNaN;
  // The sheet asks for ~100 values per frame: cached until a filter or the data changes
  char key[512];
  snprintf(key, sizeof(key), "%s|%d|%d|%d|%d|%d|%s|%lld", selected_player.c_str(), filters.site_index,
           filters.game_type, filters.last_hands, filters.period_index, filters.position,
           path.c_str(), total_hands_in_db);
  if (sheet_key_ != key) { sheet_key_ = key; sheet_cache_.clear(); }
  std::map<std::string, double>::const_iterator cached = sheet_cache_.find(stat);
  if (cached != sheet_cache_.end()) return cached->second;
  double value = 0;
  if (!db.GetStat(stat, selected_player, MakeStatFilter(), &value)) value = kNaN;
  sheet_cache_[stat] = value;
  return value;
}

bool Explorer::HasColumn(const std::string &stat) const {
  return std::find(columns.begin(), columns.end(), stat) != columns.end();
}

bool Explorer::AddColumn(const std::string &stat) {
  if (!ohstats::Db::IsSupportedStat(stat) || HasColumn(stat)) return false;
  columns.push_back(stat);
  players_dirty = true;
  return true;
}

void Explorer::RemoveColumn(const std::string &stat) {
  std::vector<std::string>::iterator it = std::find(columns.begin(), columns.end(), stat);
  if (it == columns.end()) return;
  int index = (int)(it - columns.begin());
  columns.erase(it);
  if (sort_column == index) sort_column = -1;
  else if (sort_column > index) --sort_column;
  players_dirty = true;
}

void Explorer::SetColumns(const std::vector<std::string> &stats) {
  columns = stats;
  if (sort_column >= (int)columns.size()) sort_column = -1;
  players_dirty = true;
}

bool Explorer::DatabaseChanged() {
  return db.IsOpen() && db.CountHands() != total_hands_in_db;
}

}  // namespace explorer
