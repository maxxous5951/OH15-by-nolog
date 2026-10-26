//******************************************************************************
//
// This file is part of the OpenHoldem project
//    Licensed under GPL v3: http://www.gnu.org/licenses/gpl.html
//
//******************************************************************************
//
// Purpose: panels of the explorer, see ui_panels.h
//
//******************************************************************************

#include "ui_panels.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <algorithm>

#include "export.h"
#include "hh_import.h"

namespace explorer {

namespace {

// Escape closes the modal dialog that is being drawn
void EscapeCloses() {
  if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
}

const ImVec4 kGreen(0.35f, 0.85f, 0.45f, 1.0f);
const ImVec4 kRed(0.95f, 0.40f, 0.40f, 1.0f);
const ImVec4 kGrey(0.60f, 0.60f, 0.65f, 1.0f);
const ImVec4 kOrange(1.0f, 0.65f, 0.25f, 1.0f);
const ImVec4 kBlue(0.45f, 0.70f, 1.0f, 1.0f);

bool ComboList(const char *label, int *index, const char *const *items, int count, float width) {
  bool changed = false;
  if (width > 0) ImGui::SetNextItemWidth(width);
  if (ImGui::BeginCombo(label, items[*index])) {
    for (int i = 0; i < count; ++i) {
      if (ImGui::Selectable(items[i], i == *index)) { if (*index != i) changed = true; *index = i; }
    }
    ImGui::EndCombo();
  }
  return changed;
}

const char *const kPotTypes[] = { "Any pot", "Limped", "Single raised", "3-bet pot", "4-bet+ pot" };
const char *const kResults[] = { "Any result", "Won", "Lost" };
const char *const kShowdowns[] = { "Any", "Went to showdown", "No showdown" };
const char *const kPositions[] = { "Any position", "EP", "CO", "BTN", "SB", "BB" };

ImU32 HeatColor(double bb_per_100, long long hands) {
  if (hands <= 0) return IM_COL32(45, 45, 52, 255);
  double t = bb_per_100 / 150.0;
  if (t > 1) t = 1;
  if (t < -1) t = -1;
  const double strength = 0.25 + 0.6 * fabs(t);
  if (t >= 0) return IM_COL32((int)(40 * (1 - strength) + 30 * strength), (int)(45 * (1 - strength) + 170 * strength), (int)(50 * (1 - strength) + 80 * strength), 255);
  return IM_COL32((int)(40 * (1 - strength) + 200 * strength), (int)(45 * (1 - strength) + 60 * strength), (int)(50 * (1 - strength) + 60 * strength), 255);
}

}  // namespace

std::string FilterSummary(const ohstats::HandFilter &f) {
  std::string s;
  char b[96];
  auto add = [&s](const std::string &part) { if (!s.empty()) s += ", "; s += part; };
  if (f.position >= 0) add(std::string("position ") + PositionName(f.position));
  if (!f.hole_class.empty()) add("hand " + f.hole_class);
  if (!f.board_has.empty()) add("board has " + f.board_has);
  if (f.pot_type > 0) add(kPotTypes[f.pot_type]);
  if (f.result == 1) add("won"); else if (f.result == 2) add("lost");
  if (f.showdown == 1) add("showdown"); else if (f.showdown == 2) add("no showdown");
  if (f.min_pot_bb > 0 || f.max_pot_bb > 0) { snprintf(b, sizeof(b), "pot %g-%g bb", f.min_pot_bb, f.max_pot_bb); add(b); }
  if (f.min_eff_bb > 0 || f.max_eff_bb > 0) { snprintf(b, sizeof(b), "stack %g-%g bb", f.min_eff_bb, f.max_eff_bb); add(b); }
  if (f.marked_only) add("starred");
  if (!f.tag.empty()) add("tag " + f.tag);
  if (!f.text.empty()) add("note \"" + f.text + "\"");
  return s;
}

ImVec4 TypeColor(PlayerType t) {
  switch (t) {
    case kTypeNit:            return kBlue;
    case kTypeTag:            return kGreen;
    case kTypeLag:            return kOrange;
    case kTypeManiac:         return kRed;
    case kTypeFish:           return ImVec4(1.0f, 0.9f, 0.4f, 1.0f);
    case kTypeCallingStation: return ImVec4(0.95f, 0.8f, 0.3f, 1.0f);
    case kTypePassive:        return ImVec4(0.7f, 0.75f, 0.9f, 1.0f);
    default:                  return kGrey;
  }
}

bool InputString(const char *label, std::string *text, const char *hint, float width) {
  char buffer[256];
  snprintf(buffer, sizeof(buffer), "%s", text->c_str());
  if (width != 0) ImGui::SetNextItemWidth(width);
  const bool changed = ImGui::InputTextWithHint(label, hint, buffer, sizeof(buffer));
  if (changed) *text = buffer;
  return changed;
}

//------------------------------------------------------------------------------
// Filter bar
//------------------------------------------------------------------------------

void DrawFilterBar(Explorer &ex, UiState &ui) {
  if (ImGui::SmallButton("Hand filters...")) ui.open_filter_popup = true;
  const std::string summary = FilterSummary(ex.hand_filter);
  ImGui::SameLine();
  if (summary.empty()) {
    ImGui::TextColored(kGrey, "no hand filter");
  } else {
    ImGui::TextColored(kOrange, "%s", summary.c_str());
    ImGui::SameLine();
    if (ImGui::SmallButton("Clear")) ex.ClearHandFilter();
  }
  const std::string who = ex.selected_player.empty() ? std::string("hero") : ex.selected_player;
  ImGui::SameLine();
  ImGui::TextColored(kGrey, "  |  hands of: %s", who.c_str());
}

//------------------------------------------------------------------------------
// Player: HUD line, leaks
//------------------------------------------------------------------------------

void DrawHud(Explorer &ex) {
  if (ex.selected_player.empty()) return;
  struct Item { const char *label; const char *stat; };
  static const Item items[] = { {"VPIP", "vpip"}, {"PFR", "pfr"}, {"3B", "3bet"}, {"F3B", "fold_to_3bet"},
                                {"AF", "total_af"}, {"CB", "flop_cbet"}, {"WTSD", "wtsd"} };
  std::string line;
  for (size_t i = 0; i < sizeof(items) / sizeof(items[0]); ++i) {
    if (i) line += "  /  ";
    line += std::string(items[i].label) + " " + FormatStat(items[i].stat, ex.SheetValue(items[i].stat));
  }
  const double hands = ex.SheetValue("hands");
  const double vpip = ex.SheetValue("vpip"), pfr = ex.SheetValue("pfr");
  const PlayerType type = ClassifyPlayer(vpip, pfr, IsUndefined(hands) ? 0 : (long long)hands);
  ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.12f, 0.14f, 0.20f, 1.0f));
  ImGui::BeginChild("##hud", ImVec2(0, ImGui::GetFrameHeight() * 2.7f), ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollbar);
  ImGui::TextUnformatted(line.c_str());
  ImGui::TextColored(TypeColor(type), "%s", PlayerTypeName(type));
  ImGui::SameLine();
  ImGui::TextColored(kGrey, "  %s hands", FormatStat("hands", hands).c_str());
  ImGui::EndChild();
  ImGui::PopStyleColor();
}

