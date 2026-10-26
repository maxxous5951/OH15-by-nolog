//******************************************************************************
//
// This file is part of the OpenHoldem project
//    Licensed under GPL v3: http://www.gnu.org/licenses/gpl.html
//
//******************************************************************************
//
// Purpose: exports of the explorer, see export.h
//
//******************************************************************************

#include "export.h"

#include <stdio.h>
#include <fstream>

#include "HandHistoryFormat.h"

namespace explorer {

std::string CsvField(const std::string &text) {
  if (text.find_first_of(",\"\r\n;") == std::string::npos) return text;
  std::string out = "\"";
  for (size_t i = 0; i < text.size(); ++i) {
    if (text[i] == '"') out += '"';
    out += text[i];
  }
  return out + "\"";
}

static std::string Number(double v, int decimals) {
  if (IsUndefined(v)) return "";
  char b[48];
  snprintf(b, sizeof(b), "%.*f", decimals, v);
  return b;
}

static bool Open(std::ofstream *file, const std::string &path, std::string *error) {
  file->open(path.c_str(), std::ios::binary | std::ios::trunc);
  if (!*file) { if (error) *error = "can't write " + path; return false; }
  return true;
}

long long ExportPlayersCsv(const Explorer &ex, const std::string &path, std::string *error) {
  std::ofstream file;
  if (!Open(&file, path, error)) return -1;
  file << "player,type,hands,won";
  for (size_t c = 0; c < ex.columns.size(); ++c) file << "," << CsvField(StatLabel(ex.columns[c]));
  file << "\n";
  for (size_t i = 0; i < ex.players.size(); ++i) {
    const ohstats::PlayerRow &p = ex.players[i];
    file << CsvField(p.name) << "," << PlayerTypeName(ex.TypeOf(p.name)) << "," << p.hands << "," << Number(p.won, 2);
    for (size_t c = 0; c < ex.columns.size() && c < p.values.size(); ++c) {
      // percentages as 0..100, counts and ratios as they are
      double v = p.values[c];
      const bool percent = KindOf(ex.columns[c]) == kKindPercent;
      file << "," << Number(percent && !IsUndefined(v) ? v * 100.0 : v, KindOf(ex.columns[c]) == kKindCount ? 0 : (percent ? 1 : 2));
    }
    file << "\n";
  }
  file.flush();
  if (!file) { if (error) *error = "write error"; return -1; }
  return (long long)ex.players.size();
}

long long ExportReportCsv(const Explorer &ex, const std::string &path, std::string *error) {
  std::ofstream file;
  if (!Open(&file, path, error)) return -1;
  file << CsvField(ReportName(ex.report_kind)) << ",hands,net,net_bb,bb_per_100,vpip,pfr,showdown_win\n";
  for (size_t i = 0; i < ex.report.size(); ++i) {
    const ReportRow &r = ex.report[i];
    file << CsvField(r.label) << "," << r.hands << "," << Number(r.net, 2) << "," << Number(r.net_bb, 2) << ","
         << Number(r.bb_per_100, 2) << "," << Number(IsUndefined(r.vpip) ? r.vpip : r.vpip * 100, 1) << ","
         << Number(IsUndefined(r.pfr) ? r.pfr : r.pfr * 100, 1) << ","
         << Number(IsUndefined(r.sd_win) ? r.sd_win : r.sd_win * 100, 1) << "\n";
  }
  file.flush();
  if (!file) { if (error) *error = "write error"; return -1; }
  return (long long)ex.report.size();
}

// The query of the hand list without paging
static ohstats::HandQuery CurrentQuery(const Explorer &ex) {
  ohstats::HandQuery q;
  if (ex.filters.site_index > 0 && ex.filters.site_index <= (int)ex.sites.size()) q.site = ex.sites[ex.filters.site_index - 1];
  q.game_type = ex.filters.game_type == 2 ? -1 : ex.filters.game_type;
  q.since = ex.Since();
  q.player = ex.selected_player;
  q.filter = ex.hand_filter;
  return q;
}

long long ExportHandListCsv(Explorer *ex, const std::string &path, std::string *error) {
  std::ofstream file;
  if (!Open(&file, path, error)) return -1;
  file << "date,site,hand_id,table,type,sb,bb,player,position,cards,board,result,pot,marked\n";
  ohstats::HandQuery q = CurrentQuery(*ex);
  q.limit = 1000;
  long long written = 0, total = 0;
  for (q.offset = 0;; q.offset += q.limit) {
    std::vector<ohstats::HandRow> rows;
    if (!ex->db.ListHands(q, &rows, q.offset == 0 ? &total : NULL, error)) return -1;
    for (size_t i = 0; i < rows.size(); ++i) {
      const ohstats::HandRow &h = rows[i];
      file << FormatDate(h.played_at) << "," << CsvField(h.site) << "," << CsvField(h.hand_id) << ","
           << CsvField(h.table_name) << "," << (h.tournament ? "tournament" : "cash") << ","
           << Number(h.sblind, 2) << "," << Number(h.bblind, 2) << "," << CsvField(h.has_player ? h.player_name : "") << ","
           << (h.has_player ? PositionName(h.player_position) : "") << "," << (h.has_player ? h.player_hole : "") << ","
           << CsvField(h.board) << "," << (h.has_player ? Number(h.player_net, 2) : "") << "," << Number(h.pot, 2) << ","
           << (h.marked ? 1 : 0) << "\n";
      ++written;
    }
    if ((int)rows.size() < q.limit) break;
  }
  file.flush();
  if (!file) { if (error) *error = "write error"; return -1; }
  return written;
}

long long ExportHandsText(Explorer *ex, const std::string &path, long long max_hands, std::string *error) {
  std::ofstream file;
  if (!Open(&file, path, error)) return -1;
  ohstats::HandQuery q = CurrentQuery(*ex);
  q.limit = 500;
  long long written = 0;
  for (q.offset = 0; written < max_hands; q.offset += q.limit) {
    std::vector<ohstats::HandRow> rows;
    if (!ex->db.ListHands(q, &rows, NULL, error)) return -1;
    for (size_t i = 0; i < rows.size() && written < max_hands; ++i) {
      hh::Hand hand;
      if (!ex->db.LoadHand(rows[i].pk, &hand)) continue;
      file << hh::FormatPokerStars(hand) << "\r\n";
      ++written;
    }
    if ((int)rows.size() < q.limit) break;
  }
  file.flush();
  if (!file) { if (error) *error = "write error"; return -1; }
  return written;
}

}  // namespace explorer
