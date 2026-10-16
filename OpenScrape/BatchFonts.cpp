//******************************************************************************
//
// This file is part of the OpenHoldem project
//    Source code:           https://github.com/OpenHoldem/openholdembot/
//    Forums:                http://www.maxinmontreal.com/forums/index.php
//    Licensed under GPL v3: http://www.gnu.org/licenses/gpl.html
//
//******************************************************************************
//
// Purpose: Batch completion of fonts, see BatchFonts.h
//
//******************************************************************************

#include "stdafx.h"
#include <afxdlgs.h>
#include <algorithm>
#include <math.h>
#include <memory>
#include <map>
#include <string>
#include <utility>
#include "BatchFonts.h"
#include "DialogTableMap.h"
#include "MainFrm.h"
#include "OpenScrape.h"
#include "OpenScrapeDoc.h"
#include "..\CTablemap\CTablemap.h"
#include "..\CTransform\CTransform.h"

typedef std::pair<int, std::string> TGlyphKey;
typedef std::map<TGlyphKey, SBatchGlyph> TGlyphMap;

namespace {

// Text regions have a transform "T0" .. "T9" (the digit is the font group)
bool IsTextRegion(const CString &transform, int *group) {
	if (transform.GetLength() < 2) return false;
	if (transform.Left(1) != "T") return false;
	const char digit = transform.GetAt(transform.GetLength() - 1);
	if (digit < '0' || digit > '9') return false;
	*group = digit - '0';
	return *group < k_max_number_of_font_groups_in_tablemap;
}

// Same separation of the characters as "Create Font" does for a single region,
// but the unknown characters are only collected, not asked for.
void CollectUnknownGlyphs(RMapCI region, const CString &file_name, int group, int width, int height,
	const std::shared_ptr<HBITMAP__> &region_image,
	const bool background[], const bool (*character)[MAX_CHAR_HEIGHT], CTransform *trans, TGlyphMap *found) {
	int scan_pos = 0;
	// skip initial background bands
	while (scan_pos < width && background[scan_pos]) scan_pos++;
	while (scan_pos < width) {
		const int start = scan_pos;
		// scan for the next background band
		while (scan_pos < width && !background[scan_pos]) scan_pos++;
		int x_begin = 0, x_end = 0, y_begin = 0, y_end = 0;
		trans->GetShiftLeftDownIndexes(start, scan_pos - start, height, background, character,
			&x_begin, &x_end, &y_begin, &y_end);
		STablemapFont glyph;
		CString spaced_hexmash;
		trans->CalcHexmash(x_begin, x_end, y_begin, y_end, character, &spaced_hexmash, true);
		int pos = 0, x_count = 0;
		CString number = spaced_hexmash.Tokenize(" ", pos);
		while (pos != -1 && x_count < MAX_SINGLE_CHAR_WIDTH) {
			glyph.x[x_count++] = strtoul(number.GetString(), NULL, 16);
			number = spaced_hexmash.Tokenize(" ", pos);
		}
		glyph.x_count = x_count;
		trans->CalcHexmash(x_begin, x_end, y_begin, y_end, character, &glyph.hexmash, false);
		glyph.ch = '?';
		// skip background bands
		while (scan_pos < width && background[scan_pos]) scan_pos++;

		if (glyph.x_count <= 0 || glyph.hexmash.IsEmpty()) continue;
		// Only characters that are not yet part of the font
		if (p_tablemap->t$(group)->find(glyph.hexmash) != p_tablemap->t$(group)->end()) continue;

		const TGlyphKey key(group, std::string((LPCSTR)glyph.hexmash));
		TGlyphMap::iterator it = found->find(key);
		if (it != found->end()) {
			it->second.occurrences++;
		} else {
			SBatchGlyph entry;
			entry.group = group;
			entry.font = glyph;
			entry.occurrences = 1;
			entry.example_file = file_name;
			entry.example_region = region->first;
			entry.assigned = false;
			entry.skipped = false;
			entry.image = region_image;
			entry.image_width = width;
			entry.image_height = height;
			entry.box.left = x_begin;
			entry.box.top = y_begin;
			entry.box.right = x_end;
			entry.box.bottom = y_end;
			found->insert(std::make_pair(key, entry));
		}
	}
}

// Returns false if the image can't be read
bool ScanImage(const CString &path, const CString &file_name, TGlyphMap *found, int *skipped_regions) {
	HBITMAP bitmap = (HBITMAP)LoadImage(NULL, path.GetString(), IMAGE_BITMAP, 0, 0,
		LR_LOADFROMFILE | LR_CREATEDIBSECTION);
	if (bitmap == NULL) return false;
	BITMAP info;
	ZeroMemory(&info, sizeof(info));
	GetObject(bitmap, sizeof(info), &info);

	HDC hdc_screen = CreateDC("DISPLAY", NULL, NULL, NULL);
	HDC hdc = CreateCompatibleDC(hdc_screen);
	HDC hdc_region = CreateCompatibleDC(hdc_screen);
	HGDIOBJ old_bitmap = SelectObject(hdc, bitmap);
	CTransform trans;

	for (RMapCI region = p_tablemap->r$()->begin(); region != p_tablemap->r$()->end(); ++region) {
		int group = 0;
		if (!IsTextRegion(region->second.transform, &group)) continue;
		const int width = region->second.right - region->second.left + 1;
		const int height = region->second.bottom - region->second.top + 1;
		// Regions that don't fit into the image or into our buffers are ignored
		if (width <= 0 || height <= 0 || width > MAX_CHAR_WIDTH || height > MAX_CHAR_HEIGHT
			|| region->second.left < 0 || region->second.top < 0
			|| region->second.right >= info.bmWidth || region->second.bottom >= info.bmHeight) {
			(*skipped_regions)++;
			continue;
		}
		// The bitmap lives as long as one of the glyphs found in it needs it
		std::shared_ptr<HBITMAP__> bitmap_region(CreateCompatibleBitmap(hdc_screen, width, height),
			[](HBITMAP__ *handle) { if (handle != NULL) DeleteObject(handle); });
		if (!bitmap_region) continue;
		HGDIOBJ old_region_bitmap = SelectObject(hdc_region, bitmap_region.get());
		BitBlt(hdc_region, 0, 0, width, height, hdc, region->second.left, region->second.top, SRCCOPY);

		bool background[MAX_CHAR_WIDTH];
		for (int i = 0; i < MAX_CHAR_WIDTH; i++) background[i] = true;
		bool character[MAX_CHAR_WIDTH][MAX_CHAR_HEIGHT] = { false };
		CString text = "", separation = "";
		trans.TTypeTransform(region, hdc_region, &text, &separation, background, character);

		SelectObject(hdc_region, old_region_bitmap);	// deselect, the bitmap stays alive

		CollectUnknownGlyphs(region, file_name, group, width, height, bitmap_region,
			background, character, &trans, found);
	}

	SelectObject(hdc, old_bitmap);
	DeleteDC(hdc_region);
	DeleteDC(hdc);
	DeleteDC(hdc_screen);
	DeleteObject(bitmap);
	return true;
}

bool MoreFrequent(const SBatchGlyph &a, const SBatchGlyph &b) {
	if (a.occurrences != b.occurrences) return a.occurrences > b.occurrences;
	if (a.group != b.group) return a.group < b.group;
	return a.font.hexmash < b.font.hexmash;
}

// One part of a glyph that consists of several touching characters:
// the columns first..last (inclusive) of "whole", with the same rules as the
// text recognition uses (no empty columns at the edges, lowest pixel row = bit 0).
bool MakeGlyphPart(const SBatchGlyph &whole, int first, int last, SBatchGlyph *part) {
	const int count = std::min(whole.font.x_count, (int)MAX_SINGLE_CHAR_WIDTH);
	first = std::max(first, 0);
	last = std::min(last, count - 1);
	while (first <= last && whole.font.x[first] == 0) first++;
	while (last >= first && whole.font.x[last] == 0) last--;
	if (first > last) return false;

	unsigned int all_pixels = 0;
	for (int i = first; i <= last; ++i) all_pixels |= whole.font.x[i];
	if (all_pixels == 0) return false;
	int lowest = 0, highest = 31;
	while (((all_pixels >> lowest) & 1u) == 0) lowest++;
	while (((all_pixels >> highest) & 1u) == 0) highest--;

	*part = whole;
	part->font.x_count = last - first + 1;
	part->font.hexmash = "";
	for (int i = first; i <= last; ++i) {
		const unsigned int value = whole.font.x[i] >> lowest;	// bottom of this part = bit 0
		part->font.x[i - first] = value;
		CString hex;
		hex.Format("%x", value);
		part->font.hexmash += hex;
	}
	part->font.ch = '?';
	part->assigned = false;
	part->skipped = false;
	// Position inside the region (bit b is the row box.bottom - b)
	part->box.left = whole.box.left + first;
	part->box.right = whole.box.left + last;
	part->box.top = whole.box.bottom - highest;
	part->box.bottom = whole.box.bottom - lowest;
	return true;
}

}  // namespace

