import sys

import pytest

from fakes import ScriptedBackend
from openreplayshooter.cli import main, resolve_window
from openreplayshooter.model import CaptureError, WindowInfo


def test_list_prints_the_synthetic_window(capsys):
    assert main(["list", "--backend", "synthetic"]) == 0
    assert "Synthetic Table" in capsys.readouterr().out


@pytest.mark.parametrize("selector", ["synthetic", "TABLE", "0x1", "1"])
def test_shoot_accepts_title_fragment_or_handle(tmp_path, capsys, selector):
    code = main(
        [
            "shoot",
            "--backend",
            "synthetic",
            "-w",
            selector,
            "-o",
            str(tmp_path),
            "-i",
            "0",
            "-n",
            "3",
        ]
    )

    assert code == 0
    assert len(list(tmp_path.glob("frame*.htm"))) == 3
    assert "3 frame(s) written" in capsys.readouterr().out


def test_shoot_options_reach_the_writer(tmp_path):
    args = ["shoot", "--backend", "synthetic", "-w", "1", "-o", str(tmp_path), "-i", "0", "-n", "1"]
    assert main([*args, "--format", "png", "--no-legacy-html", "--metadata"]) == 0

    assert (tmp_path / "frame000000.png").exists()
    assert (tmp_path / "frame000000.json").exists()
    assert not (tmp_path / "frame000000.htm").exists()


def test_unknown_window_is_a_usage_error(tmp_path, capsys):
    code = main(["shoot", "--backend", "synthetic", "-w", "nope", "-o", str(tmp_path)])
    assert code == 2
    assert "No window title contains 'nope'" in capsys.readouterr().err
    assert not list(tmp_path.iterdir())


def test_full_folder_has_its_own_exit_code(tmp_path, capsys):
    args = ["shoot", "--backend", "synthetic", "-w", "1", "-o", str(tmp_path), "-i", "0", "-n", "2"]
    assert main(args) == 0
    assert main(args) == 4
    assert "already exist" in capsys.readouterr().err


@pytest.mark.parametrize("bad", [["-i", "-5"], ["-n", "0"]])
def test_out_of_range_numbers_are_rejected(tmp_path, capsys, bad):
    code = main(["shoot", "--backend", "synthetic", "-w", "1", "-o", str(tmp_path), *bad])
    assert code == 2


def test_unknown_backend_is_rejected_by_argparse():
    with pytest.raises(SystemExit) as excinfo:
        main(["list", "--backend", "nope"])
    assert excinfo.value.code == 2


@pytest.mark.skipif(sys.platform == "win32", reason="printwindow is available on Windows")
def test_printwindow_backend_is_refused_off_windows(capsys):
    assert main(["list", "--backend", "printwindow"]) == 2
    assert "only runs on Windows" in capsys.readouterr().err


def test_gui_without_pyside6_gives_an_install_hint(monkeypatch, capsys):
    # None in sys.modules makes any import of that name fail, cached or not.
    for name in ("PySide6", "PySide6.QtWidgets", "openreplayshooter.gui.main_window"):
        monkeypatch.setitem(sys.modules, name, None)

    assert main(["gui", "--backend", "synthetic"]) == 2
    assert 'pip install "openreplayshooter[gui]"' in capsys.readouterr().err


def test_ambiguous_title_lists_the_candidates():
    backend = ScriptedBackend([WindowInfo(10, "Table 1"), WindowInfo(11, "Table 2")])
    with pytest.raises(CaptureError) as excinfo:
        resolve_window(backend, "table")
    message = str(excinfo.value)
    assert "matches 2 windows" in message
    assert "Table 1" in message
    assert "Table 2" in message


def test_handle_wins_over_title_matching():
    backend = ScriptedBackend([WindowInfo(10, "Table 1"), WindowInfo(11, "Table 2")])
    assert resolve_window(backend, "0xB").title == "Table 2"