void DrawLeaks(Explorer &ex) {
  ImGui::SetNextItemOpen(false, ImGuiCond_Once);
  if (!ImGui::CollapsingHeader("Leaks (compared with reference ranges)")) return;
  ex.UpdateLeaks();
  ImGui::TextColored(kGrey, "Reference: solid regular, 6-max NLHE cash. A statistic is flagged only when its whole\n"
                            "95%% confidence interval is outside the range, so small samples are not judged.");
  if (ex.leaks.empty()) { ImGui::TextUnformatted("No data."); return; }
  if (!ImGui::BeginTable("leaks", 7, ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_SizingFixedFit)) return;
  ImGui::TableSetupColumn("Stat", 0, 130);
  ImGui::TableSetupColumn("Value", 0, 50);
  ImGui::TableSetupColumn("n", 0, 50);
  ImGui::TableSetupColumn("95% CI", 0, 90);
  ImGui::TableSetupColumn("Range", 0, 80);
  ImGui::TableSetupColumn("Verdict", 0, 60);
  ImGui::TableSetupColumn("Advice", ImGuiTableColumnFlags_WidthStretch);
  ImGui::TableHeadersRow();
  for (size_t i = 0; i < ex.leaks.size(); ++i) {
    const LeakRow &r = ex.leaks[i];
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted(StatLabel(r.stat).c_str());
    ImGui::TableSetColumnIndex(1); ImGui::TextUnformatted(FormatStat(r.stat, r.value).c_str());
    ImGui::TableSetColumnIndex(2); ImGui::Text("%.0f", r.opportunities);
    ImGui::TableSetColumnIndex(3);
    if (r.status != kLeakNoData) ImGui::Text("%.1f - %.1f", r.ci_low * 100, r.ci_high * 100);
    ImGui::TableSetColumnIndex(4); ImGui::Text("%.0f - %.0f", r.range_low * 100, r.range_high * 100);
    ImGui::TableSetColumnIndex(5);
    switch (r.status) {
      case kLeakOk:     ImGui::TextColored(kGreen, "OK"); break;
      case kLeakLow:    ImGui::TextColored(kBlue, "LOW"); break;
      case kLeakHigh:   ImGui::TextColored(kOrange, "HIGH"); break;
      default:          ImGui::TextColored(kGrey, "no data"); break;
    }
    ImGui::TableSetColumnIndex(6); ImGui::TextWrapped("%s", r.advice.c_str());
  }
  ImGui::EndTable();
}

