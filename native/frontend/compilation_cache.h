// SPDX-License-Identifier: 0BSD
// Copyright (C) 2026 by I.C.KaZe
#pragma once
#include <array>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace workbench
{
// Immutable snapshots. Compilation and dependency reads never hold the cache lock.
// External files are compared byte-for-byte, including same-size/same-time edits.
template<class Value> class CompilationCache
{
  public:
    using Bytes = std::vector<unsigned char>;
    using Dependencies = std::vector<std::pair<std::string, Bytes>>;
    using Loader = std::function<Bytes(const std::string&)>;
    std::shared_ptr<const Value> get(const std::string& path, const std::string& text,
        bool steps, const Loader& loader) const
    {
        std::array<std::shared_ptr<const Entry>, 2> entries;
        { std::lock_guard<std::mutex> lock(mutex_); entries = entries_; }
        // Prefer full analysis: its MDX and navigation offsets also serve F5.
        for (const auto& entry : entries)
        {
            if (!entry || entry->path != path || entry->text != text || (steps && !entry->steps)) continue;
            bool valid = true;
            try
            {
                for (const auto& dependency : entry->dependencies)
                    if (loader(dependency.first) != dependency.second) { valid = false; break; }
            }
            catch (...) { valid = false; }
            if (valid) return entry->value;
        }
        return {};
    }
    void put(std::string path, std::string text, bool steps, Dependencies dependencies,
        std::shared_ptr<const Value> value)
    {
        auto entry = std::make_shared<Entry>(Entry{std::move(path), std::move(text), steps,
            std::move(dependencies), std::move(value)});
        std::lock_guard<std::mutex> lock(mutex_);
        entries_[steps ? 0 : 1] = std::move(entry);
    }
  private:
    struct Entry
    {
        std::string path, text;
        bool steps;
        Dependencies dependencies;
        std::shared_ptr<const Value> value;
    };
    mutable std::mutex mutex_;
    std::array<std::shared_ptr<const Entry>, 2> entries_{};
};
}
