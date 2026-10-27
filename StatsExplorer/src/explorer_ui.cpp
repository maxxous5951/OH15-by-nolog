//******************************************************************************
//
// This file is part of the OpenHoldem project
//    Licensed under GPL v3: http://www.gnu.org/licenses/gpl.html
//
//******************************************************************************
//
// Purpose: Dear ImGui interface of the stats explorer, see explorer_ui.h
//
//******************************************************************************

#include "explorer_ui.h"
#include <filesystem>
#include <algorithm>
#include <system_error>
#include <vector>

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <string>

#include "demo_data.h"
#include "ui_panels.h"

namespace explorer {

namespace {

const ImU32 kTableGreen = IM_COL32(24, 92, 56, 255);
const ImU32 kTableEdge  = IM_COL32(70, 48, 28, 255);
const ImVec4 kGreen(0.35f, 0.85f, 0.45f, 1.0f);
const ImVec4 kRed(0.95f, 0.40f, 0.40f, 1.0f);
const ImVec4 kGrey(0.60f, 0.60f, 0.65f, 1.0f);

bool ComboStrings(const char *label, int *index, const std::vector<std::string> &items, float width) {
  bool changed = false;
  if (width > 0) ImGui::SetNextItemWidth(width);
  const char *preview = (*index >= 0 && *index < (int)items.size()) ? items[*index].c_str() : "";
  if (ImGui::BeginCombo(label, preview)) {
    for (int i = 0; i < (int)items.size(); ++i) {
      bool selected = (i == *index);
      if (ImGui::Selectable(items[i].c_str(), selected)) {
        if (*index != i) changed = true;
        *index = i;
      }
      if (selected) ImGui::SetItemDefaultFocus();
    }
    ImGui::EndCombo();
  }
  return changed;
}

ImU32 SuitColor(char suit) {
  switch (suit) {
    case 's': return IM_COL32(25, 25, 25, 255);
    case 'h': return IM_COL32(200, 30, 30, 255);
    case 'd': return IM_COL32(30, 90, 200, 255);
    case 'c': return IM_COL32(20, 130, 50, 255);
  }
  return IM_COL32(80, 80, 80, 255);
}

// Card "As" at the given position; an empty string draws the back of a card
void DrawCard(ImDrawList *dl, ImVec2 pos, ImVec2 size, const std::string &card) {
  const ImVec2 end(pos.x + size.x, pos.y + size.y);
  if (card.size() < 2) {
    dl->AddRectFilled(pos, end, IM_COL32(40, 70, 150, 255), 4.0f);
    dl->AddRect(pos, end, IM_COL32(220, 220, 240, 255), 4.0f);
    return;
  }
  dl->AddRectFilled(pos, end, IM_COL32(245, 245, 245, 255), 4.0f);
  dl->AddRect(pos, end, IM_COL32(30, 30, 30, 255), 4.0f);
  char rank[3] = { card[0], 0, 0 };
  if (card[0] == 'T') { rank[0] = '1'; rank[1] = '0'; }
  const ImU32 color = SuitColor(card[1]);
  dl->AddText(ImVec2(pos.x + 4, pos.y + 2), color, rank);
  char suit[2] = { card[1], 0 };
  const ImVec2 rank_size = ImGui::CalcTextSize(rank);
  dl->AddText(ImVec2(pos.x + 4, pos.y + 4 + rank_size.y), color, suit);
}

std::string CardsText(const std::string &hole) {
  if (hole.size() < 4) return "";
  return hole.substr(0, hole.size() / 2) + " " + hole.substr(hole.size() / 2);
}

//------------------------------------------------------------------------------
// Menu, filters, status
//------------------------------------------------------------------------------

void DrawMenu(Explorer &ex, UiState &ui) {
  if (!ImGui::BeginMenuBar()) return;
  if (ImGui::BeginMenu("File")) {
    if (ImGui::MenuItem("Open database...")) {
      snprintf(ui.path_buffer, sizeof(ui.path_buffer), "%s", ex.path.c_str());
      ui.popup_error.clear();
      {
        std::filesystem::path p = std::filesystem::u8path(ex.path.empty() ? "." : ex.path);
        ui.browse_dir = ex.path.empty() ? std::string() : p.parent_path().u8string();
      }
      ui.open_path_popup = true;
    }
    if (ImGui::MenuItem("Create demo database...")) {
      ui.popup_error.clear();
      ui.open_demo_popup = true;
    }
    ImGui::Separator();
    if (ImGui::MenuItem("Import hand histories...", NULL, false, ex.IsOpen())) {
      ui.import_message.clear();
      ui.open_import_popup = true;
    }
    struct ExportItem { const char *label; int kind; const char *file; };
    static const ExportItem exports[] = { { "Export players table (CSV)...", 1, "players.csv" },
      { "Export current report (CSV)...", 2, "report.csv" }, { "Export hand list (CSV)...", 3, "hands.csv" },
      { "Export hands (text)...", 4, "hands.txt" } };
    for (size_t i = 0; i < 4; ++i) {
      if (ImGui::MenuItem(exports[i].label, NULL, false, ex.IsOpen())) {
        ui.export_kind = exports[i].kind;
        snprintf(ui.export_path, sizeof(ui.export_path), "%s", exports[i].file);
        ui.export_message.clear();
        ui.open_export_popup = true;
      }
    }
    ImGui::Separator();
    if (ImGui::MenuItem("Quit")) ui.quit_requested = true;
    ImGui::EndMenu();
  }
  if (ImGui::BeginMenu("View")) {
    ImGui::MenuItem("Reload automatically when OpenHoldem records hands", NULL, &ui.auto_refresh);
    if (ImGui::MenuItem("Reload now")) { ex.players_dirty = true; ex.hands_dirty = true; }
    ImGui::EndMenu();
  }
  ImGui::EndMenuBar();
}


// Simple folder browser: directories and .db/.sqlite files of ui.browse_dir.
static void DrawDbBrowser(UiState &ui) {
  namespace fs = std::filesystem;
  std::error_code ec;
  fs::path dir = fs::u8path(ui.browse_dir.empty() ? "." : ui.browse_dir);
  dir = fs::absolute(dir, ec);
  if (ec || !fs::is_directory(dir, ec)) dir = fs::current_path(ec);
  ui.browse_dir = dir.u8string();
  ImGui::Text("Folder: %s", ui.browse_dir.c_str());
  std::vector<std::string> dirs, files;
  for (fs::directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end; it.increment(ec)) {
    std::error_code e2;
    std::string name = it->path().filename().u8string();
    if (it->is_directory(e2)) { if (!name.empty() && name[0] != '.') dirs.push_back(name); }
    else {
      std::string ext = it->path().extension().u8string();
      std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char ch) { return (char)tolower(ch); });
      if (ext == ".db" || ext == ".sqlite" || ext == ".sqlite3") files.push_back(name);
    }
  }
  std::sort(dirs.begin(), dirs.end());
  std::sort(files.begin(), files.end());
  if (ImGui::BeginChild("##browser", ImVec2(0, 220), true)) {
    if (dir.has_parent_path() && dir.parent_path() != dir && ImGui::Selectable("[..]")) {
      ui.browse_dir = dir.parent_path().u8string();
    }
    for (size_t i = 0; i < dirs.size(); ++i) {
      ImGui::PushID((int)i);
      std::string label = "[" + dirs[i] + "]";
      if (ImGui::Selectable(label.c_str())) { ui.browse_dir = (dir / fs::u8path(dirs[i])).u8string(); }
      ImGui::PopID();
    }
    for (size_t i = 0; i < files.size(); ++i) {
      ImGui::PushID((int)(1000 + i));
      if (ImGui::Selectable(files[i].c_str())) {
        snprintf(ui.path_buffer, sizeof(ui.path_buffer), "%s", (dir / fs::u8path(files[i])).u8string().c_str());
      }
      ImGui::PopID();
    }
    if (dirs.empty() && files.empty()) ImGui::TextDisabled("(no sub-folder or .db file)");
  }
  ImGui::EndChild();
}

