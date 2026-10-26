// The files of the other parts' checkouts that the tests hold the core to (the protocol's handbook pages and lists of
// cases, the launcher's sources): FZGX_PROTOCOL_DIR and FZGX_LAUNCHER_DIR are the checkouts' folders from port/core,
// which run.sh takes from port/setup/pins.tsv (REPO_PROTOCOL, REPO_LAUNCHER: this repository's root today; on the
// split only those rows change). The tests run from port/core.
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>

// A file of the protocol's checkout (handbook/stat-packs.md, protocol/tests/vectors/stat-cases.tsv).
inline std::filesystem::path ProtocolFile(const char* path)
{
  return std::filesystem::path(FZGX_PROTOCOL_DIR) / path;
}

// A file of the launcher's checkout (FZGX_LAUNCHER_DIR, run.sh's `launcher`), as ProtocolFile.
inline std::filesystem::path LauncherFile(const char* path)
{
  return std::filesystem::path(FZGX_LAUNCHER_DIR) / path;
}
