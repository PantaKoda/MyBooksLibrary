"""Generate the small synthetic PDF fixtures used by application checks.

Standard library only. Run from any directory:
    python tests/fixtures/make_fixtures.py
The output is deterministic, so regenerated files match the committed ones.
"""

import zlib
from pathlib import Path

HERE = Path(__file__).resolve().parent


def pdf_string(text: str) -> str:
    return "(" + text.replace("\\", "\\\\").replace("(", "\\(").replace(")", "\\)") + ")"


def page_stream(lines):
    """lines: (font size, x, y, text) with Helvetica."""
    parts = []
    for size, x, y, text in lines:
        parts.append(f"BT /F1 {size} Tf {x} {y} Td {pdf_string(text)} Tj ET")
    return "\n".join(parts).encode("latin-1")


def stream_object(dictionary: bytes, data: bytes) -> bytes:
    return dictionary + b"\nstream\n" + data + b"\nendstream"


def write_objects(path: Path, objects):
    """objects: bytes bodies; object number = index + 1; object 1 is the catalog."""
    out = bytearray(b"%PDF-1.4\n%\xe2\xe3\xcf\xd3\n")
    offsets = []
    for number, body in enumerate(objects, start=1):
        offsets.append(len(out))
        out += b"%d 0 obj\n" % number + body + b"\nendobj\n"
    xref = len(out)
    out += b"xref\n0 %d\n0000000000 65535 f \n" % (len(objects) + 1)
    for offset in offsets:
        out += b"%010d 00000 n \n" % offset
    out += b"trailer\n<< /Size %d /Root 1 0 R >>\nstartxref\n%d\n%%%%EOF\n" % (len(objects) + 1, xref)
    path.write_bytes(bytes(out))


def write_pdf(path: Path, pages, catalog_extra: str = ""):
    """pages: list of line lists. Writes a minimal PDF 1.4 with no outline.
    catalog_extra: more catalog entries, e.g. a /PageLabels number tree."""
    objects = []
    n_pages = len(pages)
    # 1: catalog, 2: pages, 3: font, then per page: page object + content stream.
    page_ids = [4 + 2 * i for i in range(n_pages)]
    objects.append(f"<< /Type /Catalog /Pages 2 0 R{catalog_extra} >>".encode())
    kids = " ".join(f"{pid} 0 R" for pid in page_ids)
    objects.append(f"<< /Type /Pages /Kids [{kids}] /Count {n_pages} >>".encode())
    objects.append(b"<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>")
    for i, lines in enumerate(pages):
        content_id = page_ids[i] + 1
        objects.append(
            (f"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] "
             f"/Resources << /Font << /F1 3 0 R >> >> /Contents {content_id} 0 R >>").encode())
        stream = page_stream(lines)
        objects.append(stream_object(b"<< /Length %d >>" % len(stream), stream))
    write_objects(path, objects)