//------------------------------------------------------------------------------
// Graph
//------------------------------------------------------------------------------

void DrawGraphTab(Explorer &ex, UiState &ui) {
  DrawFilterBar(ex, ui);
  ex.UpdateGraph();
  const Graph &g = ex.graph;
  ImGui::Checkbox("Showdown / no showdown", &ui.graph_split);
  ImGui::SameLine();
  ImGui::Checkbox("95% confidence cone", &ui.graph_cone);
  if (g.hands < 2) { ImGui::TextWrapped("Not enough hands for a graph."); return; }
  ImGui::Text("%lld hands   result %+.1f bb (%s)   %+.2f bb/100  (95%%: +-%.2f)   standard deviation %.1f bb/hand",
              g.hands, g.net_bb, FormatAmount(g.money, false).c_str(), g.bb_per_100, g.WinrateMargin(), g.std_dev_bb);

  const ImVec2 origin = ImGui::GetCursorScreenPos();
  ImVec2 size = ImGui::GetContentRegionAvail();
  size.y = std::max(120.0f, size.y - 4);
  ImGui::InvisibleButton("##graph", size);
  ImDrawList *dl = ImGui::GetWindowDrawList();
  const float left = 64, bottom = 26, top = 8, right = 12;
  const ImVec2 p0(origin.x + left, origin.y + top), p1(origin.x + size.x - right, origin.y + size.y - bottom);
  if (p1.x - p0.x < 50 || p1.y - p0.y < 50) return;
  dl->AddRectFilled(p0, p1, IM_COL32(18, 18, 24, 255));

  const size_t n = g.points.size();
  const double mean = g.net_bb / (double)g.hands, sd = g.std_dev_bb;
  double ymin = 0, ymax = 0;
  for (size_t i = 0; i < n; ++i) {
    ymin = std::min(ymin, g.points[i].total); ymax = std::max(ymax, g.points[i].total);
    if (ui.graph_split) {
      ymin = std::min(ymin, std::min(g.points[i].showdown, g.points[i].no_showdown));
      ymax = std::max(ymax, std::max(g.points[i].showdown, g.points[i].no_showdown));
    }
  }
  if (ui.graph_cone) {
    const double h = 1.96 * sd * sqrt((double)n);
    ymin = std::min(ymin, mean * (double)n - h); ymax = std::max(ymax, mean * (double)n + h);
  }
  if (ymax - ymin < 1) { ymax += 1; ymin -= 1; }
  const double pad = (ymax - ymin) * 0.05;
  ymin -= pad; ymax += pad;
  auto X = [&](double i) { return p0.x + (float)(i / (double)(n - 1)) * (p1.x - p0.x); };
  auto Y = [&](double v) { return p1.y - (float)((v - ymin) / (ymax - ymin)) * (p1.y - p0.y); };

  // grid and labels
  const int lines = 6;
  for (int i = 0; i <= lines; ++i) {
    const double v = ymin + (ymax - ymin) * i / lines;
    const float y = Y(v);
    dl->AddLine(ImVec2(p0.x, y), ImVec2(p1.x, y), IM_COL32(60, 60, 70, 255));
    char b[32]; snprintf(b, sizeof(b), "%.0f bb", v);
    dl->AddText(ImVec2(origin.x + 4, y - 7), IM_COL32(170, 170, 180, 255), b);
  }
  if (ymin < 0 && ymax > 0) dl->AddLine(ImVec2(p0.x, Y(0)), ImVec2(p1.x, Y(0)), IM_COL32(150, 150, 160, 255), 1.5f);
  for (int i = 0; i <= 4; ++i) {
    const double idx = (double)(n - 1) * i / 4;
    char b[32]; snprintf(b, sizeof(b), "%.0f", idx + 1);
    dl->AddText(ImVec2(X(idx) - 10, p1.y + 4), IM_COL32(170, 170, 180, 255), b);
  }
  dl->AddText(ImVec2(p1.x - 40, p1.y + 4 + 0), IM_COL32(120, 120, 130, 255), "hands");

  // confidence cone around the winrate line
  const size_t stride = std::max<size_t>(1, n / 1200);
  dl->PushClipRect(p0, p1, true);
  if (ui.graph_cone && sd > 0) {
    const size_t cone_step = std::max<size_t>(stride, n / 80);
    for (size_t i = cone_step; i < n; i += cone_step) {
      const size_t j = std::min(n - 1, i + cone_step);
      const double a = (double)i, b = (double)j;
      const double ha = 1.96 * sd * sqrt(a), hb = 1.96 * sd * sqrt(b);
      ImVec2 quad[4] = { ImVec2(X(a), Y(mean * a + ha)), ImVec2(X(b), Y(mean * b + hb)),
                         ImVec2(X(b), Y(mean * b - hb)), ImVec2(X(a), Y(mean * a - ha)) };
      dl->AddConvexPolyFilled(quad, 4, IM_COL32(90, 120, 200, 45));
      if (j >= n - 1) break;
    }
    dl->AddLine(ImVec2(X(0), Y(0)), ImVec2(X((double)(n - 1)), Y(mean * (double)(n - 1))), IM_COL32(110, 140, 220, 200), 1.0f);
  }
  // series
  std::vector<ImVec2> total, showdown, other;
  for (size_t i = 0; i < n; i += stride) {
    total.push_back(ImVec2(X((double)i), Y(g.points[i].total)));
    if (ui.graph_split) {
      showdown.push_back(ImVec2(X((double)i), Y(g.points[i].showdown)));
      other.push_back(ImVec2(X((double)i), Y(g.points[i].no_showdown)));
    }
  }
  total.push_back(ImVec2(X((double)(n - 1)), Y(g.points[n - 1].total)));
  if (ui.graph_split) {
    showdown.push_back(ImVec2(X((double)(n - 1)), Y(g.points[n - 1].showdown)));
    other.push_back(ImVec2(X((double)(n - 1)), Y(g.points[n - 1].no_showdown)));
    dl->AddPolyline(showdown.data(), (int)showdown.size(), IM_COL32(90, 210, 110, 255), ImDrawFlags_None, 1.5f);
    dl->AddPolyline(other.data(), (int)other.size(), IM_COL32(230, 90, 90, 255), ImDrawFlags_None, 1.5f);
  }
  dl->AddPolyline(total.data(), (int)total.size(), IM_COL32(120, 170, 255, 255), ImDrawFlags_None, 2.2f);
  dl->PopClipRect();
  dl->AddRect(p0, p1, IM_COL32(110, 110, 120, 255));

  // legend + hover
  float lx = p0.x + 8;
  dl->AddText(ImVec2(lx, p0.y + 4), IM_COL32(120, 170, 255, 255), "total");
  if (ui.graph_split) {
    dl->AddText(ImVec2(lx + 50, p0.y + 4), IM_COL32(90, 210, 110, 255), "showdown");
    dl->AddText(ImVec2(lx + 130, p0.y + 4), IM_COL32(230, 90, 90, 255), "no showdown");
  }
  const ImVec2 mouse = ImGui::GetIO().MousePos;
  if (ImGui::IsItemHovered() && mouse.x >= p0.x && mouse.x <= p1.x) {
    size_t idx = (size_t)std::max(0.0, std::min((double)(n - 1), (double)(mouse.x - p0.x) / (p1.x - p0.x) * (double)(n - 1) + 0.5));
    dl->AddLine(ImVec2(X((double)idx), p0.y), ImVec2(X((double)idx), p1.y), IM_COL32(255, 255, 255, 90));
    ImGui::BeginTooltip();
    ImGui::Text("Hand %zu   %s", idx + 1, FormatDate(g.points[idx].time).c_str());
    ImGui::Text("total %+.1f bb", g.points[idx].total);
    if (ui.graph_split) ImGui::Text("showdown %+.1f / no showdown %+.1f", g.points[idx].showdown, g.points[idx].no_showdown);
    ImGui::EndTooltip();
  }
}

