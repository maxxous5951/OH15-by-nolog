// Headless test of the explorer: renders the ImGui draw lists with a small software
// rasterizer (no window, no GPU), clicks through the interface and writes PPM screenshots.
// usage: headless_render <imgui-dir> <output-dir>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <algorithm>
#include <string>
#include <vector>

#include "imgui.h"
#include "demo_data.h"
#include "explorer_model.h"
#include "explorer_ui.h"

using namespace explorer;

static int g_checks = 0, g_failures = 0;
#define CHECK(cond) do { ++g_checks; if (!(cond)) { ++g_failures; \
  printf("  FAILED %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

struct Image { int w, h; std::vector<unsigned char> rgb; };

static Image g_image;
static std::vector<unsigned int> g_font;   // RGBA
static int g_font_w = 0, g_font_h = 0;

static void Clear(Image *img, int w, int h) {
  img->w = w; img->h = h; img->rgb.assign((size_t)w * h * 3, 0);
}

static inline unsigned char Mul(unsigned a, unsigned b) { return (unsigned char)((a * b + 127) / 255); }

static void DrawTriangle(const ImDrawVert &a, const ImDrawVert &b, const ImDrawVert &c, const ImVec4 &clip) {
  float minx = std::min(a.pos.x, std::min(b.pos.x, c.pos.x)), maxx = std::max(a.pos.x, std::max(b.pos.x, c.pos.x));
  float miny = std::min(a.pos.y, std::min(b.pos.y, c.pos.y)), maxy = std::max(a.pos.y, std::max(b.pos.y, c.pos.y));
  int x0 = std::max((int)floorf(minx), (int)clip.x), x1 = std::min((int)ceilf(maxx), (int)clip.z - 1);
  int y0 = std::max((int)floorf(miny), (int)clip.y), y1 = std::min((int)ceilf(maxy), (int)clip.w - 1);
  x0 = std::max(x0, 0); y0 = std::max(y0, 0); x1 = std::min(x1, g_image.w - 1); y1 = std::min(y1, g_image.h - 1);
  float area = (b.pos.x - a.pos.x) * (c.pos.y - a.pos.y) - (c.pos.x - a.pos.x) * (b.pos.y - a.pos.y);
  if (fabsf(area) < 1e-6f) return;
  for (int y = y0; y <= y1; ++y) for (int x = x0; x <= x1; ++x) {
    float px = x + 0.5f, py = y + 0.5f;
    float w0 = ((b.pos.x - px) * (c.pos.y - py) - (c.pos.x - px) * (b.pos.y - py)) / area;
    float w1 = ((c.pos.x - px) * (a.pos.y - py) - (a.pos.x - px) * (c.pos.y - py)) / area;
    float w2 = 1.0f - w0 - w1;
    const float eps = -0.0005f;
    if (w0 < eps || w1 < eps || w2 < eps) continue;
    float u = w0 * a.uv.x + w1 * b.uv.x + w2 * c.uv.x;
    float v = w0 * a.uv.y + w1 * b.uv.y + w2 * c.uv.y;
    int tx = std::min(std::max((int)(u * g_font_w), 0), g_font_w - 1);
    int ty = std::min(std::max((int)(v * g_font_h), 0), g_font_h - 1);
    unsigned tex = g_font[(size_t)ty * g_font_w + tx];
    unsigned cols[4];
    cols[0] = (a.col >> 0) & 255; cols[1] = (a.col >> 8) & 255; cols[2] = (a.col >> 16) & 255; cols[3] = (a.col >> 24) & 255;
    // vertex colour taken from the first vertex (flat) except for alpha/colour gradients, which are rare
    unsigned r = Mul(cols[0], tex & 255), g = Mul(cols[1], (tex >> 8) & 255), bl = Mul(cols[2], (tex >> 16) & 255);
    unsigned al = Mul(cols[3], (tex >> 24) & 255);
    unsigned char *dst = &g_image.rgb[((size_t)y * g_image.w + x) * 3];
    dst[0] = (unsigned char)((r * al + dst[0] * (255 - al)) / 255);
    dst[1] = (unsigned char)((g * al + dst[1] * (255 - al)) / 255);
    dst[2] = (unsigned char)((bl * al + dst[2] * (255 - al)) / 255);
  }
}

static void RenderDrawData(ImDrawData *dd) {
  for (int n = 0; n < dd->CmdListsCount; ++n) {
    const ImDrawList *list = dd->CmdLists[n];
    for (int i = 0; i < list->CmdBuffer.Size; ++i) {
      const ImDrawCmd &cmd = list->CmdBuffer[i];
      if (cmd.UserCallback) continue;
      for (unsigned e = 0; e + 2 < cmd.ElemCount; e += 3) {
        const ImDrawIdx *idx = list->IdxBuffer.Data + cmd.IdxOffset + e;
        const ImDrawVert *vtx = list->VtxBuffer.Data + cmd.VtxOffset;
        DrawTriangle(vtx[idx[0]], vtx[idx[1]], vtx[idx[2]], cmd.ClipRect);
      }
    }
  }
}

static void SavePpm(const std::string &path) {
  FILE *f = fopen(path.c_str(), "wb");
  if (!f) return;
  fprintf(f, "P6\n%d %d\n255\n", g_image.w, g_image.h);
  fwrite(g_image.rgb.data(), 1, g_image.rgb.size(), f);
  fclose(f);
}

static void Frame(Explorer &ex, UiState &ui, const char *screenshot = NULL) {
  ImGui::NewFrame();
  DrawExplorer(ex, ui);
  ImGui::Render();
  if (screenshot) {
    Clear(&g_image, (int)ImGui::GetIO().DisplaySize.x, (int)ImGui::GetIO().DisplaySize.y);
    for (size_t i = 0; i < g_image.rgb.size(); i += 3) { g_image.rgb[i] = 30; g_image.rgb[i + 1] = 30; g_image.rgb[i + 2] = 34; }
    RenderDrawData(ImGui::GetDrawData());
    SavePpm(screenshot);
  }
}

static void Frames(Explorer &ex, UiState &ui, int n) { for (int i = 0; i < n; ++i) Frame(ex, ui); }

static void Click(Explorer &ex, UiState &ui, ImVec2 p) {
  ImGuiIO &io = ImGui::GetIO();
  io.AddMousePosEvent(p.x, p.y);
  Frames(ex, ui, 2);
  io.AddMouseButtonEvent(0, true);
  Frames(ex, ui, 2);
  io.AddMouseButtonEvent(0, false);
  Frames(ex, ui, 3);
}

int main(int argc, char **argv) {
  if (argc < 3) { printf("usage: %s <imgui-dir> <output-dir>\n", argv[0]); return 2; }
  const std::string imgui_dir = argv[1], out = std::string(argv[2]) + "/";
  const std::string db_path = out + "explorer_test.db";
  unlink(db_path.c_str()); unlink((db_path + "-wal").c_str()); unlink((db_path + "-shm").c_str());

  printf("demo data\n");
  std::string error;
  CHECK(GenerateDemoDb(db_path, 2400, 7, &error));
  if (!error.empty()) printf("  %s\n", error.c_str());

  printf("model\n");
  Explorer ex;
  CHECK(ex.Open(db_path, &error));
  CHECK(ex.players.size() == 8);
  CHECK(ex.sites.size() == 2);
  {
    double nit = -1, maniac = -1;
    for (size_t i = 0; i < ex.players.size(); ++i) {
      const ohstats::PlayerRow &p = ex.players[i];
      if (p.name == "Nit") nit = p.values[0];
      if (p.name == "Maniac") maniac = p.values[0];
    }
    printf("  vpip Nit %.3f, Maniac %.3f\n", nit, maniac);
    CHECK(nit > 0.05 && nit < 0.25);
    CHECK(maniac > nit + 0.2);
  }
  ex.sort_column = 0; ex.sort_descending = true; ex.SortPlayers();
  CHECK(ex.players.front().name == "Maniac" || ex.players.front().name == "Fish");
  ex.sort_column = -2; ex.sort_descending = false; ex.SortPlayers();
  CHECK(ex.players.front().name == "CallStn");
  ex.filters.game_type = 1; ex.RefreshPlayers();
  CHECK(!ex.players.empty() && ex.players[0].hands < 300);
  ex.filters.game_type = 2; ex.RefreshPlayers();
  long long both = ex.players.empty() ? 0 : ex.players[0].hands;
  ex.filters.game_type = 0; ex.RefreshPlayers();
  CHECK(both > ex.players[0].hands);
  ex.filters.site_index = 2; ex.RefreshPlayers();      // one site
  CHECK(!ex.players.empty());
  ex.filters.site_index = 0; ex.filters.last_hands = 50; ex.RefreshPlayers();
  CHECK(ex.players[0].hands == 50);
  ex.filters.last_hands = 0; ex.filters.position = ohstats::kPositionBTN; ex.RefreshPlayers();
  CHECK(!ex.players.empty() && ex.players[0].hands < 700);
  ex.filters.position = -1; ex.RefreshPlayers();
  CHECK(ex.AddColumn("rfi_25bb") && !ex.AddColumn("rfi_25bb") && !ex.AddColumn("nope"));
  ex.RefreshPlayers();
  CHECK(ex.players[0].values.size() == ex.columns.size());
  ex.RemoveColumn("rfi_25bb");
  CHECK(StatLabel("bb_3bet_vs_sb_25bb") == "BB 3Bet vs SB 2.5bb");
  CHECK(FormatStat("vpip", 0.256) == "25.6" && FormatStat("vpip", NAN) == "-" && FormatStat("hands", 12) == "12");

  // every statistic of the sections exists, nothing is listed twice
  {
    size_t total = 0;
    for (size_t s = 0; s < Sections().size(); ++s) {
      total += Sections()[s].stats.size();
      for (size_t i = 0; i < Sections()[s].stats.size(); ++i) CHECK(ohstats::Db::IsSupportedStat(Sections()[s].stats[i]));
    }
    CHECK(total == ohstats::Db::SupportedStats().size() - 2);   // without vpip_L / pfr_L
  }

  // hands: totals, pages, every hand can be loaded, chips are conserved, replay is consistent
  ex.RefreshHands();
  CHECK(ex.hands_total > 1000);
  CHECK((int)ex.hands.size() == Explorer::kHandsPerPage);
  int bad = 0;
  for (size_t i = 0; i < ex.hands.size(); ++i) {
    CHECK(ex.SelectHand(ex.hands[i].pk));
    double start = 0, end = 0;
    for (int s = 0; s < hh::kMaxSeats; ++s) if (ex.hand.in_hand[s]) { start += ex.hand.start_stack[s]; end += ex.hand.end_stack[s]; }
    if (fabs(start - end) > 0.02 + ex.hand.rake) { if (++bad < 3) printf("  chips not conserved in hand %s: %f vs %f\n", ex.hand.hand_id.c_str(), start, end); }
    CHECK(ex.replay.size() == ex.hand.actions.size() + 2);
    CHECK(fabs(ex.replay.back().pot - ex.hand.total_pot) < 0.01);
    CHECK(ex.hand_text.find("*** HOLE CARDS ***") != std::string::npos);
  }
  CHECK(bad == 0);
  ex.hands_page = 1; ex.RefreshHands();
  CHECK(ex.hands[0].pk != 0 && (int)ex.hands.size() == Explorer::kHandsPerPage);
  ex.hands_page = 0; ex.RefreshHands();
  ex.SelectPlayer("Maniac");
  CHECK(ex.hands_total > 300 && ex.hands[0].has_player && ex.hands[0].player_name == "Maniac");
  CHECK(ex.SheetValue("vpip") > 0.4);
  CHECK(ex.SheetValue("hands") > 300);
  ex.SelectPlayer("");
  CHECK(ex.DatabaseChanged() == false);
  CHECK(AddDemoHands(&ex.db, 5, 99, &error));
  CHECK(ex.DatabaseChanged() == true);
  ex.RefreshPlayers();
  CHECK(ex.DatabaseChanged() == false);

  printf("interface\n");
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO &io = ImGui::GetIO();
  io.IniFilename = NULL;
  io.DisplaySize = ImVec2(1500, 900);
  io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;
  const std::string font_path = imgui_dir + "/misc/fonts/DroidSans.ttf";
  CHECK(io.Fonts->AddFontFromFileTTF(font_path.c_str(), 15.0f) != NULL);
  unsigned char *pixels; int w, h;
  io.Fonts->GetTexDataAsRGBA32(&pixels, &w, &h);
  g_font_w = w; g_font_h = h;
  g_font.assign((size_t)w * h, 0);
  memcpy(g_font.data(), pixels, (size_t)w * h * 4);
  io.Fonts->SetTexID((ImTextureID)(intptr_t)1);
  ImGui::StyleColorsDark();

  Explorer ui_ex;
  CHECK(ui_ex.Open(db_path, &error));
  UiState ui;
  io.DeltaTime = 1.0f / 60.0f;
  Frames(ui_ex, ui, 4);
  Frame(ui_ex, ui, (out + "1_players.ppm").c_str());
  CHECK(ui.has_player_row);
  CHECK(ui_ex.selected_player.empty());
  Click(ui_ex, ui, ImVec2((ui.player_row_min.x + 30), (ui.player_row_min.y + ui.player_row_max.y) * 0.5f));
  CHECK(!ui_ex.selected_player.empty());
  CHECK(ui.active_tab == 1);
  std::string clicked = ui_ex.selected_player;
  Frame(ui_ex, ui, (out + "2_player_sheet.ppm").c_str());

  ui.tab_request = 2;
  Frames(ui_ex, ui, 3);
  CHECK(ui.active_tab == 2);
  CHECK(ui.has_hand_row);
  Frame(ui_ex, ui, (out + "3_hands.ppm").c_str());
  Click(ui_ex, ui, ImVec2(ui.hand_row_min.x + 20, (ui.hand_row_min.y + ui.hand_row_max.y) * 0.5f));
  CHECK(ui_ex.selected_hand != 0);
  CHECK(ui.active_tab == 3);
  ui_ex.replay_step = std::min((int)ui_ex.replay.size() - 1, 6);
  Frames(ui_ex, ui, 2);
  Frame(ui_ex, ui, (out + "4_replay.ppm").c_str());

  // columns popup and filters
  ui.open_columns_popup = true;
  Frames(ui_ex, ui, 3);
  Frame(ui_ex, ui, (out + "5_columns.ppm").c_str());
  ImGui::GetIO().AddKeyEvent(ImGuiKey_Escape, true);
  Frames(ui_ex, ui, 2);
  ImGui::GetIO().AddKeyEvent(ImGuiKey_Escape, false);
  Frames(ui_ex, ui, 2);
  ui_ex.filters.name = "nit";
  ui_ex.players_dirty = true;
  Frames(ui_ex, ui, 3);
  CHECK(ui_ex.players.size() == 1 && ui_ex.players[0].name == "Nit");

  // New tabs: each one is drawn, its data is computed
  ui_ex.filters.name.clear(); ui_ex.players_dirty = true;
  ui_ex.SelectPlayer("");
  ui.tab_request = 4; Frames(ui_ex, ui, 3);
  CHECK(ui.active_tab == 4 && ui_ex.graph.hands > 1000);
  Frame(ui_ex, ui, (out + "6_graph.ppm").c_str());
  ui.graph_split = true;
  Frames(ui_ex, ui, 2);
  Frame(ui_ex, ui, (out + "6b_graph_split.ppm").c_str());
  // hover the graph: no crash, tooltip
  io.AddMousePosEvent(900, 450); Frames(ui_ex, ui, 3);

  ui.tab_request = 5; Frames(ui_ex, ui, 3);
  CHECK(ui.active_tab == 5 && ui_ex.report.size() == 5);
  Frame(ui_ex, ui, (out + "7_reports_position.ppm").c_str());
  ui_ex.report_kind = kReportStartingHand; ui_ex.report_as_matrix = true;
  Frames(ui_ex, ui, 3);
  CHECK(ui_ex.report.size() > 100);
  Frame(ui_ex, ui, (out + "8_matrix.ppm").c_str());
  ui_ex.report_as_matrix = false; ui_ex.report_kind = kReportPosition;
  ui.tab_request = 6; Frames(ui_ex, ui, 3);
  CHECK(ui.active_tab == 6 && ui_ex.sessions.size() == 1);
  Frame(ui_ex, ui, (out + "9_sessions.ppm").c_str());
  ui.tab_request = 7; Frames(ui_ex, ui, 3);
  CHECK(ui.active_tab == 7 && ui_ex.tournaments.size() == 1);
  Frame(ui_ex, ui, (out + "10_tournaments.ppm").c_str());

  // player sheet with HUD, sample sizes and leaks
  ui_ex.SelectPlayer("Maniac");
  ui.tab_request = 1; Frames(ui_ex, ui, 3);
  Frame(ui_ex, ui, (out + "11_player_hud.ppm").c_str());
  CHECK(ui_ex.TypeOf("Maniac") == kTypeManiac);

  // Open database browser: starts in the folder of the open database, lists .db files
  ui.browse_dir = out; ui.open_path_popup = true; Frames(ui_ex, ui, 3);
  Frame(ui_ex, ui, (out + "16_open_browser.ppm").c_str());
  ImGui::GetIO().AddKeyEvent(ImGuiKey_Escape, true); Frames(ui_ex, ui, 2);
  ImGui::GetIO().AddKeyEvent(ImGuiKey_Escape, false); Frames(ui_ex, ui, 2);
  CHECK(!ui.browse_dir.empty());

  // filter popup, import popup, export popup
  ui.open_filter_popup = true; Frames(ui_ex, ui, 3);
  Frame(ui_ex, ui, (out + "12_filters.ppm").c_str());
  ImGui::GetIO().AddKeyEvent(ImGuiKey_Escape, true); Frames(ui_ex, ui, 2);
  ImGui::GetIO().AddKeyEvent(ImGuiKey_Escape, false); Frames(ui_ex, ui, 2);
  ui_ex.hand_filter.pot_type = 3;
  ui_ex.hands_dirty = true;
  ui.tab_request = 2; Frames(ui_ex, ui, 3);
  CHECK(ui.active_tab == 2 && ui_ex.hands_total > 0);
  Frame(ui_ex, ui, (out + "13_hands_filtered.ppm").c_str());
  ui_ex.ClearHandFilter();
  ui.open_import_popup = true; Frames(ui_ex, ui, 3);
  Frame(ui_ex, ui, (out + "14_import.ppm").c_str());
  ImGui::GetIO().AddKeyEvent(ImGuiKey_Escape, true); Frames(ui_ex, ui, 2);
  ImGui::GetIO().AddKeyEvent(ImGuiKey_Escape, false); Frames(ui_ex, ui, 2);

  // replay with equity: a hand that went to showdown with known cards
  {
    ui_ex.SelectPlayer("");
    ui_ex.hand_filter.showdown = 1;
    ui_ex.hands_dirty = true; Frames(ui_ex, ui, 2);
    CHECK(!ui_ex.hands.empty());
    if (!ui_ex.hands.empty()) {
      CHECK(ui_ex.SelectHand(ui_ex.hands[0].pk));
      ui_ex.replay_step = std::max(0, (int)ui_ex.replay.size() - 3);
      ui.tab_request = 3; Frames(ui_ex, ui, 3);
      bool any = false;
      for (int i = 0; i < hh::kMaxSeats; ++i) any |= !IsUndefined(ui_ex.equity[i]);
      CHECK(any);
      Frame(ui_ex, ui, (out + "15_replay_equity.ppm").c_str());
    }
    ui_ex.ClearHandFilter();
  }

  printf("\n%d checks, %d failures\n", g_checks, g_failures);
  ImGui::DestroyContext();
  return g_failures ? 1 : 0;
}