int CBatchFonts::ScanFolder(const CString &folder, std::vector<SBatchGlyph> *glyphs,
	int *unreadable_files, int *skipped_regions) {
	TGlyphMap found;
	int images = 0;
	*unreadable_files = 0;
	*skipped_regions = 0;

	CString base = folder;
	if (!base.IsEmpty() && base.Right(1) != "\\") base += "\\";
	WIN32_FIND_DATA find_data;
	HANDLE handle = FindFirstFile(base + "*.bmp", &find_data);
	if (handle != INVALID_HANDLE_VALUE) {
		do {
			if (find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
			if (ScanImage(base + find_data.cFileName, find_data.cFileName, &found, skipped_regions)) {
				images++;
			} else {
				(*unreadable_files)++;
			}
		} while (FindNextFile(handle, &find_data));
		FindClose(handle);
	}

	glyphs->clear();
	for (TGlyphMap::const_iterator it = found.begin(); it != found.end(); ++it) {
		glyphs->push_back(it->second);
	}
	std::stable_sort(glyphs->begin(), glyphs->end(), MoreFrequent);
	return images;
}

void CBatchFonts::Run(CWnd *parent) {
	COpenScrapeDoc *doc = COpenScrapeDoc::GetDocument();
	if (p_tablemap->r$()->empty()) {
		AfxMessageBox("Please load or create a tablemap with text regions first.", MB_ICONINFORMATION);
		return;
	}
	CFolderPickerDialog folder_dialog(NULL, 0, parent, 0);
	folder_dialog.m_ofn.lpstrTitle = "Folder with the bmp files (e.g. replay frames)";
	if (folder_dialog.DoModal() != IDOK) return;

	std::vector<SBatchGlyph> glyphs;
	int unreadable = 0, skipped_regions = 0, images = 0;
	{
		CWaitCursor wait;
		images = ScanFolder(folder_dialog.GetPathName(), &glyphs, &unreadable, &skipped_regions);
	}
	CString summary;
	summary.Format("%d image(s) scanned, %d unknown character shape(s) found.", images, (int)glyphs.size());
	if (unreadable > 0) summary.AppendFormat("\n%d file(s) could not be read.", unreadable);
	if (skipped_regions > 0) summary.AppendFormat("\n%d region(s) ignored (outside the image or too large).", skipped_regions);
	if (glyphs.empty()) {
		AfxMessageBox(summary, MB_ICONINFORMATION);
		return;
	}

	CDlgBatchFonts dialog(parent);
	dialog.glyphs = glyphs;
	dialog.image_count = images;
	if (dialog.DoModal() != IDOK) return;

	int added = 0;
	for (size_t i = 0; i < dialog.glyphs.size(); ++i) {
		const SBatchGlyph &glyph = dialog.glyphs[i];
		if (!glyph.assigned) continue;
		if (p_tablemap->t$_insert(glyph.group, glyph.font)) added++;
	}
	if (added > 0) {
		theApp.m_TableMapDlg->update_tree("Fonts");
		theApp.m_TableMapDlg->update_display();
		theApp.m_TableMapDlg->Invalidate(false);
		doc->SetModifiedFlag(true);
	}
	CString result;
	result.Format("%d character(s) added to the fonts.", added);
	AfxMessageBox(result, MB_ICONINFORMATION);
}

// ---------------------------------------------------------------------------
// Dialog
// ---------------------------------------------------------------------------

IMPLEMENT_DYNAMIC(CDlgBatchFonts, CDialog)

CDlgBatchFonts::CDlgBatchFonts(CWnd *parent)
	: CDialog(CDlgBatchFonts::IDD, parent), image_count(0), _current(0), _cut(0) {
}

CDlgBatchFonts::~CDlgBatchFonts() {
}

void CDlgBatchFonts::DoDataExchange(CDataExchange *pDX) {
	CDialog::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_BATCH_CHAR, _character);
	DDX_Control(pDX, IDC_BATCH_PIXELS, _pixels);
	DDX_Control(pDX, IDC_BATCH_INFO, _info);
	DDX_Control(pDX, IDC_BATCH_PROGRESS, _progress);
}

