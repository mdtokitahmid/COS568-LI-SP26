#pragma once

#include <vector>
#include <cstdlib>
#include <string>
#include <thread>
#include <shared_mutex>
#include <atomic>

#include "../util.h"
#include "base.h"
#include "./lipp/src/core/lipp.h"
#include "pgm_index_dynamic.hpp"
#include "pgm_index.hpp"
#include "searches/branching_binary_search.h"

/**
 * Milestone 3: Async double-buffered Hybrid DPGM + LIPP index.
 *
 * Data structure invariant — keys live in exactly one of three places:
 *   1. lipp_          : bulk-loaded keys + previously flushed keys
 *   2. dpgm_flushing_ : keys currently being drained to LIPP in background
 *   3. dpgm_active_   : most recently inserted keys not yet flushed
 *
 * No auxiliary data structures (no vectors, no maps).
 * This satisfies the README requirement: "No auxiliary data structures are
 * allowed other than LIPP and DPGM."
 *
 * Async strategy:
 *   - Inserts go to dpgm_active_ (main thread, no lock).
 *   - When dpgm_active_ reaches flush_interval inserts, it is moved into
 *     dpgm_flushing_ and a background thread drains it into LIPP.
 *   - New inserts immediately continue into a fresh dpgm_active_ — the
 *     flush does not block insertions (this is the async win).
 *
 * Thread-safety model (rw_mutex_, std::shared_mutex):
 *   shared_lock — lookup reading lipp_ and dpgm_flushing_
 *   unique_lock — background flush writing lipp_ and clearing dpgm_flushing_
 *
 * Lookup order (LIPP first):
 *   lipp_ → dpgm_flushing_ (if flush active) → dpgm_active_
 *   Most keys are bulk-loaded in lipp_, so the majority of lookups return
 *   from step 1 with no DPGM overhead.
 *
 * Overflow handling:
 *   If dpgm_active_ fills again before the previous flush finishes, Insert()
 *   joins the thread (brief stall), then starts the next flush cycle.
 *
 * Template parameters:
 *   pgm_error      : PGM epsilon for both DPGM instances
 *   flush_interval : flush after this many inserts into dpgm_active_
 *                    (absolute count — fires within the 2M-op benchmark window)
 */
template <class KeyType, size_t pgm_error = 64, size_t flush_interval = 50000>
class HybridPGMLIPPAsync : public Base<KeyType> {

    using SearchClass = BranchingBinarySearch<0>;
    using DPGMType    = DynamicPGMIndex<KeyType, uint64_t, SearchClass,
                                        PGMIndex<KeyType, SearchClass, pgm_error, 16>>;

public:
    HybridPGMLIPPAsync(const std::vector<int>& /*params*/)
        : dpgm_count_(0), flush_in_progress_(false) {}

    ~HybridPGMLIPPAsync() {
        if (flush_thread_.joinable()) flush_thread_.join();
    }

    // -------------------------------------------------------------------------
    uint64_t Build(const std::vector<KeyValue<KeyType>>& data, size_t /*num_threads*/) {
        std::vector<std::pair<KeyType, uint64_t>> loading_data;
        loading_data.reserve(data.size());
        for (const auto& itm : data)
            loading_data.emplace_back(itm.key, itm.value);

        dpgm_count_ = 0;

        return util::timing([&] {
            lipp_.bulk_load(loading_data.data(), (int)loading_data.size());
        });
    }

    // -------------------------------------------------------------------------
    size_t EqualityLookup(const KeyType& lookup_key, uint32_t /*thread_id*/) const {
        // --- Step 1: LIPP (holds bulk-loaded + all previously flushed keys) ----
        // LIPP-first is correct for this benchmark because all keys are unique
        // and inserts are new keys (not updates).  If duplicate-key / update
        // semantics were required, dpgm_active_ would have to be checked first
        // so a newer value shadows the older LIPP entry.
        // Lock only when background thread may be writing LIPP.
        if (flush_in_progress_.load(std::memory_order_acquire)) {
            std::shared_lock<std::shared_mutex> lk(rw_mutex_);
            uint64_t value;
            if (lipp_.find(lookup_key, value)) return value;

            // --- Step 2 (under same lock): dpgm_flushing_ ----------------------
            // Keys mid-transfer: already moved out of dpgm_active_ but not yet
            // in lipp_.  The background thread holds unique_lock while writing
            // lipp_ and clearing dpgm_flushing_, so when we hold shared_lock
            // the flushing DPGM is either still full (safe to read) or already
            // cleared (find() returns end() quickly).
            auto it = dpgm_flushing_.find(lookup_key);
            if (it != dpgm_flushing_.end()) return it->value();
        } else {
            // No concurrent writer — read lipp_ directly, no lock needed.
            uint64_t value;
            if (lipp_.find(lookup_key, value)) return value;
        }

        // --- Step 3: dpgm_active_ (main-thread-only, no lock) ------------------
        // Only recently inserted keys live here.  Reached only on a lipp_ miss.
        if (dpgm_count_ > 0) {
            auto it = dpgm_active_.find(lookup_key);
            if (it != dpgm_active_.end()) return it->value();
        }

        return util::NOT_FOUND;
    }

