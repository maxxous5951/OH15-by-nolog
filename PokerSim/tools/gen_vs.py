#!/usr/bin/env python3
"""Génère PokerSim.sln et vs/*.vcxproj (Visual Studio 2022, x64, C++20).
Les listes de sources viennent du disque et sont vérifiées contre CMakeLists.txt (pas de dérive entre les deux systèmes de build).
Usage : python tools/gen_vs.py      (à relancer si des fichiers .cpp sont ajoutés)"""
import glob, os, re, sys, uuid

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
NS = uuid.UUID("6f1c2d3e-0000-4000-8000-706f6b657273")
def guid(name): return "{" + str(uuid.uuid5(NS, name)).upper() + "}"

def rel(p): return os.path.relpath(os.path.join(ROOT, p), os.path.join(ROOT, "vs")).replace("/", "\\")
def files(pattern): return sorted(os.path.relpath(f, ROOT).replace("\\", "/") for f in glob.glob(os.path.join(ROOT, pattern)))

CORE = files("src/*.cpp")
HEADERS = files("include/pokersim/*.hpp")
IMGUI = ["third_party/imgui/imgui.cpp", "third_party/imgui/imgui_draw.cpp", "third_party/imgui/imgui_tables.cpp",
         "third_party/imgui/imgui_widgets.cpp", "third_party/imgui/misc/imgui_stdlib.cpp"]
IMGUI_WIN = ["third_party/imgui/backends/imgui_impl_win32.cpp", "third_party/imgui/backends/imgui_impl_dx11.cpp"]

# --- cohérence avec CMakeLists.txt
cm = open(os.path.join(ROOT, "CMakeLists.txt"), encoding="utf-8").read()
m = re.search(r"add_library\(pokersim_core STATIC(.*?)\)", cm, re.S)
cmake_core = sorted(re.findall(r"src/[\w./]+\.cpp", m.group(1)))
if cmake_core != CORE:
    sys.exit("Incohérence src/*.cpp <-> CMakeLists.txt :\n  disque : %s\n  cmake  : %s" % (CORE, cmake_core))

PROJECTS = [
    # nom, type, sous-système, sources, références, bibliothèques, includes supplémentaires, fichiers sans avertissements
    dict(name="PokerSimGui", cfg="Application", sub="Windows", target="pokersim_gui",
         src=["apps/gui/main_win32.cpp", "apps/gui/gui_platform_win32.cpp"] + IMGUI_WIN, refs=["PokerSimGuiCore", "PokerSimCore"],
         libs="d3d11.lib;dxgi.lib;d3dcompiler.lib;dwmapi.lib;comdlg32.lib;ole32.lib;shell32.lib;uuid.lib;gdi32.lib",
         inc=["apps/gui", "third_party/imgui"], quiet=IMGUI_WIN),
    dict(name="PokerSimCli", cfg="Application", sub="Console", target="pokersim", src=["apps/cli/main.cpp"], refs=["PokerSimCore"], inc=[]),
    dict(name="PokerSimCore", cfg="StaticLibrary", target="PokerSimCore", src=CORE, hdr=HEADERS, refs=[], inc=[]),
    dict(name="PokerSimGuiCore", cfg="StaticLibrary", target="PokerSimGuiCore",
         src=["apps/gui/gui_app.cpp", "apps/gui/gui_draw.cpp", "apps/gui/gui_optimizer.cpp", "apps/gui/gui_optimizer_draw.cpp", "apps/gui/gui_process.cpp"] + IMGUI,
         hdr=["apps/gui/gui_app.hpp", "apps/gui/gui_platform_win32.hpp", "apps/gui/gui_optimizer.hpp", "apps/gui/gui_process.hpp"], refs=["PokerSimCore"],
         inc=["apps/gui", "third_party/imgui"], quiet=IMGUI),
    dict(name="OhDump", cfg="Application", sub="Console", target="ohdump", src=["apps/ohdump/main.cpp"], refs=["PokerSimCore"], inc=[], folder="Outils"),
    dict(name="Test_Core", cfg="Application", sub="Console", target="test_core", src=["tests/test_core.cpp"], refs=["PokerSimCore"], inc=[], folder="Tests"),
    dict(name="Test_OhLang", cfg="Application", sub="Console", target="test_ohlang", src=["tests/test_ohlang.cpp"], refs=["PokerSimCore"], inc=[], folder="Tests"),
    dict(name="Test_Prwin", cfg="Application", sub="Console", target="test_prwin", src=["tests/test_prwin.cpp"], refs=["PokerSimCore"], inc=[], folder="Tests"),
    dict(name="Test_Pt", cfg="Application", sub="Console", target="test_pt", src=["tests/test_pt.cpp"], refs=["PokerSimCore"], inc=[], folder="Tests"),
    dict(name="Test_GuiHeadless", cfg="Application", sub="Console", target="test_gui_headless", src=["apps/gui/headless_test.cpp"],
         refs=["PokerSimGuiCore", "PokerSimCore"], inc=["apps/gui", "third_party/imgui"], folder="Tests"),
]
for p in PROJECTS:
    for f in p["src"] + p.get("hdr", []):
        if not os.path.exists(os.path.join(ROOT, f)): sys.exit("Fichier manquant : " + f)