BEGIN_MESSAGE_MAP(CDlgBatchFonts, CDialog)
	ON_WM_DRAWITEM()
	ON_BN_CLICKED(IDC_BATCH_SKIP, &CDlgBatchFonts::OnBnClickedSkip)
	ON_BN_CLICKED(IDC_BATCH_BACK, &CDlgBatchFonts::OnBnClickedBack)
	ON_BN_CLICKED(IDC_BATCH_FINISH, &CDlgBatchFonts::OnBnClickedFinish)
	ON_BN_CLICKED(IDC_BATCH_SPLIT, &CDlgBatchFonts::OnBnClickedSplit)
END_MESSAGE_MAP()

int CDlgBatchFonts::AssignedCount() const {
	int count = 0;
	for (size_t i = 0; i < glyphs.size(); ++i) if (glyphs[i].assigned) count++;
	return count;
}

BOOL CDlgBatchFonts::OnInitDialog() {
	CDialog::OnInitDialog();
	LOGFONT lf;
	ZeroMemory(&lf, sizeof(lf));
	lf.lfHeight = 8;
	lf.lfQuality = PROOF_QUALITY;
	strcpy_s(lf.lfFaceName, 32, "Courier New");
	_fixed_font.CreateFontIndirect(&lf);
	_pixels.SetFont(&_fixed_font);
	_character.SetLimitText(1);
	SetDlgItemText(IDC_BATCH_HINT, "Characters glued together?\r\nLeft/Right = cut, Down = split");
	_current = 0;
	ShowGlyph();
	return FALSE;	// we set the focus ourselves
}

