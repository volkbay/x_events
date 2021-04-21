//
// Created by Florian Mahlknecht on 2021-04-20.
// Copyright (c) 2021 NASA / JPL. All rights reserved.



#pragma once

#include <cstddef>
#include <set>

namespace x {

  class DebugMemoryMonitor {
    friend class DebugMemory;

    class DebugMemoryInterface {
    public:
      virtual size_t memory_usage_in_bytes() const = 0;
    };

  public:
    DebugMemoryMonitor(const DebugMemoryMonitor&) = delete;
    DebugMemoryMonitor& operator=(const DebugMemoryMonitor &) = delete;
    DebugMemoryMonitor(DebugMemoryMonitor &&) = delete;
    DebugMemoryMonitor& operator=(DebugMemoryMonitor &&) = delete;

    size_t memory_usage_in_bytes() const {
      size_t total_memory = 0;
      for (const auto& d : debug_set_)
        total_memory += d->memory_usage_in_bytes();
      return total_memory;
    }

    void register_debug_memory(DebugMemoryInterface* debug_memory) {
      debug_set_.insert(debug_memory);
    }

    void unregister_debug_memory(DebugMemoryInterface* debug_memory) {
      auto it = debug_set_.find(debug_memory);
      if (it != debug_set_.end()) {
        debug_set_.erase(it);
      }
    }

    static auto& instance(){
      static DebugMemoryMonitor memory_monitor;
      return memory_monitor;
    }

  private:
    DebugMemoryMonitor() = default;
    std::set<DebugMemoryInterface*> debug_set_;
  };

  class DebugMemory : DebugMemoryMonitor::DebugMemoryInterface {
  public:
    DebugMemory() {
      DebugMemoryMonitor::instance().register_debug_memory(this);
    };

    virtual ~DebugMemory() {
      DebugMemoryMonitor::instance().unregister_debug_memory(this);
    }

  };

}




