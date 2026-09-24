"""Generate the small synthetic PDF fixtures used by application checks.

Standard library only. Run from any directory:
    python tests/fixtures/make_fixtures.py
The output is deterministic, so regenerated files match the committed ones.
"""

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


def write_pdf(path: Path, pages):
    """pages: list of line lists. Writes a minimal PDF 1.4 with no outline."""
    objects = []  # bytes bodies, object number = index + 1
    n_pages = len(pages)
    # 1: catalog, 2: pages, 3: font, then per page: page object + content stream.
    page_ids = [4 + 2 * i for i in range(n_pages)]
    objects.append(b"<< /Type /Catalog /Pages 2 0 R >>")
    kids = " ".join(f"{pid} 0 R" for pid in page_ids)
    objects.append(f"<< /Type /Pages /Kids [{kids}] /Count {n_pages} >>".encode())
    objects.append(b"<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>")
    for i, lines in enumerate(pages):
        content_id = page_ids[i] + 1
        objects.append(
            (f"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] "
             f"/Resources << /Font << /F1 3 0 R >> >> /Contents {content_id} 0 R >>").encode())
        stream = page_stream(lines)
        objects.append(b"<< /Length %d >>\nstream\n" % len(stream) + stream + b"\nendstream")

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


def main():
    write_pdf(HERE / "title-page.pdf", [
        [(36, 72, 600, "Practical Library Engineering"),
         (18, 72, 560, "A Worked Example"),
         (14, 72, 480, "Jane Example")],
        [(10, 72, 700, "Copyright 2024 Jane Example. All rights reserved."),
         (10, 72, 684, "First published 2024.")],
        [(12, 72, 700, "Chapter 1"), (12, 72, 680, "This page is body text for the fixture.")],
    ])


if __name__ == "__main__":
    main()
