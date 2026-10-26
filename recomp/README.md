# recomp: F-Zero GX's facts for recomp-kit

`recomp-kit` (a separate repository, `../recomp-kit` beside this one) holds the procedures for recompiling a
GameCube or Wii game with DolRecomp and ModernGekko: the playbook, the harness and the rules. A game supplies its
facts. These are F-Zero GX's.

- `manifest.toml`: the one file the kit's tools read. Addresses, names, sizes, hashes, settings and the current
  best build's switches, each with the note it came from. The format is the recomp-kit harness's manifest format.
- `machine-*.toml`: the harness's machine files, as `port/setup/install.sh` writes them into a work folder: that
  folder goes where they say `@WORK@` and `@WORK_WIN@`, LLVM (as `port/setup/tools.py` finds it, of the version
  `port/setup/pins.tsv` pins) where they say `@LLVM@` and `@LLVM_WIN@`. The harness reads the installed copies.
- The findings the manifest cites are in the project's research notes, which are not published; the kit's
  `templates/findings.md` is their shape.

Check the manifest after every edit:

    python3 ../recomp-kit/tools/manifest/validate.py recomp/manifest.toml --strict --questions

Rules for editing it:

- A value goes in only with its source: the note and section, or the script. Write the note first.
- A value the notes do not settle stays out and gets a `[[question]]`. The harness refuses a command that needs it.
- No game data: no code, dumps, keys or saved states. A saved state is named by its file name; the file stays in
  the work folder outside git.
