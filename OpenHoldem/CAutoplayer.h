//******************************************************************************
//
// This file is part of the OpenHoldem project
//    Source code:           https://github.com/OpenHoldem/openholdembot/
//    Forums:                http://www.maxinmontreal.com/forums/index.php
//    Licensed under GPL v3: http://www.gnu.org/licenses/gpl.html
//
//******************************************************************************
//
// Purpose:
//
//******************************************************************************

#ifndef INC_CAUTOPLAYER_H
#define INC_CAUTOPLAYER_H


#include "MainFrm.h"
#include "../CTablemap/CTablemap.h"
#include "../CTablemap/CTableMapAccess.h"
#include "OpenHoldem.h"

class CAutoplayer 
{
public:
	// public functions
	CAutoplayer();
	~CAutoplayer();
public:
	void EngageAutoPlayerUponConnectionIfNeeded();
	void DoAutoplayer();

public:
	// public accessors
	const bool autoplayer_engaged() { return _autoplayer_engaged; }
	// Advisor mode: the autoplayer logic runs completely (evaluation, trace, log),
	// but nothing gets clicked or typed and the symbol engines do not
	// register an action. Only active if the real autoplayer is not engaged.
	bool advisor_mode_engaged();
	bool TimeToHandleSecondaryFormulas();

public:
	// public mutators
	void EngageAutoplayer(bool to_be_enabled_or_not);

private:
	// private functions and variables - not available via accessors or mutators
	void DoRebuyIfNeccessary();
	bool ExecutePrimaryFormulasIfNecessary();
	bool ExecuteSecondaryFormulasIfNecessary();
	bool ExecuteRaiseCallCheckFold();
	bool ExecuteBeep();
	bool AnyPrimaryFormulaTrue();
	bool AnySecondaryFormulaTrue();
	bool DoAllin();
	bool DoBetPot();
	bool HandleInterfacebuttonsI86(); 
	void PrepareActionSequence();
	void FinishActionSequenceIfNecessary();
	bool DoBetsize();
	bool DoPrefold();
	bool DoChat();
	void UpdateEnginesAfterAction(int autoplayer_action_code);
	bool AdvisorDecisionIsNew();

private:
	// private variables - use public accessors and public mutators to address these
	bool	_autoplayer_engaged;
	CString _advisor_last_decision_key;

private:
	POINT	cursor_position;
	HWND	window_with_focus;
	bool	action_sequence_needs_to_be_finished;
	bool	_already_executing_allin_adjustment;

	CCritSec	m_critsec;
};

extern CAutoplayer *p_autoplayer;

// True if input (mouse, keyboard, hotkeys) must be simulated instead of sent.
bool AdvisorBlocksInput();


#endif //INC_CAUTOPLAYER_H
