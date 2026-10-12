//******************************************************************************
//
// This file is part of the OpenHoldem project
//    Source code:           https://github.com/OpenHoldem/openholdembot/
//    Forums:                http://www.maxinmontreal.com/forums/index.php
//    Licensed under GPL v3: http://www.gnu.org/licenses/gpl.html
//
//******************************************************************************
//
// Purpose: The main view (poker table), see OpenHoldemView.h
//
//******************************************************************************

// OpenHoldemView.cpp : implementation of the COpenHoldemView class
//

#include "stdafx.h"
#include "OpenHoldemView.h"

#include "CAutoplayerTrace.h"
#include "CEngineContainer.h"
#include "CHandresetDetector.h"
#include "CHeartbeatThread.h"

#include "CCasinoInterface.h"
#include "CScraper.h"
#include "CStringMatch.h"
#include "CSymbolengineAutoplayer.h"
#include "CSymbolengineChipAmounts.h"
#include "CSymbolengineColourCodes.h"
#include "CSymbolengineGameType.h"
#include "CSymbolEngineIsOmaha.h"
#include "CSymbolEngineIsTournament.h"
#include "CSymbolEngineTableLimits.h"
#include "CSymbolengineUserchair.h"
#include "..\CTablemap\CTablemap.h"
#include "CTableState.h"
#include "NumericalFunctions.h"
#include "..\DLLs\StringFunctions_DLL\string_functions.h"

#include "ModernUI/ModernTable.h"
#include "OpenHoldem.h"
#include "OpenHoldemDoc.h"

namespace {

modernui::CardView ToCardView(Card *card) {
  modernui::CardView view;
  if (card->IsCardBack()) {
    view.kind = modernui::CardView::Back;
  } else if (!card->IsNoCard()) {
    view.kind = modernui::CardView::Face;
    view.rank = card->GetRankCharacter();
    view.suit = card->GetSuitCharacter(false);
  }
  return view;
}

bool Clickable(int logical_button) {
  return p_casino_interface->LogicalAutoplayerButton(logical_button)->IsClickable();
}

}  // namespace

// COpenHoldemView
IMPLEMENT_DYNCREATE(COpenHoldemView, CView)

BEGIN_MESSAGE_MAP(COpenHoldemView, CView)
	ON_WM_ERASEBKGND()
	ON_WM_TIMER()
END_MESSAGE_MAP()

// COpenHoldemView construction/destruction
COpenHoldemView::COpenHoldemView() {
  modernui::GdiplusAcquire();
  _snapshot = new modernui::TableView();
  _snapshot_valid = false;
}

COpenHoldemView::~COpenHoldemView() {
  delete _snapshot;
  _snapshot = NULL;
  modernui::GdiplusRelease();
}

BOOL COpenHoldemView::PreCreateWindow(CREATESTRUCT& cs) {
	return CView::PreCreateWindow(cs);
}

void COpenHoldemView::OnInitialUpdate() {
	CView::OnInitialUpdate();
	// Timer to check for display updates
	SetTimer(DISPLAY_UPDATE_TIMER, 103, 0);
}

BOOL COpenHoldemView::OnEraseBkgnd(CDC *pDC) {
  // We paint the complete client area ourselves (double-buffered): no flicker.
  return TRUE;
}

// COpenHoldemView drawing
void COpenHoldemView::OnDraw(CDC* pDC) {
  CRect client;
  GetClientRect(&client);
  if (client.Width() < 1 || client.Height() < 1) return;
  // First paint, before the first timer tick
  if (!_snapshot_valid && p_heartbeat_thread != NULL) {
    RefreshSnapshot();
  }
  CDC memory_dc;
  CBitmap bitmap;
  memory_dc.CreateCompatibleDC(pDC);
  bitmap.CreateCompatibleBitmap(pDC, client.Width(), client.Height());
  CBitmap *old_bitmap = memory_dc.SelectObject(&bitmap);
  {
    Gdiplus::Graphics graphics(memory_dc.GetSafeHdc());
    modernui::SetupQuality(graphics);
    modernui::DrawTable(graphics,
      Gdiplus::RectF(0.0f, 0.0f, (Gdiplus::REAL)client.Width(), (Gdiplus::REAL)client.Height()),
      modernui::DpiScale(m_hWnd), modernui::Theme::Colors(), *_snapshot);
  }
  pDC->BitBlt(0, 0, client.Width(), client.Height(), &memory_dc, 0, 0, SRCCOPY);
  memory_dc.SelectObject(old_bitmap);
  write_log(Preferences()->debug_alltherest(), "[GUI] location Johnny_1\n");
}

void COpenHoldemView::OnTimer(UINT_PTR nIDEvent) {
  write_log(Preferences()->debug_timers(), "[GUI] COpenHoldemView::OnTimer()\n");
	if (nIDEvent == DISPLAY_UPDATE_TIMER) {
		RefreshSnapshot();
    write_log(Preferences()->debug_alltherest(), "[GUI] location Johnny_2\n");
	}
	// Otherwise: continue with parent class
	CView::OnTimer(nIDEvent);
}