void DrawPopups(Explorer &ex, UiState &ui) {
  if (ui.open_path_popup)    { ImGui::OpenPopup("Open database");   ui.open_path_popup = false; }
  if (ui.open_demo_popup)    { ImGui::OpenPopup("Demo database");   ui.open_demo_popup = false; }
  if (ui.open_columns_popup) { ImGui::OpenPopup("Columns");         ui.open_columns_popup = false; }

  DrawPanelPopups(ex, ui);
  ImGui::SetNextWindowSize(ImVec2(560, 0), ImGuiCond_Appearing);
  if (ImGui::BeginPopupModal("Open database", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
    ImGui::TextUnformatted("Path of the stats database (oh_stats.db, created by OpenHoldem):");
    ImGui::SetNextItemWidth(-1);
    bool enter = ImGui::InputText("##path", ui.path_buffer, sizeof(ui.path_buffer),
                                  ImGuiInputTextFlags_EnterReturnsTrue);
    DrawDbBrowser(ui);
    if (!ui.popup_error.empty()) ImGui::TextColored(kRed, "%s", ui.popup_error.c_str());
    if (ImGui::Button("Open") || enter) {
      std::string error;
      if (ex.Open(ui.path_buffer, &error)) ImGui::CloseCurrentPopup();
      else ui.popup_error = error;
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
  }

  ImGui::SetNextWindowSize(ImVec2(560, 0), ImGuiCond_Appearing);
  if (ImGui::BeginPopupModal("Demo database", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
    ImGui::TextWrapped("Creates a database with random sample hands (6-max cash games, 7 opponents "
                       "with different styles) to try the explorer. Choose a NEW file name.");
    ImGui::SetNextItemWidth(-1);
    ImGui::InputText("##demo", ui.demo_path_buffer, sizeof(ui.demo_path_buffer));
    if (!ui.popup_error.empty()) ImGui::TextColored(kRed, "%s", ui.popup_error.c_str());
    if (ImGui::Button("Create and open")) {
      std::string error;
      FILE *existing = fopen(ui.demo_path_buffer, "rb");
      if (existing) {
        fclose(existing);
        ui.popup_error = "This file already exists. Choose a new name (the file is not overwritten).";
      } else if (GenerateDemoDb(ui.demo_path_buffer, 3000, 1, &error) && ex.Open(ui.demo_path_buffer, &error)) {
        ImGui::CloseCurrentPopup();
      } else {
        ui.popup_error = error;
      }
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
  }

  ImGui::SetNextWindowSize(ImVec2(620, 520), ImGuiCond_Appearing);
  if (ImGui::BeginPopupModal("Columns", NULL, 0)) {
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
    ImGui::TextUnformatted("Presets:");
    const std::vector<std::string> &presets = PresetNames();
    for (size_t i = 0; i < presets.size(); ++i) {
      ImGui::SameLine();
      if (ImGui::SmallButton(presets[i].c_str())) ex.SetColumns(PresetColumns(presets[i]));
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("None")) ex.SetColumns(std::vector<std::string>());
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##colfilter", "filter, e.g. 3bet", ui.column_filter, sizeof(ui.column_filter));
    if (ImGui::BeginChild("##columnlist", ImVec2(0, -ImGui::GetFrameHeightWithSpacing()), ImGuiChildFlags_Borders)) {
      const std::vector<Section> &sections = Sections();
      for (size_t s = 0; s < sections.size(); ++s) {
        bool header_done = false;
        for (size_t i = 0; i < sections[s].stats.size(); ++i) {
          const std::string &stat = sections[s].stats[i];
          std::string label = StatLabel(stat);
          if (ui.column_filter[0] &&
              stat.find(ui.column_filter) == std::string::npos &&
              label.find(ui.column_filter) == std::string::npos) continue;
          if (!header_done) { ImGui::SeparatorText(sections[s].title.c_str()); header_done = true; }
          bool on = ex.HasColumn(stat);
          if (ImGui::Checkbox((label + "##" + stat).c_str(), &on)) {
            if (on) ex.AddColumn(stat); else ex.RemoveColumn(stat);
          }
          if (ImGui::IsItemHovered()) ImGui::SetTooltip("symbol: pt_%s", stat.c_str());
        }
      }
    }
    ImGui::EndChild();
    if (ImGui::Button("Close")) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
  }
}

void DrawFilters(Explorer &ex, UiState &ui) {
  Filters &f = ex.filters;
  bool changed = false;
  std::vector<std::string> sites;
  sites.push_back("All sites");
  for (size_t i = 0; i < ex.sites.size(); ++i) sites.push_back(ex.sites[i]);
  changed |= ComboStrings("##site", &f.site_index, sites, 120);
  ImGui::SameLine();
  static const char *kGameNames[] = { "Cash games", "Tournaments", "Both" };
  std::vector<std::string> games(kGameNames, kGameNames + 3);
  changed |= ComboStrings("##game", &f.game_type, games, 120);
  ImGui::SameLine();
  changed |= ComboStrings("##period", &f.period_index, PeriodNames(), 130);
  ImGui::SameLine();
  int position_index = f.position + 1;
  std::vector<std::string> positions;
  positions.push_back("All positions");
  for (int p = 0; p <= 4; ++p) positions.push_back(PositionName(p));
  if (ComboStrings("##position", &position_index, positions, 120)) { f.position = position_index - 1; changed = true; }
  ImGui::SameLine();
  ImGui::SetNextItemWidth(110);
  if (ImGui::InputInt("Min hands", &f.min_hands, 1, 10)) { if (f.min_hands < 1) f.min_hands = 1; changed = true; }
  ImGui::SameLine();
  ImGui::SetNextItemWidth(110);
  if (ImGui::InputInt("Last hands", &f.last_hands, 10, 100)) { if (f.last_hands < 0) f.last_hands = 0; changed = true; }
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("Only the last N hands of each player (0 = all).\nLike the \"500hands\" symbols.");

  ImGui::SetNextItemWidth(220);
  if (ImGui::InputTextWithHint("##name", "Search player...", ui.name_buffer, sizeof(ui.name_buffer))) {
    f.name = ui.name_buffer;
    changed = true;
  }
  ImGui::SameLine();
  if (ImGui::Button("Columns...")) ui.open_columns_popup = true;
  ImGui::SameLine();
  if (ImGui::Button("Reload")) changed = true;
  if (changed) { ex.players_dirty = true; ex.hands_dirty = true; if (ex.hands_page) ex.hands_page = 0; }
}

void DrawStatusBar(Explorer &ex) {
  ImGui::Separator();
  if (!ex.error.empty()) ImGui::TextColored(kRed, "%s", ex.error.c_str());
  else ImGui::TextColored(kGrey, "%s  |  %lld hands in the database  |  %d players listed",
                          ex.path.c_str(), ex.total_hands_in_db, (int)ex.players.size());
}

//------------------------------------------------------------------------------
// Player table
//------------------------------------------------------------------------------

void DrawPlayers(Explorer &ex, UiState &ui) {
  ui.has_player_row = false;
  const int n_columns = 4 + (int)ex.columns.size();
  ImGuiTableFlags flags = ImGuiTableFlags_Resizable | ImGuiTableFlags_Hideable | ImGuiTableFlags_Sortable |
                          ImGuiTableFlags_ScrollX | ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg |
                          ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingFixedFit |
                          ImGuiTableFlags_SortTristate * 0;
  if (!ImGui::BeginTable("players", n_columns, flags, ImVec2(0, 0))) return;
  ImGui::TableSetupScrollFreeze(1, 1);
  ImGui::TableSetupColumn("Player", ImGuiTableColumnFlags_WidthFixed, 140, 1);
  ImGui::TableSetupColumn("Hands", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_PreferSortDescending |
                                   ImGuiTableColumnFlags_DefaultSort, 56, 2);
  ImGui::TableSetupColumn("Won", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_PreferSortDescending, 70, 3);
  ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoSort, 100, 4);
  for (size_t i = 0; i < ex.columns.size(); ++i) {
    ImGui::TableSetupColumn(StatLabel(ex.columns[i]).c_str(),
      ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_PreferSortDescending, 0.0f, 100 + (ImGuiID)i);
  }
  ImGui::TableHeadersRow();

  if (ImGuiTableSortSpecs *specs = ImGui::TableGetSortSpecs()) {
    if (specs->SpecsDirty && specs->SpecsCount > 0) {
      ImGuiID id = specs->Specs[0].ColumnUserID;
      ex.sort_column = id == 1 ? -2 : id == 2 ? -1 : id == 3 ? -3 : (id >= 100 ? (int)id - 100 : -1);
      ex.sort_descending = specs->Specs[0].SortDirection == ImGuiSortDirection_Descending;
      ex.SortPlayers();
      specs->SpecsDirty = false;
    }
  }

  ImGuiListClipper clipper;
  clipper.Begin((int)ex.players.size());
  while (clipper.Step()) {
    for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
      const ohstats::PlayerRow &p = ex.players[row];
      ImGui::TableNextRow();
      ImGui::TableSetColumnIndex(0);
      bool selected = (p.name == ex.selected_player);
      ImGui::PushID(row);
      if (ImGui::Selectable(p.name.c_str(), selected,
                            ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap)) {
        ex.SelectPlayer(p.name);
        ui.tab_request = 1;
      }
      if (row == clipper.DisplayStart || !ui.has_player_row) {
        ui.has_player_row = true;
        ui.player_row_min = ImGui::GetItemRectMin();
        ui.player_row_max = ImGui::GetItemRectMax();
      }
      ImGui::PopID();
      ImGui::TableSetColumnIndex(1);
      ImGui::Text("%lld", p.hands);
      ImGui::TableSetColumnIndex(2);
      ImGui::TextColored(p.won >= 0 ? kGreen : kRed, "%.2f", p.won);
      ImGui::TableSetColumnIndex(3);
      {
        const PlayerType type = ex.TypeOf(p.name);
        ImGui::TextColored(TypeColor(type), "%s", PlayerTypeName(type));
      }
      for (size_t c = 0; c < ex.columns.size() && c < p.values.size(); ++c) {
        ImGui::TableSetColumnIndex(4 + (int)c);
        const std::string text = FormatStat(ex.columns[c], p.values[c]);
        if (IsUndefined(p.values[c])) ImGui::TextColored(kGrey, "%s", text.c_str());
        else ImGui::TextUnformatted(text.c_str());
        if (ImGui::IsItemHovered() && !IsUndefined(p.values[c]) && c < p.opportunities.size() &&
            HasConfidenceInterval(ex.columns[c]) && p.opportunities[c] > 0) {
          double lo, hi;
          WilsonInterval(p.values[c] * p.opportunities[c], p.opportunities[c], &lo, &hi);
          ImGui::SetTooltip("%s of %s\n%.0f opportunities\n95%% confidence interval: %.1f - %.1f",
                            StatLabel(ex.columns[c]).c_str(), p.name.c_str(), p.opportunities[c], lo * 100, hi * 100);
        }
      }
    }
  }
  ImGui::EndTable();
}

//------------------------------------------------------------------------------
// Player sheet
//------------------------------------------------------------------------------

void DrawPlayerSheet(Explorer &ex) {
  if (ex.selected_player.empty()) {
    ImGui::TextWrapped("Click a player in the table to see all of his statistics.");
    return;
  }
  ImGui::Text("%s", ex.selected_player.c_str());
  ImGui::SameLine();
  ImGui::TextColored(kGrey, "  (filters of the table apply)");
  DrawHud(ex);
  DrawLeaks(ex);
  const std::vector<Section> &sections = Sections();
  for (size_t s = 0; s < sections.size(); ++s) {
    if (sections[s].stats.empty()) continue;
    ImGui::SetNextItemOpen(s < 3, ImGuiCond_Once);
    if (!ImGui::CollapsingHeader(sections[s].title.c_str())) continue;
    ImGui::PushID((int)s);
    if (ImGui::BeginTable("sheet", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
      ImGui::TableSetupColumn("a", ImGuiTableColumnFlags_WidthStretch, 3.0f);
      ImGui::TableSetupColumn("b", ImGuiTableColumnFlags_WidthStretch, 1.0f);
      ImGui::TableSetupColumn("c", ImGuiTableColumnFlags_WidthStretch, 3.0f);
      ImGui::TableSetupColumn("d", ImGuiTableColumnFlags_WidthStretch, 1.0f);
      for (size_t i = 0; i < sections[s].stats.size(); ++i) {
        const std::string &stat = sections[s].stats[i];
        if (i % 2 == 0) ImGui::TableNextRow();
        ImGui::TableSetColumnIndex((int)(i % 2) * 2);
        ImGui::TextUnformatted(StatLabel(stat).c_str());
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("symbol: pt_%s", stat.c_str());
        ImGui::TableSetColumnIndex((int)(i % 2) * 2 + 1);
        const double value = ex.SheetValue(stat);
        const std::string text = FormatStat(stat, value);
        if (IsUndefined(value)) ImGui::TextColored(kGrey, "%s", text.c_str());
        else ImGui::TextUnformatted(text.c_str());
        double num = 0, den = 0;
        if (!IsUndefined(value) && HasConfidenceInterval(stat) && ex.SheetFraction(stat, &num, &den) && den > 0) {
          ImGui::SameLine();
          ImGui::TextColored(kGrey, "(%.0f)", den);
          if (ImGui::IsItemHovered()) {
            double lo, hi;
            WilsonInterval(num, den, &lo, &hi);
            ImGui::SetTooltip("%.0f opportunities\n95%% confidence interval: %.1f - %.1f", den, lo * 100, hi * 100);
          }
        }
      }
      ImGui::EndTable();
    }
    ImGui::PopID();
  }
}

//------------------------------------------------------------------------------
// Hand list
//------------------------------------------------------------------------------

void DrawHandList(Explorer &ex, UiState &ui) {
  ui.has_hand_row = false;
  DrawFilterBar(ex, ui);
  if (ex.selected_player.empty()) ImGui::Text("Hands (cards and result: hero)");
  else ImGui::Text("Hands of %s", ex.selected_player.c_str());
  ImGui::SameLine();
  const long long pages = ex.hands_total > 0 ? (ex.hands_total - 1) / Explorer::kHandsPerPage + 1 : 1;
  ImGui::TextColored(kGrey, "  %lld hands, page %d / %lld", ex.hands_total, ex.hands_page + 1, pages);
  ImGui::SameLine();
  if (ImGui::SmallButton("<") && ex.hands_page > 0)   { --ex.hands_page; ex.hands_dirty = true; }
  ImGui::SameLine();
  if (ImGui::SmallButton(">") && ex.hands_page + 1 < pages) { ++ex.hands_page; ex.hands_dirty = true; }

  ImGuiTableFlags flags = ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollX | ImGuiTableFlags_ScrollY |
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingFixedFit;
  if (!ImGui::BeginTable("hands", 9, flags, ImVec2(0, 0))) return;
  ImGui::TableSetupScrollFreeze(0, 1);
  ImGui::TableSetupColumn("Date", ImGuiTableColumnFlags_WidthFixed, 120);
  ImGui::TableSetupColumn("Site", ImGuiTableColumnFlags_WidthFixed, 50);
  ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 40);
  ImGui::TableSetupColumn("Blinds", ImGuiTableColumnFlags_WidthFixed, 70);
  ImGui::TableSetupColumn("Pos", ImGuiTableColumnFlags_WidthFixed, 34);
  ImGui::TableSetupColumn("Cards", ImGuiTableColumnFlags_WidthFixed, 56);
  ImGui::TableSetupColumn("Board", ImGuiTableColumnFlags_WidthFixed, 130);
  ImGui::TableSetupColumn("Result", ImGuiTableColumnFlags_WidthFixed, 70);
  ImGui::TableSetupColumn("Pot", ImGuiTableColumnFlags_WidthFixed, 60);
  ImGui::TableHeadersRow();
  ImGuiListClipper clipper;
  clipper.Begin((int)ex.hands.size());
  while (clipper.Step()) {
    for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
      const ohstats::HandRow &h = ex.hands[row];
      ImGui::TableNextRow();
      ImGui::TableSetColumnIndex(0);
      ImGui::PushID(row);
      char label[64];
      snprintf(label, sizeof(label), "%s%s", h.marked ? "* " : "", FormatDate(h.played_at).c_str());
      if (ImGui::Selectable(label, h.pk == ex.selected_hand,
                            ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap)) {
        if (ex.SelectHand(h.pk)) ui.tab_request = 3;
      }
      if (!ui.has_hand_row) {
        ui.has_hand_row = true;
        ui.hand_row_min = ImGui::GetItemRectMin();
        ui.hand_row_max = ImGui::GetItemRectMax();
      }
      ImGui::PopID();
      ImGui::TableSetColumnIndex(1); ImGui::TextUnformatted(h.site.c_str());
      ImGui::TableSetColumnIndex(2); ImGui::TextUnformatted(h.tournament ? "MTT" : "Cash");
      ImGui::TableSetColumnIndex(3);
      ImGui::Text("%s/%s", FormatAmount(h.sblind, h.tournament).c_str(), FormatAmount(h.bblind, h.tournament).c_str());
      ImGui::TableSetColumnIndex(4); ImGui::TextUnformatted(h.has_player ? PositionName(h.player_position) : "");
      ImGui::TableSetColumnIndex(5); ImGui::TextUnformatted(CardsText(h.has_player ? h.player_hole : "").c_str());
      ImGui::TableSetColumnIndex(6); ImGui::TextUnformatted(h.board.c_str());
      ImGui::TableSetColumnIndex(7);
      if (h.has_player) ImGui::TextColored(h.player_net >= 0 ? kGreen : kRed, "%s", FormatAmount(h.player_net, h.tournament).c_str());
      ImGui::TableSetColumnIndex(8); ImGui::TextUnformatted(FormatAmount(h.pot, h.tournament).c_str());
    }
  }
  ImGui::EndTable();
}

//------------------------------------------------------------------------------
// Hand: replayer + text
//------------------------------------------------------------------------------

void DrawReplayTable(Explorer &ex, const ReplayFrame &frame, float height) {
  const hh::Hand &h = ex.hand;
  ImVec2 origin = ImGui::GetCursorScreenPos();
  ImVec2 size(ImGui::GetContentRegionAvail().x, height);
  ImGui::InvisibleButton("##replay_area", size);
  ImDrawList *dl = ImGui::GetWindowDrawList();
  const ImVec2 center(origin.x + size.x * 0.5f, origin.y + size.y * 0.5f);
  const float rx = size.x * 0.5f - 70.0f, ry = size.y * 0.5f - 38.0f;
  if (rx < 40 || ry < 30) return;
  dl->AddEllipseFilled(center, ImVec2(rx + 14, ry + 14), kTableEdge, 0, 64);
  dl->AddEllipseFilled(center, ImVec2(rx, ry), kTableGreen, 0, 64);

  // Board and pot
  const ImVec2 card_size(34, 46);
  const float board_x = center.x - (5 * card_size.x + 4 * 6) * 0.5f;
  for (int i = 0; i < 5; ++i) {
    ImVec2 p(board_x + i * (card_size.x + 6), center.y - card_size.y * 0.5f - 6);
    if (i < frame.board_cards && i < h.board_count) DrawCard(dl, p, card_size, h.board[i]);
    else dl->AddRect(p, ImVec2(p.x + card_size.x, p.y + card_size.y), IM_COL32(255, 255, 255, 40), 4.0f);
  }
  char pot_text[64];
  snprintf(pot_text, sizeof(pot_text), "Pot %s", FormatAmount(frame.pot, h.tournament).c_str());
  ImVec2 pot_size = ImGui::CalcTextSize(pot_text);
  dl->AddText(ImVec2(center.x - pot_size.x * 0.5f, center.y + card_size.y * 0.5f + 2), IM_COL32(255, 235, 150, 255), pot_text);

  // Seats around the table, the hero (or seat 1) at the bottom
  const int n = h.nseats > 0 ? h.nseats : 6;
  const int bottom = (h.hero >= 0 && h.hero < n) ? h.hero : 0;
  const float kPi = 3.14159265f;
  for (int seat = 0; seat < n && seat < hh::kMaxSeats; ++seat) {
    if (!h.in_hand[seat]) continue;
    const float theta = kPi * 0.5f + 2.0f * kPi * (float)(seat - bottom) / (float)n;
    const ImVec2 c(center.x + (rx + 8) * cosf(theta), center.y + (ry + 8) * sinf(theta));
    const ImVec2 box(110, 44);
    const ImVec2 tl(c.x - box.x * 0.5f, c.y - box.y * 0.5f), br(c.x + box.x * 0.5f, c.y + box.y * 0.5f);
    const bool folded = frame.folded[seat];
    const bool active = (frame.actor == seat);
    dl->AddRectFilled(tl, br, folded ? IM_COL32(40, 40, 46, 230) : IM_COL32(28, 30, 40, 245), 6.0f);
    dl->AddRect(tl, br, active ? IM_COL32(255, 210, 60, 255) : IM_COL32(120, 120, 140, 255), 6.0f, 0, active ? 2.5f : 1.0f);
    const ImU32 text_color = folded ? IM_COL32(130, 130, 140, 255) : IM_COL32(235, 235, 240, 255);
    dl->AddText(ImVec2(tl.x + 6, tl.y + 4), text_color, h.name[seat].c_str());
    dl->AddText(ImVec2(tl.x + 6, tl.y + 24), IM_COL32(150, 220, 160, folded ? 120 : 255),
                FormatAmount(frame.stack[seat], h.tournament).c_str());
    if (!IsUndefined(ex.equity[seat]) && !folded) {
      char eq[24];
      snprintf(eq, sizeof(eq), "%.0f%%", ex.equity[seat] * 100.0);
      const ImVec2 es = ImGui::CalcTextSize(eq);
      dl->AddText(ImVec2(br.x - es.x - 6, br.y - es.y - 5), IM_COL32(255, 220, 120, 255), eq);
    }
    // Hole cards (known cards only; hidden: nothing for folded players)
    if (!folded) {
      const ImVec2 small(22, 30);
      const bool known = !h.hole[seat][0].empty();
      for (int k = 0; k < 2; ++k) {
        ImVec2 p(br.x - 2 * (small.x + 3) + k * (small.x + 3), tl.y - small.y * 0.55f);
        DrawCard(dl, p, small, known ? h.hole[seat][k] : std::string());
      }
    }
    if (seat == h.dealer) {
      dl->AddCircleFilled(ImVec2(br.x - 8, br.y - 8), 8, IM_COL32(240, 240, 240, 255));
      dl->AddText(ImVec2(br.x - 12, br.y - 15), IM_COL32(20, 20, 20, 255), "D");
    }
    // Bet in front of the seat
    if (frame.bet[seat] > hh::kEps) {
      const ImVec2 b(c.x + (center.x - c.x) * 0.38f, c.y + (center.y - c.y) * 0.38f);
      std::string text = FormatAmount(frame.bet[seat], h.tournament);
      ImVec2 ts = ImGui::CalcTextSize(text.c_str());
      dl->AddRectFilled(ImVec2(b.x - ts.x * 0.5f - 6, b.y - ts.y * 0.5f - 2),
                        ImVec2(b.x + ts.x * 0.5f + 6, b.y + ts.y * 0.5f + 2), IM_COL32(20, 20, 20, 190), 8.0f);
      dl->AddText(ImVec2(b.x - ts.x * 0.5f, b.y - ts.y * 0.5f), IM_COL32(255, 220, 120, 255), text.c_str());
    }
  }
}

void DrawHand(Explorer &ex, UiState &ui) {
  if (ex.selected_hand == 0 || ex.replay.empty()) {
    ImGui::TextWrapped("Select a hand in the \"Hands\" list.");
    return;
  }
  const hh::Hand &h = ex.hand;
  ImGui::Text("%s  |  %s  |  %s  |  blinds %s/%s  |  hand %s", ex.hand_site.c_str(), h.table_name.c_str(),
              h.timestamp.c_str(), FormatAmount(h.sblind, h.tournament).c_str(),
              FormatAmount(h.bblind, h.tournament).c_str(), h.hand_id.c_str());

  const int last = (int)ex.replay.size() - 1;
  if (ex.replay_step > last) ex.replay_step = last;
  if (ex.replay_step < 0) ex.replay_step = 0;
  if (ui.playing && ImGui::GetTime() - ui.last_step_time > 0.9) {
    if (ex.replay_step < last) { ++ex.replay_step; ui.last_step_time = ImGui::GetTime(); }
    else ui.playing = false;
  }
  if (ui.eq_hand != ex.selected_hand || ui.eq_step != ex.replay_step) {
    ex.UpdateEquity();
    ui.eq_hand = ex.selected_hand; ui.eq_step = ex.replay_step;
  }
  const float avail = ImGui::GetContentRegionAvail().y;
  DrawReplayTable(ex, ex.replay[ex.replay_step], avail > 560 ? 300.0f : avail * 0.45f);

  // Controls
  if (ImGui::Button("|<")) { ex.replay_step = 0; ui.playing = false; }
  ImGui::SameLine();
  if (ImGui::Button("<")) { if (ex.replay_step > 0) --ex.replay_step; ui.playing = false; }
  ImGui::SameLine();
  if (ImGui::Button(ui.playing ? "Pause" : "Play")) {
    ui.playing = !ui.playing;
    if (ui.playing && ex.replay_step >= last) ex.replay_step = 0;
    ui.last_step_time = ImGui::GetTime();
  }
  ImGui::SameLine();
  if (ImGui::Button(">")) { if (ex.replay_step < last) ++ex.replay_step; ui.playing = false; }
  ImGui::SameLine();
  if (ImGui::Button(">|")) { ex.replay_step = last; ui.playing = false; }
  ImGui::SameLine();
  ImGui::SetNextItemWidth(-1);
  if (ImGui::SliderInt("##step", &ex.replay_step, 0, last, "step %d")) ui.playing = false;
  const std::string &text = ex.replay[ex.replay_step].text;
  ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.5f, 1.0f), "%s", text.empty() ? "Start of the hand" : text.c_str());
  if (!ex.pot_odds_text.empty()) ImGui::TextColored(kGrey, "Next: %s", ex.pot_odds_text.c_str());
  else ImGui::TextColored(kGrey, " ");

  // Star, tags and note of the hand
  {
    bool save = false;
    if (ImGui::Checkbox("Star", &ex.note_marked)) save = true;
    ImGui::SameLine();
    if (InputString("##tags", &ex.note_tags, "tags, comma separated", 200)) {}
    if (ImGui::IsItemDeactivatedAfterEdit()) save = true;
    ImGui::SameLine();
    if (InputString("##note", &ex.note_text, "note", -1)) {}
    if (ImGui::IsItemDeactivatedAfterEdit()) save = true;
    if (save) ex.SaveNote();
  }

  ImGui::Separator();
  if (ImGui::SmallButton("Copy hand text")) ImGui::SetClipboardText(ex.hand_text.c_str());
  ImGui::InputTextMultiline("##handtext", const_cast<char *>(ex.hand_text.c_str()), ex.hand_text.size() + 1,
                            ImVec2(-1, -1), ImGuiInputTextFlags_ReadOnly);
}

