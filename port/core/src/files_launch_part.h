// One object of the launch file, whose members are taken by name: what the generated reader (launch_file.g.h, from
// protocol/launch.toml) and files_launch.cpp's own reading take every key with.
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "files.h"

#include <cmath>
#include <format>
#include <initializer_list>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace fzgx::launch
{

// One object of the file. Its members are taken by name; a member nobody took is unknown (Done). The first thing
// wrong in the whole file is kept in `problem`, with the key's full name.
class Part
{
public:
  Part(const picojson::object& members, std::string prefix, std::string* problem)
      : m_members(members), m_prefix(std::move(prefix)), m_problem(problem)
  {
  }

  void Refuse(const std::string& key, const std::string& what)
  {
    if (m_problem->empty())
      *m_problem = "\"" + m_prefix + key + "\" " + what;
  }

  // Whether the object has the member (taking nothing).
  bool Has(const char* key) const { return m_members.count(key) != 0; }

  const picojson::value* Take(const char* key)
  {
    m_taken.insert(key);
    const auto it = m_members.find(key);
    return it == m_members.end() ? nullptr : &it->second;
  }

  void Flag(const char* key, bool* out)
  {
    if (const picojson::value* v = Take(key))
    {
      if (v->is<bool>())
        *out = v->get<bool>();
      else
        Refuse(key, "is not true or false");
    }
  }

  void Whole(const char* key, u32* out, u32 low, u32 high)
  {
    if (const picojson::value* v = Take(key))
    {
      if (const std::optional<u32> n = WholeOf(*v, low, high))
        *out = *n;
      else
        Refuse(key, "is not a whole number from " + std::to_string(low) + " to " + std::to_string(high));
    }
  }

  void Number(const char* key, float* out, double low, double high)
  {
    if (const picojson::value* v = Take(key))
    {
      if (v->is<double>() && v->get<double>() >= low && v->get<double>() <= high)
        *out = static_cast<float>(v->get<double>());
      else
        Refuse(key, std::format("is not a number from {} to {}", low, high));
    }
  }

  // A text; an empty one only where `may_be_empty` says so.
  void Words(const char* key, std::string* out, bool may_be_empty = false)
  {
    if (const picojson::value* v = Take(key))
    {
      if (!v->is<std::string>())
        Refuse(key, "is not text");
      else if (v->get<std::string>().empty() && !may_be_empty)
        Refuse(key, "is empty");
      else
        *out = v->get<std::string>();
    }
  }

  void OneOf(const char* key, std::string* out, std::initializer_list<const char*> allowed)
  {
    const picojson::value* v = Take(key);
    if (!v)
      return;
    std::string list;
    for (const char* a : allowed)
    {
      if (v->is<std::string>() && v->get<std::string>() == a)
      {
        *out = a;
        return;
      }
      list += (list.empty() ? "" : ", ") + std::string(a);
    }
    Refuse(key, "is not one of " + list);
  }

  const picojson::object* Object(const char* key)
  {
    const picojson::value* v = Take(key);
    if (v && !v->is<picojson::object>())
      Refuse(key, "is not an object");
    return v && v->is<picojson::object>() ? &v->get<picojson::object>() : nullptr;
  }

  // A list of whole numbers, each from low to high, `least` to `most` of them.
  void Wholes(const char* key, std::vector<u32>* out, u32 low, u32 high, std::size_t least, std::size_t most)
  {
    const picojson::value* v = Take(key);
    if (!v)
      return;
    std::vector<u32> numbers;
    bool good = v->is<picojson::array>() && v->get<picojson::array>().size() >= least &&
                v->get<picojson::array>().size() <= most;
    for (std::size_t i = 0; good && i < v->get<picojson::array>().size(); ++i)
    {
      const std::optional<u32> n = WholeOf(v->get<picojson::array>()[i], low, high);
      good = n.has_value();
      numbers.push_back(n.value_or(0));
    }
    if (good)
      *out = numbers;
    else
      Refuse(key, std::format("is not a list of {} to {} whole numbers from {} to {}", least, most, low, high));
  }

  // Every member was asked for by name: one that was not is no setting.
  void Done()
  {
    for (const auto& [key, value] : m_members)
    {
      if (m_taken.count(key) == 0)
        Refuse(key, "is not a setting of a launch file");
    }
  }

  std::string Under(const char* key) const { return m_prefix + key + "."; }

private:
  static std::optional<u32> WholeOf(const picojson::value& v, u32 low, u32 high)
  {
    if (!v.is<double>())
      return std::nullopt;
    const double n = v.get<double>();
    if (!(n >= low && n <= high) || n != std::floor(n))
      return std::nullopt;
    return static_cast<u32>(n);
  }

  const picojson::object& m_members;
  std::string m_prefix;
  std::string* m_problem;
  std::set<std::string> m_taken;
};

// The "debug" part's keys the reader takes by hand (files_launch_debug.cpp; launch.toml's `hand` members), called by
// files_launch.cpp's ReadDebug after the generated reader's.
void ReadRedraw(Part& p, Debug* d, std::string* problem);
void RefuseRemovedKeys(Part& p);  // pacedLoad, drawSkipOff, hitShowOff, spectateLive (removed keys)
void ReadLanesOff(Part& p);  // the file's "lanes": 0 only (the multicore physics is parked on branch parked/lanes)
void ReadStandStill(Part& p, Debug* d, std::string* problem);
void ReadPartialKeys(Part& p, Debug* d, std::string* problem);
void ReadEmulatorSide(Part& p, Debug* d);
}  // namespace fzgx::launch
