# MyBooksLibrary

A desktop library for your PDF books, for Windows.
- **Finds** each book's title, authors, edition and year, and its printed table of contents, with each chapter matched to its page.
- **Searches** across all your books by title, author and chapter, and **opens a chapter at its page** in the built-in reader.

Your original files are never changed.

## Download

The Windows app is on the **[Releases](https://github.com/PantaKoda/MyBooksLibrary/releases)** page: unpack the zip and run `appMyBooksLibrary.exe`.

The **[user guide](docs/USER_GUIDE.md)** explains every feature and the limitations, and is also in the zip.

## For developers

- [docs/BUILDING.md](docs/BUILDING.md): building, tests, `scripts/verify.ps1`, the Windows package and CI.
- [docs/RELEASING.md](docs/RELEASING.md): how changes are grouped into releases.
- [AGENTS.md](AGENTS.md): the architecture, rules and workflow.
- [docs/IMPLEMENTATION_PROGRESS.md](docs/IMPLEMENTATION_PROGRESS.md) and [docs/DECISIONS.md](docs/DECISIONS.md): progress and design decisions.

It uses [Qt](https://www.qt.io/) and the pdfbookmark SDK (PantaKoda/PDFMegine). Third-party licences are listed in the package's `NOTICE.txt`.