//------------------------------------------------------------------------------
// Right side: tabs
//------------------------------------------------------------------------------

void DrawDetails(Explorer &ex, UiState &ui) {
  if (!ImGui::BeginTabBar("details")) return;
  struct TabDef { const char *label; int id; };
  static const TabDef tabs[] = { { "Player", kTabPlayer }, { "Graph", kTabGraph }, { "Reports", kTabReports },
                                 { "Sessions", kTabSessions }, { "Tournaments", kTabTournaments },
                                 { "Hands", kTabHands }, { "Hand", kTabHand } };
  for (int i = 0; i < 7; ++i) {
    ImGuiTabItemFlags flags = (ui.tab_request == tabs[i].id) ? ImGuiTabItemFlags_SetSelected : 0;
    if (ImGui::BeginTabItem(tabs[i].label, NULL, flags)) {
      ui.active_tab = tabs[i].id;
      ImGui::BeginChild("##tabcontent", ImVec2(0, 0), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar * (tabs[i].id == kTabHands || tabs[i].id == kTabGraph || tabs[i].id == kTabReports));
      switch (tabs[i].id) {
        case kTabPlayer:      DrawPlayerSheet(ex); break;
        case kTabGraph:       DrawGraphTab(ex, ui); break;
        case kTabReports:     DrawReportsTab(ex, ui); break;
        case kTabSessions:    DrawSessionsTab(ex, ui); break;
        case kTabTournaments: DrawTournamentsTab(ex, ui); break;
        case kTabHands:       DrawHandList(ex, ui); break;
        default:              DrawHand(ex, ui); break;
      }
      ImGui::EndChild();
      ImGui::EndTabItem();
    }
  }
  ui.tab_request = 0;
  ImGui::EndTabBar();
}

}  // namespace