def vcxproj(p):
    g = guid(p["name"])
    inc = ["$(PS_ROOT)include", "$(PS_ROOT)third_party"] + ["$(PS_ROOT)" + i.replace("/", "\\") for i in p["inc"]]
    quiet = set(p.get("quiet", []))
    L = []
    a = L.append
    a('<?xml version="1.0" encoding="utf-8"?>')
    a('<Project DefaultTargets="Build" xmlns="http://schemas.microsoft.com/developer/msbuild/2003">')
    a('  <ItemGroup Label="ProjectConfigurations">')
    for c in ("Debug", "Release"):
        a('    <ProjectConfiguration Include="%s|x64"><Configuration>%s</Configuration><Platform>x64</Platform></ProjectConfiguration>' % (c, c))
    a('  </ItemGroup>')
    a('  <PropertyGroup Label="Globals">')
    a('    <VCProjectVersion>17.0</VCProjectVersion>')
    a('    <ProjectGuid>%s</ProjectGuid>' % g)
    a('    <RootNamespace>%s</RootNamespace>' % p["name"])
    a('    <Keyword>Win32Proj</Keyword>')
    a('    <WindowsTargetPlatformVersion>10.0</WindowsTargetPlatformVersion>')
    a('    <PS_ROOT>$(MSBuildThisFileDirectory)..\\</PS_ROOT>')
    a('  </PropertyGroup>')
    a('  <Import Project="$(VCTargetsPath)\\Microsoft.Cpp.Default.props" />')
    for c in ("Debug", "Release"):
        a('  <PropertyGroup Condition="\'$(Configuration)|$(Platform)\'==\'%s|x64\'" Label="Configuration">' % c)
        a('    <ConfigurationType>%s</ConfigurationType>' % p["cfg"])
        a('    <UseDebugLibraries>%s</UseDebugLibraries>' % ("true" if c == "Debug" else "false"))
        a('    <PlatformToolset>v143</PlatformToolset>')
        a('    <CharacterSet>Unicode</CharacterSet>')
        a('  </PropertyGroup>')
    a('  <Import Project="$(VCTargetsPath)\\Microsoft.Cpp.props" />')
    a('  <PropertyGroup>')
    a('    <OutDir>$(PS_ROOT)bin\\$(Configuration)\\</OutDir>')
    a('    <IntDir>$(PS_ROOT)obj\\$(ProjectName)\\$(Configuration)\\</IntDir>')
    a('    <TargetName>%s</TargetName>' % p["target"])
    a('  </PropertyGroup>')
    for c in ("Debug", "Release"):
        dbg = c == "Debug"
        a('  <ItemDefinitionGroup Condition="\'$(Configuration)|$(Platform)\'==\'%s|x64\'">' % c)
        a('    <ClCompile>')
        a('      <WarningLevel>Level3</WarningLevel>')
        a('      <ConformanceMode>true</ConformanceMode>')
        a('      <LanguageStandard>stdcpp20</LanguageStandard>')
        a('      <MultiProcessorCompilation>true</MultiProcessorCompilation>')
        a('      <AdditionalOptions>/utf-8 %(AdditionalOptions)</AdditionalOptions>')
        a('      <PreprocessorDefinitions>NOMINMAX;_CRT_SECURE_NO_WARNINGS;%s%%(PreprocessorDefinitions)</PreprocessorDefinitions>' % ("_DEBUG;" if dbg else "NDEBUG;"))
        a('      <AdditionalIncludeDirectories>%s;%%(AdditionalIncludeDirectories)</AdditionalIncludeDirectories>' % ";".join(inc))
        a('      <RuntimeLibrary>%s</RuntimeLibrary>' % ("MultiThreadedDebug" if dbg else "MultiThreaded"))
        a('      <Optimization>%s</Optimization>' % ("Disabled" if dbg else "MaxSpeed"))
        if not dbg:
            a('      <IntrinsicFunctions>true</IntrinsicFunctions>')
            a('      <FunctionLevelLinking>true</FunctionLevelLinking>')
        a('    </ClCompile>')
        if p["cfg"] == "Application":
            a('    <Link>')
            a('      <SubSystem>%s</SubSystem>' % p["sub"])
            a('      <GenerateDebugInformation>true</GenerateDebugInformation>')
            if not dbg:
                a('      <EnableCOMDATFolding>true</EnableCOMDATFolding>')
                a('      <OptimizeReferences>true</OptimizeReferences>')
            if p.get("libs"): a('      <AdditionalDependencies>%s;%%(AdditionalDependencies)</AdditionalDependencies>' % p["libs"])
            a('    </Link>')
        a('  </ItemDefinitionGroup>')
    a('  <ItemGroup>')
    for f in p["src"]:
        if f in quiet:
            a('    <ClCompile Include="%s"><WarningLevel>TurnOffAllWarnings</WarningLevel></ClCompile>' % rel(f))
        else:
            a('    <ClCompile Include="%s" />' % rel(f))
    a('  </ItemGroup>')
    if p.get("hdr"):
        a('  <ItemGroup>')
        for f in p["hdr"]: a('    <ClInclude Include="%s" />' % rel(f))
        a('  </ItemGroup>')
    if p["refs"]:
        a('  <ItemGroup>')
        for r in p["refs"]:
            a('    <ProjectReference Include="%s.vcxproj"><Project>%s</Project></ProjectReference>' % (r, guid(r)))
        a('  </ItemGroup>')
    a('  <Import Project="$(VCTargetsPath)\\Microsoft.Cpp.targets" />')
    a('</Project>')
    return "\n".join(L) + "\n"