// Reads the engine state and repaints if anything visible has changed.
void COpenHoldemView::RefreshSnapshot() {
  if (p_heartbeat_thread == NULL) return;
  // Only do this if we are not in the middle of a scraper/symbol update
  if (!TryEnterCriticalSection(&p_heartbeat_thread->cs_update_in_progress)) return;
  modernui::TableView fresh;
  BuildSnapshot(&fresh);
  LeaveCriticalSection(&p_heartbeat_thread->cs_update_in_progress);
  if (!_snapshot_valid || !fresh.SameAs(*_snapshot)) {
    *_snapshot = fresh;
    _snapshot_valid = true;
    Invalidate(FALSE);
  }
}

void COpenHoldemView::BuildSnapshot(modernui::TableView *v) {
  if (p_engine_container == NULL || p_table_state == NULL || p_tablemap == NULL
      || p_engine_container->symbol_engine_tablelimits() == NULL
      || p_engine_container->symbol_engine_gametype() == NULL
      || p_engine_container->symbol_engine_chip_amounts() == NULL) {
    return;   // very early phase of initialization: empty table
  }
  // Center info box
  CString hand_number = p_handreset_detector->GetHandNumber();
  double sblind = p_engine_container->symbol_engine_tablelimits()->sblind();
  double bblind = p_engine_container->symbol_engine_tablelimits()->bblind();
  double bigbet = p_engine_container->symbol_engine_tablelimits()->bigbet();
  double ante   = p_engine_container->symbol_engine_tablelimits()->ante();
  double pot    = p_engine_container->symbol_engine_chip_amounts()->pot();
  CString text;
  text.Format("Hand #: %s", hand_number.IsEmpty() ? "-" : hand_number.GetString());
  v->hand_line = modernui::ToWide(text);
  if (IsInteger(sblind) && IsInteger(bblind)) {
    text.Format("%s %.0f/%.0f/%.0f", p_engine_container->symbol_engine_gametype()->GetGameTypeAsString(),
      sblind, bblind, bigbet);
  } else {
    text.Format("%s %.2f/%.2f/%.2f", p_engine_container->symbol_engine_gametype()->GetGameTypeAsString(),
      sblind, bblind, bigbet);
  }
  v->game_line = modernui::ToWide(text);
  if (ante != 0) {
    text.Format("Ante: %s", Number2CString(ante).GetString());
    v->ante_line = modernui::ToWide(text);
  }
  text.Format("Pot: %s", Number2CString(pot).GetString());
  v->pot_line = modernui::ToWide(text);

  // Community cards
  for (int i = 0; i < kNumberOfCommunityCards && i < 5; ++i) {
    v->community[i] = ToCardView(p_table_state->CommonCards(i));
  }

  // Seats
  int nchairs = p_tablemap->nchairs();
  if (nchairs > kMaxNumberOfPlayers) nchairs = kMaxNumberOfPlayers;
  if (nchairs < 0) nchairs = 0;
  v->nchairs = nchairs;
  int userchair = -1;
  if (p_engine_container->symbol_engine_userchair() != NULL) {
    userchair = p_engine_container->symbol_engine_userchair()->userchair();
  }
  for (int i = 0; i < nchairs; ++i) {
    CPlayer *player = p_table_state->Player(i);
    modernui::SeatView &seat = v->seats[i];
    seat.seated = player->seated();
    seat.active = player->active();
    seat.dealer = player->dealer();
    seat.is_user = (i == userchair);
    if (!seat.seated && !seat.active) {
      // Empty chair. A bet in front of it is still shown.
    } else {
      seat.name = modernui::ToWide(player->name());
      CString balance = Number2CString(player->_balance.GetValue());
      if (seat.active) {
        seat.balance = modernui::ToWide(balance);
      } else {
        text.Format("Out (%s)", balance.GetString());
        seat.balance = modernui::ToWide(text);
      }
    }
    if (player->_bet.GetValue() != 0) {
      seat.bet = modernui::ToWide(Number2CString(player->_bet.GetValue()));
    }
    if (seat.active) {
      int cards = NumberOfCardsPerPlayer();
      if (cards > kMaxNumberOfCardsPerPlayer) cards = kMaxNumberOfCardsPerPlayer;
      if (cards > 4) cards = 4;
      seat.card_count = cards;
      for (int k = 0; k < cards; ++k) {
        seat.cards[k] = ToCardView(player->hole_cards(k));
      }
    }
  }

  // Indicators: which casino buttons are currently clickable?
  if (p_casino_interface != NULL) {
    v->indicator_on[modernui::kIndFold]     = Clickable(k_autoplayer_function_fold);
    v->indicator_on[modernui::kIndCall]     = Clickable(k_autoplayer_function_call);
    v->indicator_on[modernui::kIndCheck]    = Clickable(k_autoplayer_function_check);
    v->indicator_on[modernui::kIndRaise]    = Clickable(k_autoplayer_function_raise);
    v->indicator_on[modernui::kIndAllin]    = Clickable(k_autoplayer_function_allin);
    v->indicator_on[modernui::kIndAutopost] = Clickable(k_hopper_function_autopost);
    v->indicator_on[modernui::kIndSitin]    = Clickable(k_hopper_function_sitin);
    v->indicator_on[modernui::kIndSitout]   = Clickable(k_hopper_function_sitout);
    v->indicator_on[modernui::kIndLeave]    = Clickable(k_hopper_function_leave);
    v->indicator_on[modernui::kIndPrefold]  = Clickable(k_standard_function_prefold);
  }
}
