import json
import struct

import pytest
from PIL import Image

from openreplayshooter.writer import FrameWriter, frame_stem


def make_image(size=(64, 48), color=(10, 200, 30)):
    return Image.new("RGB", size, color)


def test_frame_stem_is_zero_padded_to_six_digits():
    assert frame_stem(0) == "frame000000"
    assert frame_stem(123456) == "frame123456"


def test_legacy_layout_matches_what_ohreplay_reads(tmp_path):
    FrameWriter(tmp_path).write(3, make_image(), "My Table - Hold'em")

    bmp = tmp_path / "frame000003.bmp"
    htm = tmp_path / "frame000003.htm"
    assert bmp.exists()
    assert htm.exists()

    # OHReplay takes the title from the first line of the .htm.
    assert htm.read_text(encoding="utf-8").splitlines()[0] == "My Table - Hold'em"
    assert "frame000003.bmp" in htm.read_text(encoding="utf-8")

    # OHReplay loads the bitmap with LoadImage: plain 24-bit, uncompressed DIB.
    header = bmp.read_bytes()[:54]
    bits_per_pixel, compression = struct.unpack_from("<HI", header, 28)
    assert (bits_per_pixel, compression) == (24, 0)
    with Image.open(bmp) as image:
        assert image.size == (64, 48)
        assert image.mode == "RGB"


def test_title_is_kept_on_one_line_and_escaped_in_html(tmp_path):
    FrameWriter(tmp_path).write(0, make_image(), "Line one\r\nLine <two>")

    lines = (tmp_path / "frame000000.htm").read_text(encoding="utf-8").splitlines()
    assert lines[0] == "Line one Line <two>"
    assert "<title>Line one Line &lt;two&gt;</title>" in lines[1]


def test_alpha_is_dropped(tmp_path):
    rgba = Image.new("RGBA", (8, 8), (1, 2, 3, 128))
    FrameWriter(tmp_path).write(0, rgba, "t")
    with Image.open(tmp_path / "frame000000.bmp") as image:
        assert image.mode == "RGB"


def test_no_partial_files_are_left_behind(tmp_path):
    FrameWriter(tmp_path, metadata=True).write(0, make_image(), "t")
    assert not list(tmp_path.glob("*.part"))


def test_png_format_and_html_reference(tmp_path):
    FrameWriter(tmp_path, image_format="png").write(1, make_image(), "t")
    assert (tmp_path / "frame000001.png").exists()
    assert not (tmp_path / "frame000001.bmp").exists()
    assert "frame000001.png" in (tmp_path / "frame000001.htm").read_text(encoding="utf-8")


def test_output_folder_is_created(tmp_path):
    target = tmp_path / "a" / "b"
    FrameWriter(target).write(0, make_image(), "t")
    assert (target / "frame000000.htm").exists()


def test_metadata_sidecar(tmp_path):
    FrameWriter(tmp_path, metadata=True).write(2, make_image((20, 10)), "Table 7")
    meta = json.loads((tmp_path / "frame000002.json").read_text(encoding="utf-8"))
    assert meta["index"] == 2
    assert meta["title"] == "Table 7"
    assert (meta["width"], meta["height"]) == (20, 10)
    assert meta["image"] == "frame000002.bmp"
    assert meta["captured_at"].endswith("+00:00")


def test_first_free_index_follows_completed_frames(tmp_path):
    writer = FrameWriter(tmp_path)
    assert writer.first_free_index(5) == 0
    for index in (0, 1, 2):
        writer.write(index, make_image(), "t")
    assert writer.first_free_index(5) == 3


def test_first_free_index_finds_gaps_and_reports_full(tmp_path):
    writer = FrameWriter(tmp_path)
    for index in (0, 2):
        writer.write(index, make_image(), "t")
    assert writer.first_free_index(3) == 1
    writer.write(1, make_image(), "t")
    assert writer.first_free_index(3) is None


def test_an_image_without_its_htm_is_not_a_completed_frame(tmp_path):
    # Simulates a crash between the image and the .htm being written.
    (tmp_path / "frame000000.bmp").write_bytes(b"BM")
    assert FrameWriter(tmp_path).first_free_index(3) == 0


@pytest.mark.parametrize(
    ("legacy_html", "metadata", "marker"),
    [(False, False, "bmp"), (False, True, "json"), (True, True, "htm")],
)
def test_resume_marker_matches_enabled_outputs(tmp_path, legacy_html, metadata, marker):
    writer = FrameWriter(tmp_path, legacy_html=legacy_html, metadata=metadata)
    writer.write(0, make_image(), "t")
    assert (tmp_path / f"frame000000.{marker}").exists()
    assert writer.first_free_index(3) == 1