def sln():
    CPP = "{8BC9CEB8-8B4A-11D0-8D11-00A0C91BC942}"
    FOLDER = "{2150E333-8FDC-42A3-9474-1A3956D46DE8}"
    folders = sorted({p["folder"] for p in PROJECTS if p.get("folder")})
    L = ["", "Microsoft Visual Studio Solution File, Format Version 12.00", "# Visual Studio Version 17",
         "VisualStudioVersion = 17.0.31903.59", "MinimumVisualStudioVersion = 10.0.40219.1"]
    for p in PROJECTS:
        L.append('Project("%s") = "%s", "vs\\%s.vcxproj", "%s"' % (CPP, p["name"], p["name"], guid(p["name"])))
        L.append("EndProject")
    for f in folders:
        L.append('Project("%s") = "%s", "%s", "%s"' % (FOLDER, f, f, guid("folder:" + f)))
        L.append("EndProject")
    L += ["Global", "\tGlobalSection(SolutionConfigurationPlatforms) = preSolution", "\t\tDebug|x64 = Debug|x64", "\t\tRelease|x64 = Release|x64", "\tEndGlobalSection",
          "\tGlobalSection(ProjectConfigurationPlatforms) = postSolution"]
    for p in PROJECTS:
        g = guid(p["name"])
        for c in ("Debug", "Release"):
            L.append("\t\t%s.%s|x64.ActiveCfg = %s|x64" % (g, c, c))
            L.append("\t\t%s.%s|x64.Build.0 = %s|x64" % (g, c, c))
    L += ["\tEndGlobalSection", "\tGlobalSection(SolutionProperties) = preSolution", "\t\tHideSolutionNode = FALSE", "\tEndGlobalSection",
          "\tGlobalSection(NestedProjects) = preSolution"]
    for p in PROJECTS:
        if p.get("folder"): L.append("\t\t%s = %s" % (guid(p["name"]), guid("folder:" + p["folder"])))
    L += ["\tEndGlobalSection", "\tGlobalSection(ExtensibilityGlobals) = postSolution", "\t\tSolutionGuid = %s" % guid("solution"), "\tEndGlobalSection", "EndGlobal", ""]
    return "\r\n".join(L)

os.makedirs(os.path.join(ROOT, "vs"), exist_ok=True)
for p in PROJECTS:
    with open(os.path.join(ROOT, "vs", p["name"] + ".vcxproj"), "w", encoding="utf-8", newline="\r\n") as f:
        f.write(vcxproj(p))
with open(os.path.join(ROOT, "PokerSim.sln"), "w", encoding="utf-8-sig", newline="") as f:
    f.write(sln())
print("généré : PokerSim.sln +", len(PROJECTS), "projets")
