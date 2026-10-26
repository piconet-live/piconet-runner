# Contributing to piconet-runner

This repository holds F-Zero GX recompiled to native code and the port that runs it online.

## Building

The [README](README.md) lists the requirements, with versions, and how to build and check this repository. Clone it
with `--recurse-submodules`. Clone [piconet-launcher](https://github.com/piconet-live/piconet-launcher) beside it.
The build is CMake's, with a preset per platform: `cmake --preset windows`, then `cmake --build --preset windows`.

## Making a change

1. Fork the repository and make a branch for your change.
2. Keep a pull request to one change, with a message that says what it changes and why.
3. Open the pull request against `main`. `port/check.sh` must pass, and so must CI.

A change to a file format or a message the parts share (the relay's packets, the launch, replay and match files, the
status board) goes to [piconet-protocol](https://github.com/piconet-live/piconet-protocol) first; this repository
then takes it with a submodule bump.

Please discuss bigger changes on the [Discord](https://discord.gg/DcauFmsGh) before you start on them. Bugs and
small fixes can go straight to an issue or a pull request.

## Licence

piconet-runner is licensed under GPL-3.0-or-later. By opening a pull request you agree that your contribution is
licensed under the same terms.
