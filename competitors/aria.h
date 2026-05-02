#pragma once

#include <vector>
#include <string>
#include <thread>
#include <shared_mutex>
#include <atomic>

#include "../util.h"
#include "base.h"
#include "my_lipp.h"
#include "pgm_index_dynamic.hpp"
#include "pgm_index.hpp"
#include "searches/branching_binary_search.h"

template <class KeyType, size_t pgm_error = 64, size_t flush_interval = 2000000>
class ARIA : public Base<KeyType> {

    using SearchClass = BranchingBinarySearch<0>;
    using DPGMType    = DynamicPGMIndex<KeyType, uint64_t, SearchClass,
                                        PGMIndex<KeyType, SearchClass, pgm_error, 16>>;

    enum class Mode : uint8_t { WRITE_BUFFERED, READ_OPTIMIZED };
    static constexpr size_t WINDOW = 1024;

public:
    ARIA(const std::vector<int>&)
        : mode_(Mode::WRITE_BUFFERED), profiling_active_(true),
          flush_in_progress_(false), dpgm_count_(0), 
          wnd_inserts_(0), wnd_total_(0) {}

    ~ARIA() {
        if (flush_thread_.joinable()) flush_thread_.join();
    }

    uint64_t Build(const std::vector<KeyValue<KeyType>>& data, size_t) {
        std::vector<std::pair<KeyType, uint64_t>> loading_data;
        loading_data.reserve(data.size());
        for (const auto& itm : data)
            loading_data.emplace_back(itm.key, itm.value);

        mode_ = Mode::WRITE_BUFFERED;
        profiling_active_ = true;
        dpgm_count_ = wnd_inserts_ = wnd_total_ = 0;

        return util::timing([&] {
            lipp_.bulk_load(loading_data.data(), (int)loading_data.size());
        });
    }

    // ========================================================================
    // THE HOT PATH (Forced to Inline)
    // ========================================================================
    __attribute__((always_inline))
    inline size_t EqualityLookup(const KeyType& key, uint32_t thread_id) const {
        // __builtin_expect(..., 0) tells the CPU: "This is almost always false. Skip it."
        if (__builtin_expect(profiling_active_, 0)) {
            ++wnd_total_;
        }

        // __builtin_expect(..., 1) tells the CPU: "This is almost always true. Go inside."
        if (__builtin_expect(mode_ == Mode::READ_OPTIMIZED, 1)) {
            uint64_t v;
            return lipp_.find(key, v) ? v : util::NOT_FOUND;
        }

        // If we are in Write-Heavy mode, jump to the heavy function.
        return EqualityLookup_ColdPath(key);
    }

    __attribute__((always_inline))
    inline void Insert(const KeyValue<KeyType>& data, uint32_t thread_id) {
        if (__builtin_expect(profiling_active_, 0)) {
            ++wnd_inserts_;
            ++wnd_total_;
            maybe_adapt();
        }

        if (__builtin_expect(mode_ == Mode::READ_OPTIMIZED, 1)) {
            lipp_.insert(data.key, data.value);
            return;
        }

        Insert_ColdPath(data);
    }

    // -------------------------------------------------------------------------
    std::string name() const { return "ARIA"; }

    std::size_t size() const {
        auto* non_const_this = const_cast<ARIA*>(this);
        if (non_const_this->flush_thread_.joinable()) non_const_this->flush_thread_.join();
        return lipp_.index_size() + dpgm_active_.size_in_bytes();
    }

    bool applicable(bool unique, bool, bool, bool multithread, const std::string&) const {
        return unique && !multithread;
    }

    std::vector<std::string> variants() const {
        return {std::to_string(pgm_error), std::to_string(flush_interval)};
    }

private:
    // ---- Hot scalar state ---------------------------------------------------
    Mode               mode_;
    bool               profiling_active_;
    mutable size_t     wnd_inserts_;
    mutable size_t     wnd_total_;

    // ---- Index structures ---------------------------------------------------
    MyLIPP<KeyType, uint64_t> lipp_;
    
    // ---- Cold State (Write-Heavy Only) --------------------------------------
    DPGMType                  dpgm_active_;
    mutable DPGMType          dpgm_flushing_;
    size_t                    dpgm_count_;
    std::atomic<bool>         flush_in_progress_;
    mutable std::shared_mutex rw_mutex_;
    mutable std::thread       flush_thread_;

    // ========================================================================
    // THE COLD PATHS (Blocked from Inlining to save the Hot Path stack frame)
    // ========================================================================
    __attribute__((noinline))
    size_t EqualityLookup_ColdPath(const KeyType& key) const {
        if (flush_in_progress_.load(std::memory_order_acquire)) {
            // Hold shared lock for the entire lookup: flush thread holds unique_lock
            // on rw_mutex_ while calling lipp_.insert(), so lipp_.find() must be
            // protected too.
            std::shared_lock<std::shared_mutex> lk(rw_mutex_);
            uint64_t v;
            if (lipp_.find(key, v)) return v;
            auto it = dpgm_flushing_.find(key);
            if (it != dpgm_flushing_.end()) return it->value();
            if (dpgm_count_ > 0) {
                auto it2 = dpgm_active_.find(key);
                if (it2 != dpgm_active_.end()) return it2->value();
            }
            return util::NOT_FOUND;
        }

        uint64_t v;
        if (lipp_.find(key, v)) return v;
        if (dpgm_count_ > 0) {
            auto it = dpgm_active_.find(key);
            if (it != dpgm_active_.end()) return it->value();
        }
        return util::NOT_FOUND;
    }

    __attribute__((noinline))
    void Insert_ColdPath(const KeyValue<KeyType>& data) {
        dpgm_active_.insert(data.key, data.value);
        dpgm_count_++;
        if (dpgm_count_ >= flush_interval) {
            trigger_async_flush();
        }
    }

    // -------------------------------------------------------------------------
    void maybe_adapt() {
        if (wnd_total_ < WINDOW) return;

        bool insert_heavy = (wnd_inserts_ * 2) > wnd_total_;
        
        // Lock the state so EqualityLookup never increments the counter again
        profiling_active_ = false;

        if (insert_heavy) {
            // mode_ is already WRITE_BUFFERED
        } else {
            // Drain the DPGM buffer into LIPP before switching to the
            // read-optimized write-through mode.  No keys are skipped.
            if (flush_thread_.joinable()) flush_thread_.join();
            for (auto it = dpgm_active_.begin(); it != dpgm_active_.end(); ++it) {
                lipp_.insert(it->key(), it->value());
            }
            dpgm_active_ = DPGMType();
            dpgm_count_  = 0;
            mode_ = Mode::READ_OPTIMIZED;
        }
    }

    void trigger_async_flush() {
        if (flush_thread_.joinable()) flush_thread_.join();
        dpgm_flushing_ = std::move(dpgm_active_);
        dpgm_active_   = DPGMType();
        dpgm_count_    = 0;
        flush_in_progress_.store(true, std::memory_order_release);

        flush_thread_ = std::thread([this] {
            std::unique_lock<std::shared_mutex> lk(rw_mutex_);
            for (auto it = dpgm_flushing_.begin(); it != dpgm_flushing_.end(); ++it) {
                lipp_.insert(it->key(), it->value());
            }
            dpgm_flushing_ = DPGMType();
            flush_in_progress_.store(false, std::memory_order_release);
        });
    }
};
