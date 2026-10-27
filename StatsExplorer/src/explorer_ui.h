//******************************************************************************
//
// This file is part of the OpenHoldem project
//    Licensed under GPL v3: http://www.gnu.org/licenses/gpl.html
//
//******************************************************************************
//
// Purpose: Dear ImGui interface of the stats explorer
//          (the backend, i.e. window and renderer, is in main.cpp).
//
//******************************************************************************

#ifndef INC_EXPLORER_UI_H
#define INC_EXPLORER_UI_H

#include "imgui.h"
#include "explorer_model.h"

namespace explorer {

struct UiState {
  UiState();
  // Dialogs
  char path_buffer[512];
  std::string browse_dir;  // folder shown in the Open database browser
  char demo_path_buffer[512];
  char name_buffer[64];
  char column_filter[64];
  bool open_path_popup, open_demo_popup, open_columns_popup;
  std::string popup_error;
  // Behaviour
  bool        auto_refresh;
  double      last_check;
  bool        playing;
  double      last_step_time;
  bool        quit_requested;
  int         tab_request;            // 1 = player, 2 = hands, 3 = hand, 0 = none
  // Export / import dialogs
  int         export_kind;            // 1 players, 2 report, 3 hand list, 4 hands text
  char        export_path[512];
  std::string export_message;
  bool        open_export_popup, open_import_popup, open_filter_popup;
  char        import_path[512];
  char        import_site[32];
  std::string import_message;
  // Graph options, notes, replay
  bool        graph_split, graph_cone;
  long long   note_hand;              // hand whose note is in the buffers
  long long   eq_hand;
  int         eq_step;
  // Hit-test information for the automatic tests (screen coordinates)
  bool   has_player_row, has_hand_row;
  ImVec2 player_row_min, player_row_max;
  ImVec2 hand_row_min, hand_row_max;
  int    active_tab;
};

// Draws the whole explorer in the main viewport (call between NewFrame and Render)
void DrawExplorer(Explorer &explorer, UiState &ui);

}  // namespace explorer

#endif  // INC_EXPLORER_UI_H