UiState::UiState()
    : open_path_popup(false), open_demo_popup(false), open_columns_popup(false), auto_refresh(true),
      last_check(0), playing(false), last_step_time(0), quit_requested(false), tab_request(0),
      has_player_row(false), has_hand_row(false), player_row_min(0, 0), player_row_max(0, 0),
      hand_row_min(0, 0), hand_row_max(0, 0), active_tab(1) {
  export_kind = 1; open_export_popup = open_import_popup = open_filter_popup = false;
  graph_split = false; graph_cone = true; note_hand = 0; eq_hand = 0; eq_step = -1;
  export_path[0] = 0;
  import_path[0] = 0;
  snprintf(import_site, sizeof(import_site), "stars");
  path_buffer[0] = 0;
  snprintf(demo_path_buffer, sizeof(demo_path_buffer), "oh_stats_demo.db");
  name_buffer[0] = 0;
  column_filter[0] = 0;
}

void DrawExplorer(Explorer &ex, UiState &ui) {
  // Reload when OpenHoldem recorded new hands
  const double now = ImGui::GetTime();
  if (ui.auto_refresh && ex.IsOpen() && now - ui.last_check > 3.0) {
    ui.last_check = now;
    if (ex.DatabaseChanged()) { ex.players_dirty = true; ex.hands_dirty = true; }
  }
  if (ex.players_dirty) ex.RefreshPlayers();
  if (ex.hands_dirty)   ex.RefreshHands();

  const ImGuiViewport *viewport = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(viewport->WorkPos);
  ImGui::SetNextWindowSize(viewport->WorkSize);
  ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                           ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus |
                           ImGuiWindowFlags_MenuBar;
  ImGui::Begin("OpenHoldem Stats Explorer", NULL, flags);
  DrawMenu(ex, ui);
  DrawFilters(ex, ui);
  ImGui::Separator();

  const float status_height = ImGui::GetFrameHeightWithSpacing() + 6;
  const float total = ImGui::GetContentRegionAvail().x;
  ImGui::BeginChild("##left", ImVec2(total * 0.54f, -status_height), ImGuiChildFlags_Borders);
  DrawPlayers(ex, ui);
  ImGui::EndChild();
  ImGui::SameLine();
  ImGui::BeginChild("##right", ImVec2(0, -status_height), ImGuiChildFlags_Borders);
  DrawDetails(ex, ui);
  ImGui::EndChild();
  DrawStatusBar(ex);
  DrawPopups(ex, ui);
  ImGui::End();
}

}  // namespace explorer
