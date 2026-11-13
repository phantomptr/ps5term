# Releasing PS5Term

Nothing has been published yet. These steps make a release that ProsperoStore
(via [ps5-homebrew-catalog](https://github.com/darkxex/ps5-homebrew-catalog)) can list.

## What the catalog requires

Checked against the catalog's `docs/submitting.md` and `catalog/verify.py` on 2026-11-07:

| Requirement | PS5Term |
|---|---|
| A published GitHub release with one `.zip` of the app folder, folder at the top | `dist/PS5Term-<version>.zip` contains `FAKE99260/` only |
| `sce_sys/param.json` committed, with `titleId` and a `contentVersion` raised every release | `FAKE99260`, `00.001.003` |
| A title ID no other listed app uses | `FAKE99260` is not listed (2026-11-07) |
| Release tag equals the record's `version` | tag `00.001.003` |
| 512×512 icon at a stable URL pinned to the tag | `sce_sys/icon0.png`, 512×512 PNG |
| License agrees with GitHub's detection | `GPL-3.0-or-later` matches `GPL-3.0` |
| Description: one plain sentence, 200 characters at most | 141 characters |
| Category | `utilities` |

## Steps

1. Raise `contentVersion` in `sce_sys/param.json` (skip for the first release).
2. Run the checks: `tools/test-terminal.sh` on macOS and Linux, the SSH suites
   (`tests/ssh/test_owned_server.py`, `test_owned_service.py`, `test_owned_sftp.py`,
   `test_owned_pq.py`) after `tools/build-owned-ssh-host.py`.
3. Build in the Linux container with the release definitions:

   ```sh
   APP_DEFINITIONS='PS5TERM_OWNED_SSH PS5TERM_OWNED_SFTP PS5TERM_LINUX_SUBSYSTEM PS5TERM_LINUX_PERSISTENCE PS5TERM_LINUX_SHARED_FOLDER PS5TERM_LINUX_NETWORK' bash tools/build.sh Folder
   python3 tools/package-app.py
   ```

4. Install that exact ZIP on a test console and check it opens, SSH works for both
   logins, and Linux starts.
5. Commit, tag the commit with the version (`00.001.003`), push, and publish a GitHub
   release with the ZIP as its only asset. Paste the release notes below.
6. Run `python3 tools/catalog-record.py` for the `apps/FAKE99260.json` record, or use
   the [submit page](https://prosperostore.net/submit/). Open the pull request from
   the account that owns `phantomptr/ps5term`.

Never replace a published asset. A fix is a new version with a new tag.

## Release notes: 00.001.005

- Free the space before uninstalling: Settings → Linux environments and storage now has
  **Delete Linux disk...** and **Delete backup**. The PS5 keeps PS5Term's data folder after an
  uninstall, and the Linux disk in it can be several GiB (issue #1).
- Removing an environment is safer: it can no longer touch PS5 files if that environment is
  opened while the removal runs.
- Growing the Linux disk now fails cleanly when the PS5 is short of space, and keeps your
  last good backup if an earlier growth did not finish.
- Fixes: a Linux session slot lost each time Linux stopped while the environments screen was
  refreshing; an install cut short no longer blocks the next one; `run` gives large programs
  time to load, can be cancelled with Ctrl+C while starting, and passes empty arguments.

## Release notes: 00.001.004

- New Linux environments screen (Settings → Linux environments): install and open Alpine
  or Ubuntu 24.04 LTS, each in its own tab, with `apk` or `apt` packages kept on the
  Linux disk. Make one the default for new Linux tabs, or remove it.
- Grow the Linux disk to 1, 2 or 4 GiB (Ubuntu wants 1 GiB). Growing again now works too.
- New `run` command in the built-in shell: run a PS5 program (ELF) in the tab, ncurses
  menus included, through your ELF loader on port 9021. Ctrl+C interrupts it.
- When you `exit` a Linux tab, it says the shell ended instead of "Linux stopped".
- Full-screen programs no longer show a grey background where they ask for black.

## Release notes: 00.001.003

- PS5Term now speaks all 21 languages of its website. New: Chinese (Simplified and
  Traditional), Japanese, Korean, Vietnamese, Arabic, Persian, Hindi, Bengali and Thai.
- It follows the PS5's language; choose another in Settings → Language. Linux and its programs
  stay as a normal Linux would be.

## Release notes: 00.001.002

- PS5Term speaks your language: German, Spanish, French, Italian, Polish, Portuguese
  (Brazil), Russian, Turkish, Hungarian and Indonesian. It follows the console's language;
  choose another in Settings → Language.
- Accented and Cyrillic letters now show in every text size.

## Release notes: 00.001.001

- Start Linux and Stop Linux in Settings now take you straight to the shell.
- About (Settings → About PS5Term, or F1 in Settings) shows the version, credits and the
  new website, [ps5term.phantomptr.com](https://ps5term.phantomptr.com/).

## Release notes draft: 00.001.000

First release.

- A terminal on the PS5's screen that works with a controller, a keyboard, or a
  keyboard and mouse. Tabs, split panes, search and copy/paste.
- A built-in shell for the PS5's own files, with system commands such as `ps`,
  `sysinfo` and `sysctl`. Type `help`.
- An SSH and SFTP server you can turn on in Settings. Log in as `ps5term` for the
  built-in shell, or as `linux` for a real Linux shell. Change the password in
  Settings, or add your own key.
- Optional Linux inside the app, with Alpine and `apk`, internet access, and your PS5
  files at `/mnt/ps5`. It runs interpreted, so it suits shell work rather than heavy
  computing.

Tested on a launch PS5 (CFI-1115A) on firmware 13.60 only.
