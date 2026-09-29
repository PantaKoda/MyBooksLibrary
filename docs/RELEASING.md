# Releasing

Every change reaches `main` through a pull request: a branch, then the PR, then CI (`build-and-test`), then review, then the merge. Releases **group** the merged pull requests into a version that can be downloaded from the repository's **Releases** page.

## What CI checks on every pull request

`.github/workflows/ci.yml` runs:
- **`scripts/verify.ps1`:** text checks, the build, every test, and the application smoke checks.
- **`scripts/package.ps1`:** the Windows package, its notices and imports, and the app run from a copy outside the repository.

A change that breaks packaging therefore fails its own pull request, not a release. Qt and the pdfbookmark SDK are set up once, pinned, in `.github/actions/setup`, which the release uses too.

## Cutting a release

1. **Pick the version.** Use `X.Y.Z`: the patch number for fixes, the minor number for new features.
2. **Open a release pull request** that:
   - sets `project(MyBooksLibrary VERSION X.Y.Z …)` in `CMakeLists.txt`;
   - adds `docs/releases/vX.Y.Z.md`, the release notes: what is new, what was fixed, and known limits, in plain words for users.

   It goes through CI and review like any other change.
3. **After it is merged**, tag the merge commit on `main` and push the tag:

   ```bash
   git switch main
   git pull --ff-only
   git tag -a vX.Y.Z -m "MyBooksLibrary X.Y.Z"
   git push origin vX.Y.Z
   ```

4. **`.github/workflows/release.yml` runs.** It publishes only if everything passes:
   - the tag is on `main` and matches the version in `CMakeLists.txt`;
   - the release notes exist;
   - `verify.ps1` and `package.ps1` pass.

   It then creates the release "MyBooksLibrary X.Y.Z" with `MyBooksLibrary-X.Y.Z-win64.zip` and its `.sha256`. The notes come first, followed by GitHub's list of the pull requests merged since the previous release.

If the workflow fails, nothing is published. Fix the problem through a pull request, delete the tag (`git push origin :refs/tags/vX.Y.Z`, then `git tag -d vX.Y.Z`), and tag again.

## What a user downloads

A zip that runs where it is unpacked: `appMyBooksLibrary.exe`, with Qt, the pdfbookmark SDK, its OCR models, the Visual C++ runtime, `NOTICE.txt` and `licenses/` (docs/BUILDING.md, "Windows package").
- **Installation:** there is no installer. Unpack the zip and run the executable.
- **Code signing:** none, so Windows SmartScreen may warn the first time.
- **Windows N editions:** they need the Media Feature Pack ([PantaKoda/PDFMegine#7](https://github.com/PantaKoda/PDFMegine/issues/7)).
- **The user's library** lives in their data folder, never next to the executable, so a new version can replace the old one.
