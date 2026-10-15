#!/usr/bin/env python3
"""
Generates ModernIconData.h from Lucide SVG icons (https://lucide.dev, ISC license).

Every SVG primitive (path, circle, rect, line, polyline, polygon, ellipse) is
converted to SVG path data so that the C++ side only needs one tiny path parser.

Usage:  python3 gen_icons.py <path-to>/lucide-static/icons  > ../ModernIconData.h
"""
import re, sys, os
import xml.etree.ElementTree as ET

ICONS = [  # (C++ identifier, lucide file name)
  ("Plus","plus"), ("FileNew","file-plus"), ("FileOpen","file-up"), ("FolderOpen","folder-open"),
  ("Save","save"), ("SaveAs","file-down"), ("Exit","x"), ("Minimize","minus"), ("Close","x"),
  ("Settings","settings"), ("ViewLog","file-text"), ("ClearLog","trash-2"),
  ("ProblemSolver","life-buoy"), ("Manual","book-open"), ("Forums","users"), ("About","info"),
  ("Play","play"), ("Braces","braces"), ("Check","check"), ("Tag","tag"), ("Eye","eye"),
  ("Camera","camera"), ("ManualMode","pointer"), ("Help","circle-help"),
  ("Sun","sun"), ("Moon","moon"), ("Monitor","monitor"), ("ChevronRight","chevron-right"),
  ("Undo","undo-2"), ("Redo","redo-2"), ("Cut","scissors"), ("Copy","copy"),
  ("Paste","clipboard-paste"), ("Search","search"), ("WrapText","wrap-text"),
  ("Rename","pencil"), ("Delete","trash-2"), ("Files","files"), ("Lightbulb","lightbulb"),
]

def f(v): return float(v)

def circle_d(cx, cy, r):
    return f"M{cx-r:g} {cy:g}a{r:g} {r:g} 0 1 0 {2*r:g} 0a{r:g} {r:g} 0 1 0 {-2*r:g} 0z"

def ellipse_d(cx, cy, rx, ry):
    return f"M{cx-rx:g} {cy:g}a{rx:g} {ry:g} 0 1 0 {2*rx:g} 0a{rx:g} {ry:g} 0 1 0 {-2*rx:g} 0z"

def rect_d(x, y, w, h, rx, ry):
    if rx == 0 and ry == 0:
        return f"M{x:g} {y:g}h{w:g}v{h:g}h{-w:g}z"
    rx = min(rx, w/2); ry = min(ry, h/2)
    return (f"M{x+rx:g} {y:g}h{w-2*rx:g}a{rx:g} {ry:g} 0 0 1 {rx:g} {ry:g}v{h-2*ry:g}"
            f"a{rx:g} {ry:g} 0 0 1 {-rx:g} {ry:g}h{-(w-2*rx):g}a{rx:g} {ry:g} 0 0 1 {-rx:g} {-ry:g}"
            f"v{-(h-2*ry):g}a{rx:g} {ry:g} 0 0 1 {rx:g} {-ry:g}z")

def points(s):
    nums = [float(n) for n in re.findall(r'-?\d*\.?\d+(?:e-?\d+)?', s)]
    return list(zip(nums[0::2], nums[1::2]))

def convert(svg_file):
    root = ET.parse(svg_file).getroot()
    out = []
    for el in root.iter():
        tag = el.tag.split('}')[-1]
        a = el.attrib
        if tag == 'path':       out.append(a['d'])
        elif tag == 'circle':   out.append(circle_d(f(a['cx']), f(a['cy']), f(a['r'])))
        elif tag == 'ellipse':  out.append(ellipse_d(f(a['cx']), f(a['cy']), f(a['rx']), f(a['ry'])))
        elif tag == 'rect':
            rx = f(a.get('rx', a.get('ry', 0))); ry = f(a.get('ry', a.get('rx', 0)))
            out.append(rect_d(f(a.get('x', 0)), f(a.get('y', 0)), f(a['width']), f(a['height']), rx, ry))
        elif tag == 'line':
            out.append(f"M{f(a['x1']):g} {f(a['y1']):g}L{f(a['x2']):g} {f(a['y2']):g}")
        elif tag in ('polyline', 'polygon'):
            p = points(a['points'])
            d = "M" + "L".join(f"{x:g} {y:g}" for x, y in p)
            out.append(d + ("z" if tag == 'polygon' else ""))
    return out

def main():
    src = sys.argv[1]
    print("// GENERATED FILE - do not edit. Created by tools/gen_icons.py")
    print("// Icons: Lucide (https://lucide.dev), ISC License, Copyright (c) Lucide Contributors.")
    print("// All icons use a 24x24 grid, stroke-width 2, round caps/joins, no fill.")
    print("#pragma once\n")
    print("namespace modernui {\n")
    print("enum class Icon {\n  None = 0,")
    for ident, _ in ICONS: print(f"  {ident},")
    print("  Count_\n};\n")
    print("struct IconDef { const char* const* paths; int count; };\n")
    for ident, name in ICONS:
        paths = convert(os.path.join(src, name + ".svg"))
        print(f"static const char* const kIcon_{ident}[] = {{")
        for p in paths: print(f'  "{p}",')
        print("};")
    print("\nstatic const IconDef kIconDefs[] = {\n  { nullptr, 0 },  // Icon::None")
    for ident, _ in ICONS:
        print(f"  {{ kIcon_{ident}, (int)(sizeof(kIcon_{ident}) / sizeof(kIcon_{ident}[0])) }},")
    print("};\n\n}  // namespace modernui")

if __name__ == "__main__":
    main()
