# Pinned licence texts for the Windows package

`scripts/package.ps1` ships these with the application, so that a package built anywhere, including CI, carries every notice.

| File | What it is | Source |
| --- | --- | --- |
| `Qt-LICENSE.txt` | Qt's licence text (LGPLv3 and GPLv3, and the commercial terms), for the shipped Qt libraries. | `Licenses/LICENSE` of a Qt 6.11.2 online installation. CI's Qt, installed with `aqtinstall`, has no such folder. |

Update it when Qt changes licence. The OCR models' licence was pinned here too until pdfbookmark SDK 0.4.0 shipped it (`share/doc/pdfbookmark/licenses/PaddleOCR-PP-OCR-models.txt`, the same text; [PantaKoda/PDFMegine#6](https://github.com/PantaKoda/PDFMegine/issues/6)).
