// The per-machine rollback's log ("debug.partialRollback.log"): the adapter's lines (lanes_partial.h), kept and
// written out in pieces; and the count of the memory's moments it keeps a step's last run by.
// SPDX-License-Identifier: GPL-3.0-or-later

#include <memory>
#include <string>

#include "fzgx/core.h"
#include "internal.h"
#include "rollback.h"
#include "util.h"

namespace fzgx
{
namespace
{
std::unique_ptr<File> s_file;
std::string s_lines;
bool s_tried = false;
}  // namespace

void PartialFlush()
{
  if (s_file && s_file->IsOpen() && !s_lines.empty())
  {
    s_file->WriteBytes(s_lines.data(), s_lines.size());
    s_file->Flush();
  }
  s_lines.clear();
}

void PartialLine(const std::string& line)
{
  if (!s_tried)
  {
    s_tried = true;
    if (!TheOptions().debug.partial_log.empty())
      s_file = std::make_unique<File>(TheOptions().debug.partial_log, "wb");
  }
  if (!s_file)
    return;
  s_lines += line;
  s_lines += '\n';
  if (s_lines.size() > (1u << 20))
    PartialFlush();
}

u32 RollbackMoments()
{
  return net::Rollback::Moments();
}
}  // namespace fzgx
