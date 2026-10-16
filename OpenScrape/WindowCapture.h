//******************************************************************************
//
// This file is part of the OpenHoldem project
//    Source code:           https://github.com/OpenHoldem/openholdembot/
//    Forums:                http://www.maxinmontreal.com/forums/index.php
//    Licensed under GPL v3: http://www.gnu.org/licenses/gpl.html
//
//******************************************************************************
//
// Purpose: Capturing the client area of the connected (poker table) window.
//   Two methods (View -> Window Capture), like in OpenHoldem:
//     * Original:    BitBlt from the device context of the window.
//                    The window must be visible and not covered.
//     * PrintWindow: Windows renders the window into a bitmap.
//                    Works for covered and DirectX/GPU-rendered windows.
//   If PrintWindow fails (e.g. minimized window), the original method is used.
//
//******************************************************************************

#ifndef INC_OPENSCRAPE_WINDOWCAPTURE_H
#define INC_OPENSCRAPE_WINDOWCAPTURE_H

// Copies the rectangle (src_x, src_y, width, height) of the client area
// of "window" to (0, 0) of dest_dc.
void CopyFromWindow(HWND window, HDC dest_dc, int width, int height, int src_x, int src_y);

#endif  // INC_OPENSCRAPE_WINDOWCAPTURE_H