//------------------------------------------------------------------------------
// Reports
//------------------------------------------------------------------------------

static void ApplyReportFilter(Explorer &ex, UiState &ui, const ReportRow &row) {
  bool applied = false;
  if (ex.report_kind == kReportPosition) {
    for (int p = 0; p <= 4; ++p) if (row.label == PositionName(p)) { ex.hand_filter.position = p; applied = true; }
  } else if (ex.report_kind == kReportStartingHand) {
    ex.hand_filter.hole_class = row.label; applied = true;
  } else if (ex.report_kind == kReportPotType) {
    ex.hand_filter.pot_type = atoi(row.key.c_str()); applied = true;
  }
  if (applied) { ex.hands_dirty = true; ex.hands_page = 0; ui.tab_request = kTabHands; }
}

void DrawReportsTab(Explorer &ex, UiState &ui) {
  DrawFilterBar(ex, ui);
  static const ReportKind kinds[] = { kReportPosition, kReportStartingHand, kReportPotType, kReportStackDepth,
                                      kReportStakes, kReportTable, kReportHour, kReportWeekday };
  ImGui::SetNextItemWidth(180);
  if (ImGui::BeginCombo("##kind", ReportName(ex.report_kind))) {
    for (size_t i = 0; i < sizeof(kinds) / sizeof(kinds[0]); ++i) {
      if (ImGui::Selectable(ReportName(kinds[i]), kinds[i] == ex.report_kind)) ex.report_kind = kinds[i];
    }
    ImGui::EndCombo();
  }
  if (ex.report_kind == kReportStartingHand) { ImGui::SameLine(); ImGui::Checkbox("Matrix", &ex.report_as_matrix); }
  ImGui::SameLine();
  ImGui::TextColored(kGrey, "click a row to filter the hands");
  ex.UpdateReport();

  if (ex.report_kind == kReportStartingHand && ex.report_as_matrix) {
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImVec2 avail = ImGui::GetContentRegionAvail();
    const float cell = std::max(24.0f, std::min((avail.x - 4) / 13.0f, (avail.y - 4) / 13.0f));
    ImGui::InvisibleButton("##matrix", ImVec2(cell * 13, cell * 13));
    ImDrawList *dl = ImGui::GetWindowDrawList();
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    const bool hovered = ImGui::IsItemHovered();
    const MatrixCell *hover_cell = NULL;
    for (int r = 0; r < 13; ++r) for (int c = 0; c < 13; ++c) {
      const MatrixCell &m = ex.matrix.cell[r][c];
      const ImVec2 a(origin.x + c * cell, origin.y + r * cell), b(a.x + cell - 1, a.y + cell - 1);
      dl->AddRectFilled(a, b, HeatColor(m.bb_per_100, m.hands));
      dl->AddText(ImVec2(a.x + 3, a.y + 2), IM_COL32(240, 240, 245, 255), m.name.c_str());
      if (m.hands > 0 && cell > 44) {
        char t[24]; snprintf(t, sizeof(t), "%+.0f", m.bb_per_100);
        dl->AddText(ImVec2(a.x + 3, a.y + cell * 0.5f), IM_COL32(230, 230, 235, 255), t);
      }
      if (hovered && mouse.x >= a.x && mouse.x < b.x && mouse.y >= a.y && mouse.y < b.y) hover_cell = &m;
    }
    if (hover_cell) {
      ImGui::BeginTooltip();
      ImGui::Text("%s: %lld hands", hover_cell->name.c_str(), hover_cell->hands);
      ImGui::Text("%+.1f bb  (%+.1f bb/100)", hover_cell->net_bb, hover_cell->bb_per_100);
      ImGui::EndTooltip();
      if (ImGui::IsMouseClicked(0) && hover_cell->hands > 0) {
        ex.hand_filter.hole_class = hover_cell->name; ex.hands_dirty = true; ex.hands_page = 0; ui.tab_request = kTabHands;
      }
    }
    return;
  }

  if (!ImGui::BeginTable("report", 8, ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_ScrollY |
                                      ImGuiTableFlags_SizingFixedFit)) return;
  ImGui::TableSetupScrollFreeze(0, 1);
  ImGui::TableSetupColumn(ReportName(ex.report_kind), 0, 130);
  ImGui::TableSetupColumn("Hands", 0, 55);
  ImGui::TableSetupColumn("Net", 0, 70);
  ImGui::TableSetupColumn("Net bb", 0, 70);
  ImGui::TableSetupColumn("bb/100", 0, 65);
  ImGui::TableSetupColumn("VPIP", 0, 50);
  ImGui::TableSetupColumn("PFR", 0, 50);
  ImGui::TableSetupColumn("SD win", 0, 55);
  ImGui::TableHeadersRow();
  for (size_t i = 0; i < ex.report.size(); ++i) {
    const ReportRow &r = ex.report[i];
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::PushID((int)i);
    if (ImGui::Selectable(r.label.c_str(), false, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap)) {
      ApplyReportFilter(ex, ui, r);
    }
    ImGui::PopID();
    ImGui::TableSetColumnIndex(1); ImGui::Text("%lld", r.hands);
    ImGui::TableSetColumnIndex(2); ImGui::TextColored(r.net >= 0 ? kGreen : kRed, "%.2f", r.net);
    ImGui::TableSetColumnIndex(3); ImGui::TextColored(r.net_bb >= 0 ? kGreen : kRed, "%.1f", r.net_bb);
    ImGui::TableSetColumnIndex(4); ImGui::TextColored(r.bb_per_100 >= 0 ? kGreen : kRed, "%.1f", r.bb_per_100);
    ImGui::TableSetColumnIndex(5); ImGui::TextUnformatted(FormatStat("vpip", r.vpip).c_str());
    ImGui::TableSetColumnIndex(6); ImGui::TextUnformatted(FormatStat("pfr", r.pfr).c_str());
    ImGui::TableSetColumnIndex(7); ImGui::TextUnformatted(FormatStat("vpip", r.sd_win).c_str());
  }
  ImGui::EndTable();
}

