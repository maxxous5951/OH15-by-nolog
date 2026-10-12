"""Write frames to disk in the layout OHReplay expects.

Legacy layout, one pair of files per frame:

    frame000000.bmp   24-bit uncompressed bitmap of the window's client area
    frame000000.htm   first line = window title, then a tiny HTML wrapper

OHReplay loads ``frameNNNNNN.bmp`` and reads the first line of ``frameNNNNNN.htm``
as the title, so the default output stays byte-compatible with the old tool.

The image is always written before the ``.htm``. A frame therefore only counts as
complete once its ``.htm`` exists, which is also what resuming relies on.
"""

from __future__ import annotations

import json
import os
from datetime import UTC, datetime
from html import escape
from pathlib import Path
from typing import Literal

from PIL import Image

ImageFormat = Literal["bmp", "png"]

_FRAME_PREFIX = "frame"
_INDEX_WIDTH = 6


def frame_stem(index: int) -> str:
    return f"{_FRAME_PREFIX}{index:0{_INDEX_WIDTH}d}"


def _single_line(title: str) -> str:
    # OHReplay reads the title up to the first line break.
    return " ".join(title.split())


def _atomic_write_bytes(path: Path, data: bytes) -> None:
    tmp = path.with_name(path.name + ".part")
    tmp.write_bytes(data)
    os.replace(tmp, path)


class FrameWriter:
    def __init__(
        self,
        output_dir: Path,
        *,
        image_format: ImageFormat = "bmp",
        legacy_html: bool = True,
        metadata: bool = False,
    ) -> None:
        self.output_dir = Path(output_dir)
        self.image_format: ImageFormat = image_format
        self.legacy_html = legacy_html
        self.metadata = metadata

    def first_free_index(self, max_frames: int) -> int | None:
        """First index below ``max_frames`` that has no completed frame, if any."""
        for index in range(max_frames):
            if not self._marker_path(index).exists():
                return index
        return None

    def write(self, index: int, image: Image.Image, title: str) -> Path:
        """Write one frame and return the path of its image file."""
        self.output_dir.mkdir(parents=True, exist_ok=True)
        stem = frame_stem(index)
        image_path = self.output_dir / f"{stem}.{self.image_format}"

        rgb = image.convert("RGB")
        tmp_image = image_path.with_name(image_path.name + ".part")
        rgb.save(tmp_image, format=self.image_format.upper())
        os.replace(tmp_image, image_path)

        clean_title = _single_line(title)
        if self.metadata:
            meta = {
                "index": index,
                "title": clean_title,
                "width": rgb.width,
                "height": rgb.height,
                "captured_at": datetime.now(UTC).isoformat(timespec="milliseconds"),
                "image": image_path.name,
            }
            _atomic_write_bytes(
                self.output_dir / f"{stem}.json",
                (json.dumps(meta, ensure_ascii=False, indent=2) + "\n").encode("utf-8"),
            )
        if self.legacy_html:
            page = (
                f"{clean_title}\n"
                f"<html><head><title>{escape(clean_title)}</title></head>"
                f"<body><img src='{image_path.name}' /></body></html>\n"
            )
            _atomic_write_bytes(self.output_dir / f"{stem}.htm", page.encode("utf-8"))
        return image_path

    def _marker_path(self, index: int) -> Path:
        """File whose existence marks a frame as complete."""
        stem = frame_stem(index)
        if self.legacy_html:
            return self.output_dir / f"{stem}.htm"
        if self.metadata:
            return self.output_dir / f"{stem}.json"
        return self.output_dir / f"{stem}.{self.image_format}"