void CDlgBatchFonts::ShowGlyph(bool only_redraw) {
	if (_current < 0 || _current >= (int)glyphs.size()) return;
	const SBatchGlyph &glyph = glyphs[_current];
	if (!only_redraw) _cut = 0;
	const int columns = std::min(glyph.font.x_count, (int)MAX_SINGLE_CHAR_WIDTH);

	CString text;
	text.Format("Character %d of %d  -  font group t%d  -  seen %d time(s)\r\nExample: %s, region %s",
		_current + 1, (int)glyphs.size(), glyph.group, glyph.occurrences,
		glyph.example_file.GetString(), glyph.example_region.GetString());
	_info.SetWindowText(text);

	int skipped = 0;
	for (size_t i = 0; i < glyphs.size(); ++i) if (glyphs[i].skipped) skipped++;
	text.Format("Typed: %d   Skipped: %d   Remaining: %d   (%d images scanned)",
		AssignedCount(), skipped, (int)glyphs.size() - AssignedCount() - skipped, image_count);
	_progress.SetWindowText(text);

	// The pixels, same style as in the font editor: highest set pixel first.
	// A "|" shows where the glyph would be cut in two.
	bool pixel[MAX_SINGLE_CHAR_WIDTH][32] = { false };
	int top = -1;
	for (int x = 0; x < columns; ++x) {
		for (int bit = 0; bit < 32; ++bit) {
			pixel[x][bit] = (glyph.font.x[x] & (1u << bit)) != 0;
			if (pixel[x][bit] && bit > top) top = bit;
		}
	}
	CString grid;
	for (int bit = top; bit >= 0; --bit) {
		for (int x = 0; x < columns; ++x) {
			if (_cut > 0 && x == _cut) grid.Append("|");
			grid.Append(pixel[x][bit] ? "X" : " ");
		}
		grid.Append("\r\n");
	}
	_pixels.SetWindowText(grid);

	GetDlgItem(IDC_BATCH_BACK)->EnableWindow(_current > 0);
	GetDlgItem(IDC_BATCH_SPLIT)->EnableWindow(columns >= 2);
	GetDlgItem(IDC_BATCH_IMAGE)->Invalidate();
	_character.SetFocus();		// so that the arrow keys and Space keep working
	if (only_redraw) return;

	// Prefill when coming back to an already typed character
	CString typed = "";
	if (glyph.assigned) typed.Format("%c", glyph.font.ch);
	_character.SetWindowText(typed);
	_character.SetSel(0, -1);
}