//------------------------------------------------------------------------------
// Sessions, tournaments
//------------------------------------------------------------------------------

void DrawSessionsTab(Explorer &ex, UiState &ui) {
  DrawFilterBar(ex, ui);
  ImGui::SetNextItemWidth(110);
  if (ImGui::InputInt("Pause between sessions (minutes)", &ex.session_gap_minutes, 5, 30)) {
    if (ex.session_gap_minutes < 1) ex.session_gap_minutes = 1;
  }
  ex.UpdateSessions();
  ImGui::TextColored(kGrey, "%d sessions", (int)ex.sessions.size());
  if (!ImGui::BeginTable("sessions", 7, ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_ScrollY |
                                        ImGuiTableFlags_SizingFixedFit)) return;
  ImGui::TableSetupScrollFreeze(0, 1);
  ImGui::TableSetupColumn("Start", 0, 120);
  ImGui::TableSetupColumn("Duration", 0, 70);
  ImGui::TableSetupColumn("Hands", 0, 55);
  ImGui::TableSetupColumn("Net", 0, 70);
  ImGui::TableSetupColumn("Net bb", 0, 70);
  ImGui::TableSetupColumn("bb/100", 0, 65);
  ImGui::TableSetupColumn("Hands/hour", 0, 80);
  ImGui::TableHeadersRow();
  for (size_t i = 0; i < ex.sessions.size(); ++i) {
    const Session &s = ex.sessions[i];
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted(FormatDate(s.start).c_str());
    ImGui::TableSetColumnIndex(1);
    const long long minutes = (s.end - s.start) / 60;
    ImGui::Text("%lldh%02lldm", minutes / 60, minutes % 60);
    ImGui::TableSetColumnIndex(2); ImGui::Text("%lld", s.hands);
    ImGui::TableSetColumnIndex(3); ImGui::TextColored(s.net >= 0 ? kGreen : kRed, "%.2f", s.net);
    ImGui::TableSetColumnIndex(4); ImGui::TextColored(s.net_bb >= 0 ? kGreen : kRed, "%.1f", s.net_bb);
    ImGui::TableSetColumnIndex(5); ImGui::TextColored(s.bb_per_100 >= 0 ? kGreen : kRed, "%.1f", s.bb_per_100);
    ImGui::TableSetColumnIndex(6); ImGui::Text("%.0f", s.hands_per_hour);
  }
  ImGui::EndTable();
}

