//******************************************************************************
//
// This file is part of the OpenHoldem project
//    Source code:           https://github.com/OpenHoldem/openholdembot/
//    Forums:                http://www.maxinmontreal.com/forums/index.php
//    Licensed under GPL v3: http://www.gnu.org/licenses/gpl.html
//
//******************************************************************************
//
// Purpose: Batch completion of fonts.
//   Scans all bmp-files of a folder (e.g. the replay frames of OpenReplayShooter)
//   with the current tablemap, collects every character of the text-regions
//   that is not yet part of the font, counts how often each one occurs
//   and lets the user name them one after the other, most frequent first.
//
//******************************************************************************

#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>
#include "afxwin.h"
#include "resource.h"
#include "..\CTablemap\CTablemap.h"

// One kind of unknown character (identified by font group + hexmash)
struct SBatchGlyph {
	int				group;				// font group (t0..t9)
	STablemapFont	font;				// pixels; font.ch is the typed character
	int				occurrences;		// how often it was seen in all images
	CString			example_file;		// first image where it was found
	CString			example_region;		// first region where it was found
	bool			assigned;			// the user typed a character for it
	bool			skipped;			// the user skipped it
	char			previous_ch;		// character erased by "Back" (0 = none), proposed again
	// The text region where it was found (shared between all glyphs of that region)
	std::shared_ptr<HBITMAP__>	image;
	int				image_width;
	int				image_height;
	RECT			box;				// the glyph inside that region (inclusive pixel coordinates)
};

// OCR suggestion (Tesseract). The whole text region is read once, the characters
// are then matched to the glyphs by their position.
struct SBatchOcrSymbol {
	std::string	text;			// UTF-8
	float		confidence;		// 0..100
	double		left;			// edges in pixels of the region (right = exclusive)
	double		right;
};
struct SBatchOcrResult {
	bool						ok;
	std::vector<SBatchOcrSymbol>	symbols;
};

class CBatchFonts {
public:
	// Complete workflow: choose folder -> scan -> dialog -> add to the tablemap
	static void Run(CWnd *parent);
	// Scans all *.bmp of a folder. The glyphs come back sorted by frequency.
	// Returns the number of images that could be read.
	static int ScanFolder(const CString &folder, std::vector<SBatchGlyph> *glyphs,
		int *unreadable_files, int *skipped_regions);
};

class CDlgBatchFonts : public CDialog {
	DECLARE_DYNAMIC(CDlgBatchFonts)
public:
	CDlgBatchFonts(CWnd *parent = NULL);
	virtual ~CDlgBatchFonts();
	enum { IDD = IDD_BATCH_FONTS };

	std::vector<SBatchGlyph>	glyphs;		// in: sorted glyphs, out: with characters
	int							image_count;
	int							AssignedCount() const;

protected:
	virtual void DoDataExchange(CDataExchange *pDX);
	virtual BOOL OnInitDialog();
	virtual void OnOK();		// accept the character and go to the next glyph
	virtual void OnCancel();
	virtual BOOL PreTranslateMessage(MSG *msg);	// Space = skip
	afx_msg void OnBnClickedSkip();
	afx_msg void OnBnClickedBack();
	afx_msg void OnBnClickedFinish();
	afx_msg void OnBnClickedSplit();
	afx_msg void OnBnClickedOcrEnable();
	afx_msg void OnCbnSelchangeOcrPreset();
	afx_msg void OnEnKillfocusOcrOption();
	afx_msg void OnDrawItem(int control_id, LPDRAWITEMSTRUCT draw_item);
	DECLARE_MESSAGE_MAP()

private:
	void ShowGlyph(bool only_redraw = false);
	void MoveCut(int delta);
	void SplitAtCut();
	void ReadOcrOptions();
	bool SuggestFromOcr();		// true if it suggests a cut
	void OcrOptionsChanged();
	void NextGlyph();
	bool ConfirmFinish();

	int			_current;
	int			_cut;		// 0 = none, otherwise the number of columns left of the cut
	bool		_ocr_enabled;
	int			_ocr_preset;			// 0 = AutoOcr0, 1 = AutoOcr1
	int			_ocr_threshold;
	int			_ocr_min_confidence;
	std::map<HBITMAP__*, SBatchOcrResult>	_ocr_cache;	// one reading per region image
	CEdit		_character;
	CEdit		_pixels;
	CStatic		_info;
	CStatic		_progress;
	CFont		_fixed_font;
};
