//******************************************************************************
//
// This file is part of the OpenHoldem project
//    Source code:           https://github.com/OpenHoldem/openholdembot/
//    Forums:                http://www.maxinmontreal.com/forums/index.php
//    Licensed under GPL v3: http://www.gnu.org/licenses/gpl.html
//
//******************************************************************************
//
// Purpose: Hides the windows of this program from screenshots / screen capture
//   (SetWindowDisplayAffinity), like OpenHoldem does.
//     * Windows 10 (2004) and newer: window missing in the captured image
//     * Windows 7 .. older Windows 10: window shown as black rectangle
//   The windows stay normally visible on the monitor. This has no influence
//   on capturing the poker table (other process).
//
//******************************************************************************

using System;
using System.Runtime.InteropServices;

namespace OpenReplayShooter
{
    internal static class DisplayAffinity
    {
        private const uint WDA_NONE = 0x00000000;
        private const uint WDA_MONITOR = 0x00000001;
        private const uint WDA_EXCLUDEFROMCAPTURE = 0x00000011;
        private const int WH_CBT = 5;
        private const int HCBT_ACTIVATE = 5;
        private const int GWL_STYLE = -16;
        private const int WS_CHILD = 0x40000000;

        private delegate IntPtr HookProc(int nCode, IntPtr wParam, IntPtr lParam);
        private delegate bool EnumThreadWndProc(IntPtr hWnd, IntPtr lParam);

        [DllImport("user32.dll")]
        private static extern bool SetWindowDisplayAffinity(IntPtr hWnd, uint dwAffinity);
        [DllImport("user32.dll")]
        private static extern IntPtr SetWindowsHookEx(int idHook, HookProc lpfn, IntPtr hMod, uint dwThreadId);
        [DllImport("user32.dll")]
        private static extern bool UnhookWindowsHookEx(IntPtr hhk);
        [DllImport("user32.dll")]
        private static extern IntPtr CallNextHookEx(IntPtr hhk, int nCode, IntPtr wParam, IntPtr lParam);
        [DllImport("user32.dll")]
        private static extern bool EnumThreadWindows(uint dwThreadId, EnumThreadWndProc lpfn, IntPtr lParam);
        [DllImport("user32.dll")]
        private static extern int GetWindowLong(IntPtr hWnd, int nIndex);
        [DllImport("kernel32.dll")]
        private static extern uint GetCurrentThreadId();

        private static HookProc hookProc;       // keeps the delegate alive
        private static EnumThreadWndProc enumProc;
        private static IntPtr hook = IntPtr.Zero;
        private static bool blockScreenshots = false;
        private static uint workingMode = 0;    // 0 = unknown yet

        /// <summary>
        /// Call once from the GUI thread. Windows that get activated later
        /// (dialogs, message boxes) get the setting too.
        /// </summary>
        public static void Install(bool block)
        {
            blockScreenshots = block;
            if (hook == IntPtr.Zero)
            {
                hookProc = new HookProc(CbtProc);
                hook = SetWindowsHookEx(WH_CBT, hookProc, IntPtr.Zero, GetCurrentThreadId());
            }
            ApplyToAllWindows();
        }

        public static void SetBlockScreenshots(bool block)
        {
            blockScreenshots = block;
            ApplyToAllWindows();
        }

        public static void Uninstall()
        {
            if (hook != IntPtr.Zero)
            {
                UnhookWindowsHookEx(hook);
                hook = IntPtr.Zero;
            }
        }

        private static IntPtr CbtProc(int nCode, IntPtr wParam, IntPtr lParam)
        {
            if (nCode == HCBT_ACTIVATE)
            {
                ApplyToWindow(wParam);
            }
            return CallNextHookEx(hook, nCode, wParam, lParam);
        }

        private static void ApplyToAllWindows()
        {
            enumProc = delegate (IntPtr hWnd, IntPtr lParam)
            {
                ApplyToWindow(hWnd);
                return true;
            };
            EnumThreadWindows(GetCurrentThreadId(), enumProc, IntPtr.Zero);
        }

        public static void ApplyToWindow(IntPtr hWnd)
        {
            try
            {
                // Only top-level windows are supported
                if ((GetWindowLong(hWnd, GWL_STYLE) & WS_CHILD) != 0) return;
                if (!blockScreenshots)
                {
                    SetWindowDisplayAffinity(hWnd, WDA_NONE);
                    return;
                }
                if (workingMode != 0)
                {
                    SetWindowDisplayAffinity(hWnd, workingMode);
                    return;
                }
                // First use: best mode first, then fall back
                if (SetWindowDisplayAffinity(hWnd, WDA_EXCLUDEFROMCAPTURE))
                    workingMode = WDA_EXCLUDEFROMCAPTURE;
                else if (SetWindowDisplayAffinity(hWnd, WDA_MONITOR))
                    workingMode = WDA_MONITOR;
            }
            catch (EntryPointNotFoundException)
            {
                // Windows older than Windows 7: nothing we can do
            }
        }
    }
}