// Touching characters come as one single glyph. Left/Right move the cut,
// Down (or the Split button) cuts it into two glyphs.
void CDlgBatchFonts::MoveCut(int delta) {
	if (_current < 0 || _current >= (int)glyphs.size()) return;
	const int columns = std::min(glyphs[_current].font.x_count, (int)MAX_SINGLE_CHAR_WIDTH);
	if (columns < 2) return;
	if (_cut == 0) _cut = columns / 2;		// first press: start in the middle
	else _cut += delta;
	_cut = std::max(1, std::min(columns - 1, _cut));
	ShowGlyph(true);
}

void CDlgBatchFonts::SplitAtCut() {
	if (_current < 0 || _current >= (int)glyphs.size()) return;
	const SBatchGlyph whole = glyphs[_current];
	const int columns = std::min(whole.font.x_count, (int)MAX_SINGLE_CHAR_WIDTH);
	if (columns < 2) return;
	if (_cut == 0) {		// show where it would be cut first
		MoveCut(0);
		return;
	}
	// A character typed before the cut is meant for the left part: keep it
	CString typed;
	_character.GetWindowText(typed);
	SBatchGlyph parts[2];
	bool have_part[2];
	have_part[0] = MakeGlyphPart(whole, 0, _cut - 1, &parts[0]);
	have_part[1] = MakeGlyphPart(whole, _cut, columns - 1, &parts[1]);
	if (!have_part[0] && !have_part[1]) {
		MessageBox("Nothing to split here.", "Batch fonts", MB_OK | MB_ICONINFORMATION);
		return;
	}

	// The glyph is replaced by its parts. A part that is already in the font
	// is dropped, a part that is already in the list only adds its occurrences.
	glyphs.erase(glyphs.begin() + _current);
	int insert_at = _current;
	for (int i = 0; i < 2; ++i) {
		if (!have_part[i]) continue;
		SBatchGlyph &part = parts[i];
		if (p_tablemap->t$(part.group)->find(part.font.hexmash) != p_tablemap->t$(part.group)->end()) continue;
		bool merged = false;
		for (size_t j = 0; j < glyphs.size(); ++j) {
			if (glyphs[j].group == part.group && glyphs[j].font.hexmash == part.font.hexmash) {
				glyphs[j].occurrences += part.occurrences;
				merged = true;
				break;
			}
		}
		if (!merged) glyphs.insert(glyphs.begin() + insert_at++, part);
	}

	if (glyphs.empty()) {
		MessageBox("No unknown characters left.", "Batch fonts", MB_OK | MB_ICONINFORMATION);
		EndDialog(IDCANCEL);
		return;
	}
	_current = std::min(_current, (int)glyphs.size() - 1);
	ShowGlyph();
	if (typed.GetLength() == 1 && have_part[0]) {
		_character.SetWindowText(typed);
		_character.SetSel(0, -1);
	}
}