void DrawTournamentsTab(Explorer &ex, UiState &ui) {
  DrawFilterBar(ex, ui);
  ex.UpdateTournaments();
  ImGui::TextColored(kGrey, "%d tournaments. Buy-ins and prizes are not recorded, so there is no ROI / ITM; "
                            "results are in chips.", (int)ex.tournaments.size());
  if (!ImGui::BeginTable("tournaments", 8, ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_ScrollY |
                                           ImGuiTableFlags_SizingFixedFit)) return;
  ImGui::TableSetupScrollFreeze(0, 1);
  ImGui::TableSetupColumn("Tournament", 0, 100);
  ImGui::TableSetupColumn("Start", 0, 120);
  ImGui::TableSetupColumn("Hands", 0, 55);
  ImGui::TableSetupColumn("Level", 0, 45);
  ImGui::TableSetupColumn("Start stack", 0, 90);
  ImGui::TableSetupColumn("Last stack", 0, 90);
  ImGui::TableSetupColumn("Chips", 0, 80);
  ImGui::TableSetupColumn("Last stack (bb)", 0, 100);
  ImGui::TableHeadersRow();
  for (size_t i = 0; i < ex.tournaments.size(); ++i) {
    const TournamentRow &t = ex.tournaments[i];
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted(t.id.c_str());
    ImGui::TableSetColumnIndex(1); ImGui::TextUnformatted(FormatDate(t.start).c_str());
    ImGui::TableSetColumnIndex(2); ImGui::Text("%lld", t.hands);
    ImGui::TableSetColumnIndex(3); ImGui::Text("%d", t.max_level);
    ImGui::TableSetColumnIndex(4); ImGui::Text("%.0f  (%.0f bb)", t.start_stack, t.start_bb);
    ImGui::TableSetColumnIndex(5); ImGui::Text("%.0f", t.final_stack);
    ImGui::TableSetColumnIndex(6); ImGui::TextColored(t.net_chips >= 0 ? kGreen : kRed, "%+.0f", t.net_chips);
    ImGui::TableSetColumnIndex(7); ImGui::Text("%.1f", t.final_bb);
  }
  ImGui::EndTable();
}