    // -------------------------------------------------------------------------
    void Insert(const KeyValue<KeyType>& data, uint32_t /*thread_id*/) {
        dpgm_active_.insert(data.key, data.value);
        dpgm_count_++;

        if (dpgm_count_ >= flush_interval) {
            // Overflow guard: wait if previous flush is still running.
            if (flush_thread_.joinable()) flush_thread_.join();

            // Move active buffer → flushing buffer.
            // After join(), background thread is done and dpgm_flushing_ is
            // empty, so this move is safe with no lock.
            dpgm_flushing_ = std::move(dpgm_active_);
            dpgm_active_   = DPGMType();
            dpgm_count_    = 0;

            // Signal before launching so EqualityLookup sees flush_in_progress_
            // as true from the moment the flushing DPGM is populated.
            flush_in_progress_.store(true, std::memory_order_release);
            flush_thread_ = std::thread([this] { do_flush(); });
        }
    }

    // -------------------------------------------------------------------------
    std::string name() const { return "HybridPGMLIPPAsync"; }

    std::size_t size() const {
        if (flush_thread_.joinable()) flush_thread_.join();
        return lipp_.index_size() + dpgm_active_.size_in_bytes();
    }

    bool applicable(bool unique, bool /*range_query*/, bool /*insert*/,
                    bool multithread, const std::string& /*ops_filename*/) const {
        return unique && !multithread;
    }

    std::vector<std::string> variants() const {
        return {std::to_string(pgm_error), std::to_string(flush_interval)};
    }

private:
    // ---- Main-thread-only (no lock) -----------------------------------------
    DPGMType dpgm_active_;
    size_t   dpgm_count_;

    // ---- Shared state (protected by rw_mutex_) ------------------------------
    LIPP<KeyType, uint64_t> lipp_;
    mutable DPGMType        dpgm_flushing_;  // mutable: const lookup reads it

    mutable std::shared_mutex rw_mutex_;
    mutable std::thread       flush_thread_;
    std::atomic<bool>         flush_in_progress_;

    // -------------------------------------------------------------------------
    /**
     * Background flush: drain dpgm_flushing_ into lipp_, then clear it.
     *
     * Holds the exclusive lock for the entire operation so that:
     *   (a) lipp_ writes are not concurrent with any shared_lock reads
     *   (b) dpgm_flushing_ is only cleared under exclusive lock, so any
     *       shared_lock lookup that reads dpgm_flushing_ sees either the
     *       full DPGM (flush not yet started) or an empty one (already done)
     *
     * Race-condition proof:
     *   - If main thread loads flush_in_progress_=true and tries shared_lock
     *     BEFORE this unique_lock: it blocks until we release → sees LIPP
     *     with all keys inserted and dpgm_flushing_ empty → correct.
     *   - If main thread loads flush_in_progress_=true and gets shared_lock
     *     BEFORE this unique_lock fires (background not yet scheduled):
     *     dpgm_flushing_ is full → dpgm_flushing_.find() works correctly.
     *     When main releases shared_lock, background gets unique_lock → proceeds.
     *
     * New inserts go to dpgm_active_ (no lock) throughout this entire function
     * → insertions are never blocked by the flush.
     */
    void do_flush() {
        // One batch exclusive lock for the entire flush.
        // Trade-off: lookups block during the flush window, but new *inserts*
        // are never blocked (they go to dpgm_active_ with no lock at all).
        // The async benefit is therefore insert-side: flush_interval new inserts
        // proceed concurrently while this thread drains dpgm_flushing_ to lipp_.
        // Per-key locking was tested and found slower due to 10K–50K lock/unlock
        // cycles per flush overwhelming the lookup shared_lock acquisition.
        std::unique_lock<std::shared_mutex> lk(rw_mutex_);
        for (auto it = dpgm_flushing_.begin(); it != dpgm_flushing_.end(); ++it)
            lipp_.insert(it->key(), it->value());
        dpgm_flushing_      = DPGMType();  // reset to empty DPGM (no vector)
        flush_in_progress_.store(false, std::memory_order_release);
    }
};