void CDlgBatchFonts::OnBnClickedSplit() {
	SplitAtCut();
}

bool CDlgBatchFonts::ConfirmFinish() {
	CString text;
	text.Format("Add %d character(s) to the fonts?", AssignedCount());
	return AfxMessageBox(text, MB_YESNO | MB_ICONQUESTION) == IDYES;
}

void CDlgBatchFonts::NextGlyph() {
	if (_current + 1 < (int)glyphs.size()) {
		_current++;
		ShowGlyph();
	} else if (ConfirmFinish()) {
		EndDialog(IDOK);
	} else {
		ShowGlyph();	// stay on the last one, the user may go back
	}
}

// Enter: accept the typed character and go to the next one
void CDlgBatchFonts::OnOK() {
	CString typed;
	_character.GetWindowText(typed);
	if (typed.GetLength() != 1) {
		MessageBox("Type exactly one character, or click Skip.", "Batch fonts", MB_OK | MB_ICONINFORMATION);
		_character.SetFocus();
		return;
	}
	SBatchGlyph &glyph = glyphs[_current];
	glyph.font.ch = typed.GetAt(0);
	glyph.assigned = true;
	glyph.skipped = false;
	NextGlyph();
}

// Space skips the current character, so the whole batch can be done with the
// keyboard only: type a character + Enter, or Space if it can't be identified.
// (Space is never a character of a font, it is background.)
BOOL CDlgBatchFonts::PreTranslateMessage(MSG *msg) {
	if (msg->message == WM_KEYDOWN
		&& (msg->wParam == VK_LEFT || msg->wParam == VK_RIGHT || msg->wParam == VK_DOWN)) {
		CWnd *focus = GetFocus();
		if (focus != NULL && (focus->GetSafeHwnd() == _character.GetSafeHwnd()
			|| focus->GetSafeHwnd() == _pixels.GetSafeHwnd()
			|| focus->GetSafeHwnd() == GetSafeHwnd())) {
			if (msg->wParam == VK_LEFT) MoveCut(-1);
			else if (msg->wParam == VK_RIGHT) MoveCut(+1);
			else if ((msg->lParam & 0x40000000) == 0) SplitAtCut();	// no auto-repeat
			return TRUE;
		}
	}
	if (msg->message == WM_KEYDOWN && msg->wParam == VK_SPACE) {
		CWnd *focus = GetFocus();
		if (focus != NULL && (focus->GetSafeHwnd() == _character.GetSafeHwnd()
			|| focus->GetSafeHwnd() == _pixels.GetSafeHwnd()
			|| focus->GetSafeHwnd() == GetSafeHwnd())) {
			// Ignore the auto-repeat of a held key, it must not skip everything
			const bool is_repeat = (msg->lParam & 0x40000000) != 0;
			if (!is_repeat) OnBnClickedSkip();
			return TRUE;
		}
	}
	return CDialog::PreTranslateMessage(msg);
}

