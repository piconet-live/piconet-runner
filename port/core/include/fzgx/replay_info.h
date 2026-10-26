// What a replay file holds, for tools: the runner's `--replay-info FILE` (port/moderngekko/src/switches.cpp) prints it
// and starts no game. The file is read by the generated reader (protocol/replay.toml, generated/replay_file.g.h).
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <string>

namespace fzgx
{
// One JSON object: "version", "seat", "mask", "seats", "steps" (whole steps), "whole", "lineup" (the file has one),
// "header" (the header's JSON, its members in order of their names) and "results" (the results' JSON, or null). A file that is no
// replay of a version the game plays, or whose header is no JSON object, gives false and *error says why.
bool ReplayInfo(const std::string& path, std::string* json, std::string* error);
}  // namespace fzgx
