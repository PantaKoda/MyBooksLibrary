# Pinned licence texts for the Windows package

`scripts/package.ps1` ships these with the application, so that a package built anywhere, including CI, carries every notice.

| File | What it is | Source |
| --- | --- | --- |
| `Qt-LICENSE.txt` | Qt's licence text (LGPLv3 and GPLv3, and the commercial terms), for the shipped Qt libraries. | `Licenses/LICENSE` of a Qt 6.11.2 online installation. CI's Qt, installed with `aqtinstall`, has no such folder. |
| `PaddleOCR-LICENSE.txt` | Apache License 2.0, for the PaddleOCR PP-OCR models shipped in `models/`. | `LICENSE` of github.com/PaddlePaddle/PaddleOCR (line endings normalised to LF). pdfbookmark SDK 0.3.0 does not ship it: [PantaKoda/PDFMegine#6](https://github.com/PantaKoda/PDFMegine/issues/6). |

Update them when Qt, or the SDK's models, change licence.