//------------------------------------------------------------------------------
// Dialogs
//------------------------------------------------------------------------------

void DrawPanelPopups(Explorer &ex, UiState &ui) {
  if (ui.open_filter_popup) { ImGui::OpenPopup("Hand filters"); ui.open_filter_popup = false; }
  if (ui.open_export_popup) { ImGui::OpenPopup("Export"); ui.open_export_popup = false; }
  if (ui.open_import_popup) { ImGui::OpenPopup("Import hand histories"); ui.open_import_popup = false; }

  ImGui::SetNextWindowSize(ImVec2(560, 0), ImGuiCond_Appearing);
  if (ImGui::BeginPopupModal("Hand filters", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
    EscapeCloses();
    ohstats::HandFilter &f = ex.hand_filter;
    ImGui::TextColored(kGrey, "Apply to the hand list, the graph and the reports\n(for the selected player, else the hero).");
    int position = f.position + 1;
    ComboList("Position", &position, kPositions, 6, 150); f.position = position - 1;
    InputString("Hole cards", &f.hole_class, "AKs, QQ, T9o", 150);
    InputString("Board contains", &f.board_has, "Ah  or  Ah Kd", 150);
    ComboList("Pot type", &f.pot_type, kPotTypes, 5, 150);
    ComboList("Result", &f.result, kResults, 3, 150);
    ComboList("Showdown", &f.showdown, kShowdowns, 3, 150);
    ImGui::SetNextItemWidth(80); ImGui::InputDouble("Pot min (bb)", &f.min_pot_bb, 0, 0, "%.1f");
    ImGui::SameLine(); ImGui::SetNextItemWidth(80); ImGui::InputDouble("max##pot", &f.max_pot_bb, 0, 0, "%.1f");
    ImGui::SetNextItemWidth(80); ImGui::InputDouble("Eff. stack min (bb)", &f.min_eff_bb, 0, 0, "%.1f");
    ImGui::SameLine(); ImGui::SetNextItemWidth(80); ImGui::InputDouble("max##stack", &f.max_eff_bb, 0, 0, "%.1f");
    ImGui::Checkbox("Starred hands only", &f.marked_only);
    InputString("Tag", &f.tag, "bluff", 150);
    InputString("Note contains", &f.text, "text", 150);
    if (ImGui::Button("Apply")) { ex.hands_dirty = true; ex.hands_page = 0; ImGui::CloseCurrentPopup(); }
    ImGui::SameLine();
    if (ImGui::Button("Clear all")) ex.ClearHandFilter();
    ImGui::SameLine();
    if (ImGui::Button("Close")) { ex.hands_dirty = true; ImGui::CloseCurrentPopup(); }
    ImGui::EndPopup();
  }

  ImGui::SetNextWindowSize(ImVec2(560, 0), ImGuiCond_Appearing);
  if (ImGui::BeginPopupModal("Export", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
    EscapeCloses();
    static const char *kTitles[] = { "", "Players table (CSV)", "Current report (CSV)", "Hand list (CSV)",
                                     "Hands as PokerStars text" };
    ImGui::Text("%s", kTitles[ui.export_kind >= 1 && ui.export_kind <= 4 ? ui.export_kind : 0]);
    ImGui::TextColored(kGrey, "The hand exports use the current filters (all pages).");
    ImGui::SetNextItemWidth(-1);
    ImGui::InputText("##exportpath", ui.export_path, sizeof(ui.export_path));
    if (!ui.export_message.empty()) ImGui::TextWrapped("%s", ui.export_message.c_str());
    if (ImGui::Button("Export")) {
      std::string error;
      long long n = -1;
      switch (ui.export_kind) {
        case 1: n = ExportPlayersCsv(ex, ui.export_path, &error); break;
        case 2: n = ExportReportCsv(ex, ui.export_path, &error); break;
        case 3: n = ExportHandListCsv(&ex, ui.export_path, &error); break;
        case 4: n = ExportHandsText(&ex, ui.export_path, 200000, &error); break;
      }
      char b[900];
      if (n >= 0) snprintf(b, sizeof(b), "%lld written to %s", n, ui.export_path);
      else        snprintf(b, sizeof(b), "Error: %s", error.c_str());
      ui.export_message = b;
    }
    ImGui::SameLine();
    if (ImGui::Button("Close")) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
  }

  ImGui::SetNextWindowSize(ImVec2(600, 0), ImGuiCond_Appearing);
  if (ImGui::BeginPopupModal("Import hand histories", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
    EscapeCloses();
    ImGui::TextWrapped("Imports PokerStars-style hand history files (cash games and tournaments) into the open database. "
                       "Give a file or a folder (all .txt files below it). Hands that are already in the database are skipped.");
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##importpath", "path of a file or folder", ui.import_path, sizeof(ui.import_path));
    ImGui::SetNextItemWidth(160);
    ImGui::InputText("Site name", ui.import_site, sizeof(ui.import_site));
    if (!ui.import_message.empty()) ImGui::TextWrapped("%s", ui.import_message.c_str());
    if (ImGui::Button("Import") && ex.IsOpen()) {
      ImportResult result;
      std::string site = ui.import_site;
      for (size_t i = 0; i < site.size(); ++i) site[i] = (char)tolower((unsigned char)site[i]);
      ImportPath(&ex.db, ui.import_path, site.empty() ? "stars" : site, &result);
      char b[256];
      snprintf(b, sizeof(b), "%d hands found: %d imported, %d already there, %d failed.", result.hands_found,
               result.imported, result.duplicates, result.failed);
      ui.import_message = b;
      for (size_t i = 0; i < result.errors.size() && i < 5; ++i) ui.import_message += "\n" + result.errors[i];
      ex.players_dirty = true; ex.hands_dirty = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Close")) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
  }
}

}  // namespace explorer