def write_image_pdf(path: Path, page_count: int, width: int = 850, height: int = 1100):
    """Image-only pages without a text layer, like a scan. Dark bars imitate
    text lines, so the SDK must render pages and attempt OCR."""
    objects = []
    # 1: catalog, 2: pages, then per page: page object, image, content stream.
    page_ids = [3 + 3 * i for i in range(page_count)]
    objects.append(b"<< /Type /Catalog /Pages 2 0 R >>")
    kids = " ".join(f"{pid} 0 R" for pid in page_ids)
    objects.append(f"<< /Type /Pages /Kids [{kids}] /Count {page_count} >>".encode())
    for i in range(page_count):
        rows = bytearray()
        for y in range(height):
            row = bytearray([255]) * width
            if (y // 22) % 3 == 0 and 120 < y < 420:  # A few text-like lines.
                seed = (y // 22) * 31 + i * 7
                x = 90
                while x < width - 90:
                    end = min(x + 30 + (seed * 13 + x) % 70, width - 90)
                    row[x:end] = bytes([32]) * (end - x)
                    x = end + 14
            rows += row
        data = zlib.compress(bytes(rows), 9)
        image_id, content_id = page_ids[i] + 1, page_ids[i] + 2
        objects.append(
            (f"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] "
             f"/Resources << /XObject << /Im0 {image_id} 0 R >> >> /Contents {content_id} 0 R >>").encode())
        objects.append(stream_object(
            (f"<< /Type /XObject /Subtype /Image /Width {width} /Height {height} /ColorSpace /DeviceGray "
             f"/BitsPerComponent 8 /Filter /FlateDecode /Length {len(data)} >>").encode(), data))
        content = b"q 612 0 0 792 0 0 cm /Im0 Do Q"
        objects.append(stream_object(b"<< /Length %d >>" % len(content), content))
    write_objects(path, objects)


# Printed TOC: (number, title, printed page). Body page n is physical index n + 2.
CONTENTS = [
    ("1", "Introduction", 1),
    ("2", "Getting Started", 5),
    ("2.1", "Installing the Tools", 6),
    ("3", "Networking with TCP/IP", 12),
    ("4", "Summary", 20),
]
BODY_PAGES = 24


def contents_book_pages():
    pages = [
        [(32, 72, 600, "Library Systems in Practice"),
         (14, 72, 540, "Alex Sample")],
        [(10, 72, 700, "Copyright 2021 Alex Sample."),
         (10, 72, 684, "Second Edition. First published 2019.")],
    ]
    toc = [(20, 72, 720, "Contents")]
    y = 680
    for number, title, page in CONTENTS:
        indent = 96 if "." in number else 72
        toc.append((12, indent, y, f"{number} {title}"))
        toc.append((12, 520, y, str(page)))
        y -= 24
    pages.append(toc)
    starts = {page: (number, title) for number, title, page in CONTENTS}
    for printed in range(1, BODY_PAGES + 1):
        lines = []
        if printed in starts:
            number, title = starts[printed]
            lines.append((18, 72, 700, f"{number} {title}"))
        lines.append((11, 72, 640, f"Body text of printed page {printed}."))
        lines.append((10, 300, 40, str(printed)))
        pages.append(lines)
    return pages


# A publisher's PDF without its blank pages: each chapter ends on an odd
# printed page and the blank even page after it is left out, so the printed
# numbers skip one at every chapter end. The PDF's page labels record the
# skips. Body page n is physical index n + 2 in chapter 1, n + 1 in chapter 2,
# n in chapter 3 and n - 1 in chapter 4.
DROPPED_CONTENTS = [
    ("1", "Introduction", 1),
    ("2", "Getting Started", 9),
    ("2.1", "Installing the Tools", 11),
    ("3", "Networking with TCP/IP", 17),
    ("4", "Summary", 25),
]
DROPPED_BODY = [*range(1, 8), *range(9, 16), *range(17, 24), *range(25, 31)]
# Front matter i-iii, then decimal ranges starting at printed 1, 9, 17 and 25.
DROPPED_PAGE_LABELS = (" /PageLabels << /Nums [0 << /S /r >> 3 << /S /D >> "
                       "10 << /S /D /St 9 >> 17 << /S /D /St 17 >> 24 << /S /D /St 25 >>] >>")


def dropped_pages_book_pages():
    pages = [
        [(32, 72, 600, "Library Systems Without Blanks"),
         (14, 72, 540, "Alex Sample")],
        [(10, 72, 700, "Copyright 2022 Alex Sample."),
         (10, 72, 684, "First published 2022.")],
    ]
    toc = [(20, 72, 720, "Contents")]
    y = 680
    for number, title, page in DROPPED_CONTENTS:
        indent = 96 if "." in number else 72
        toc.append((12, indent, y, f"{number} {title}"))
        toc.append((12, 520, y, str(page)))
        y -= 24
    pages.append(toc)
    starts = {page: (number, title) for number, title, page in DROPPED_CONTENTS}
    for printed in DROPPED_BODY:
        lines = []
        if printed in starts:
            number, title = starts[printed]
            lines.append((18, 72, 700, f"{number} {title}"))
        lines.append((11, 72, 640, f"Body text of printed page {printed}."))
        lines.append((10, 300, 40, str(printed)))
        pages.append(lines)
    return pages


def main():
    # Title page, copyright page and one body page.
    write_pdf(HERE / "title-page.pdf", [
        [(36, 72, 600, "Practical Library Engineering"),
         (18, 72, 560, "A Worked Example"),
         (14, 72, 480, "Jane Example")],
        [(10, 72, 700, "Copyright 2024 Jane Example. All rights reserved."),
         (10, 72, 684, "First published 2024.")],
        [(12, 72, 700, "Chapter 1"), (12, 72, 680, "This page is body text for the fixture.")],
    ])
    # Printed contents page with right-aligned page numbers and a 24-page body.
    write_pdf(HERE / "contents-book.pdf", contents_book_pages())
    # The same kind of contents, with the printed numbering skipping pages.
    write_pdf(HERE / "dropped-pages-book.pdf", dropped_pages_book_pages(), DROPPED_PAGE_LABELS)
    # Scan-like pages: exercises SDK rendering and OCR.
    write_image_pdf(HERE / "image-only.pdf", 4)


if __name__ == "__main__":
    main()