void CDlgBatchFonts::OnBnClickedSkip() {
	SBatchGlyph &glyph = glyphs[_current];
	glyph.assigned = false;
	glyph.skipped = true;
	NextGlyph();
}

void CDlgBatchFonts::OnBnClickedBack() {
	if (_current > 0) {
		_current--;
		ShowGlyph();
	}
}

void CDlgBatchFonts::OnBnClickedFinish() {
	if (AssignedCount() == 0) {
		OnCancel();
		return;
	}
	if (ConfirmFinish()) EndDialog(IDOK);
}

// Shows the text region where the character was found, magnified, with a red
// frame around the unknown character: that is what helps to recognise it.
void CDlgBatchFonts::OnDrawItem(int control_id, LPDRAWITEMSTRUCT draw_item) {
	if (control_id != IDC_BATCH_IMAGE) {
		CDialog::OnDrawItem(control_id, draw_item);
		return;
	}
	CDC *dc = CDC::FromHandle(draw_item->hDC);
	const CRect area(draw_item->rcItem);
	dc->FillSolidRect(area, RGB(48, 48, 48));
	if (_current < 0 || _current >= (int)glyphs.size()) return;
	const SBatchGlyph &glyph = glyphs[_current];
	if (!glyph.image || glyph.image_width <= 0 || glyph.image_height <= 0) return;

	// Integer zoom when it gets bigger (sharp pixels), smaller if it doesn't fit
	double scale = std::min((double)area.Width() / glyph.image_width,
		(double)area.Height() / glyph.image_height);
	if (scale >= 1.0) scale = std::min(12.0, floor(scale));
	const int width = std::max(1, (int)(glyph.image_width * scale + 0.5));
	const int height = std::max(1, (int)(glyph.image_height * scale + 0.5));
	const int x0 = area.left + (area.Width() - width) / 2;
	const int y0 = area.top + (area.Height() - height) / 2;

	CDC memory_dc;
	memory_dc.CreateCompatibleDC(dc);
	HGDIOBJ old_bitmap = ::SelectObject(memory_dc.GetSafeHdc(), glyph.image.get());
	dc->SetStretchBltMode(COLORONCOLOR);	// nearest neighbour: no blur
	dc->StretchBlt(x0, y0, width, height, &memory_dc, 0, 0, glyph.image_width, glyph.image_height, SRCCOPY);
	::SelectObject(memory_dc.GetSafeHdc(), old_bitmap);

	// Red frame around the unknown character
	CPen pen(PS_SOLID, 2, RGB(255, 0, 0));
	CPen *old_pen = dc->SelectObject(&pen);
	CBrush *old_brush = (CBrush *)dc->SelectStockObject(NULL_BRUSH);
	dc->Rectangle(x0 + (int)(glyph.box.left * scale) - 2,
		y0 + (int)(glyph.box.top * scale) - 2,
		x0 + (int)((glyph.box.right + 1) * scale) + 2,
		y0 + (int)((glyph.box.bottom + 1) * scale) + 2);
	dc->SelectObject(old_brush);
	dc->SelectObject(old_pen);

	// Yellow line: where the glyph would be cut
	if (_cut > 0) {
		CPen cut_pen(PS_SOLID, 2, RGB(255, 220, 0));
		old_pen = dc->SelectObject(&cut_pen);
		const int x = x0 + (int)((glyph.box.left + _cut) * scale);
		dc->MoveTo(x, y0 + (int)(glyph.box.top * scale) - 4);
		dc->LineTo(x, y0 + (int)((glyph.box.bottom + 1) * scale) + 4);
		dc->SelectObject(old_pen);
	}
}

void CDlgBatchFonts::OnCancel() {
	if (AssignedCount() > 0) {
		CString text;
		text.Format("Discard the %d character(s) you typed?", AssignedCount());
		if (AfxMessageBox(text, MB_YESNO | MB_ICONQUESTION) != IDYES) return;
	}
	EndDialog(IDCANCEL);
}
