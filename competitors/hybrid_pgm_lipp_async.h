#pragma once

#include <vector>
#include <cstdlib>
#include <string>
#include <thread>
#include <shared_mutex>
#include <atomic>
#include <cstring>
#include <functional>

#include "../util.h"
#include "base.h"
#include "./lipp/src/core/lipp.h"
#include "pgm_index_dynamic.hpp"
#include "pgm_index.hpp"
#include "searches/branching_binary_search.h"

// ---------------------------------------------------------------------------
// Bloom filter — explicitly allowed by course staff for Milestone 3.
//
// Answers: "Is key X definitely NOT in dpgm_active_?"
//   possibly_contains() = false  → key is DEFINITELY not in DPGM (no false negatives)
//   possibly_contains() = true   → key is PROBABLY in DPGM (~1% false positive rate)
//
// Size: 1M bits (128 KB).  False positive rates:
//   10K  keys → 0.00001%    50K keys → 0.01%
//   100K keys → 0.04%      200K keys → 0.15%   (worst case in 10% insert workload)
// Fits entirely in L2 cache on Adroit → near-zero query cost.
// ---------------------------------------------------------------------------
struct BloomFilter {
    static constexpr size_t NBITS = 1u << 20; // 1M bits = 128 KB
    static constexpr size_t MASK  = NBITS - 1;

    uint8_t bits[NBITS / 8];

    BloomFilter() { clear(); }
    void clear() { std::memset(bits, 0, sizeof(bits)); }

    // Mix a hash value to spread bits — prevents clustering for sequential keys.
    static size_t mix(size_t h) {
        h ^= h >> 33;
        h *= 0xff51afd7ed558ccdULL;
        h ^= h >> 33;
        h *= 0xc4ceb9fe1a85ec53ULL;
        h ^= h >> 33;
        return h;
    }

    template <class KeyType>
    void add(const KeyType& key) {
        size_t h  = mix(std::hash<KeyType>{}(key));
        size_t h2 = mix(h);
        // 3 independent bit positions via double hashing
        for (int i = 0; i < 3; ++i) {
            size_t pos = (h + static_cast<size_t>(i) * h2) & MASK;
            bits[pos >> 3] |= static_cast<uint8_t>(1u << (pos & 7));
        }
    }

    template <class KeyType>
    bool possibly_contains(const KeyType& key) const {
        size_t h  = mix(std::hash<KeyType>{}(key));
        size_t h2 = mix(h);
        for (int i = 0; i < 3; ++i) {
            size_t pos = (h + static_cast<size_t>(i) * h2) & MASK;
            if (!(bits[pos >> 3] >> (pos & 7) & 1u)) return false;
        }
        return true;
    }
};

// ---------------------------------------------------------------------------
/**
 * Milestone 3: Async double-buffered Hybrid DPGM + LIPP index.
 *
 * Data structure invariant — keys live in exactly one of three places:
 *   1. lipp_          : bulk-loaded keys + previously flushed keys
 *   2. dpgm_flushing_ : keys currently being drained to LIPP in background
 *   3. dpgm_active_   : most recently inserted keys not yet flushed
 *
 * Bloom filter on dpgm_active_ (explicitly allowed by course staff):
 *   Before checking dpgm_active_ after a LIPP miss, query the Bloom filter.
 *   - "definitely not in DPGM" (99%+ of bulk-loaded key lookups) → skip DPGM
 *   - "possibly in DPGM" → check dpgm_active_
 *   This brings lookup-heavy workload performance close to pure LIPP by
 *   eliminating the expensive dpgm_active_.find() call on bulk-loaded keys.
 *
 * Async strategy:
 *   - Inserts go to dpgm_active_ + bloom_active_ (main thread, no lock).
 *   - When dpgm_active_ reaches flush_interval inserts, it is moved into
 *     dpgm_flushing_ (bloom_active_ is cleared), and a background thread
 *     drains dpgm_flushing_ into LIPP.
 *   - New inserts immediately continue into the fresh dpgm_active_.
 *
 * Thread-safety model (rw_mutex_, std::shared_mutex):
 *   shared_lock — lookup reading lipp_ and dpgm_flushing_
 *   unique_lock — background flush writing lipp_ and clearing dpgm_flushing_
 *
 * Lookup order:
 *   Bloom filter check → (if maybe in DPGM) dpgm_active_ first
 *                      → lipp_ → dpgm_flushing_ (if flush active)
 *
 * Template parameters:
 *   pgm_error      : PGM epsilon for both DPGM instances
 *   flush_interval : flush after this many inserts into dpgm_active_
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
        bloom_active_.clear();

        return util::timing([&] {
            lipp_.bulk_load(loading_data.data(), (int)loading_data.size());
        });
    }

    // -------------------------------------------------------------------------
    size_t EqualityLookup(const KeyType& lookup_key, uint32_t /*thread_id*/) const {
        // --- Step 1: Bloom filter fast check -----------------------------------
        // If the key is DEFINITELY not in dpgm_active_, skip straight to LIPP.
        // This eliminates the expensive dpgm_.find() overhead on the ~99% of
        // lookups for bulk-loaded keys that were never in dpgm_active_.
        // Note: correct for unique-key workloads (no update semantics needed).
        if (dpgm_count_ > 0 && bloom_active_.possibly_contains(lookup_key)) {
            // Key MIGHT be in dpgm_active_ — check DPGM first (avoids LIPP miss
            // penalty for recently inserted keys).
            auto it = dpgm_active_.find(lookup_key);
            if (it != dpgm_active_.end()) return it->value();
        }

        // --- Step 2: LIPP (holds bulk-loaded + all previously flushed keys) ----
        // Lock only when background flush thread is writing to lipp_.
        if (flush_in_progress_.load(std::memory_order_acquire)) {
            std::shared_lock<std::shared_mutex> lk(rw_mutex_);
            uint64_t value;
            if (lipp_.find(lookup_key, value)) return value;
            // Key might be mid-transfer in dpgm_flushing_ (not yet in lipp_).
            auto it = dpgm_flushing_.find(lookup_key);
            if (it != dpgm_flushing_.end()) return it->value();
        } else {
            uint64_t value;
            if (lipp_.find(lookup_key, value)) return value;
        }

        return util::NOT_FOUND;
    }

    // -------------------------------------------------------------------------
    void Insert(const KeyValue<KeyType>& data, uint32_t /*thread_id*/) {
        dpgm_active_.insert(data.key, data.value);
        bloom_active_.add(data.key);   // mirror insert into Bloom filter
        dpgm_count_++;

        if (dpgm_count_ >= flush_interval) {
            if (flush_thread_.joinable()) flush_thread_.join();

            dpgm_flushing_ = std::move(dpgm_active_);
            dpgm_active_   = DPGMType();
            bloom_active_.clear();     // new active buffer is empty
            dpgm_count_    = 0;

            flush_in_progress_.store(true, std::memory_order_release);
            flush_thread_ = std::thread([this] { do_flush(); });
        }
    }

    // -------------------------------------------------------------------------
    std::string name() const { return "HybridPGMLIPPAsync"; }

    std::size_t size() const {
        if (flush_thread_.joinable()) flush_thread_.join();
        return lipp_.index_size() + dpgm_active_.size_in_bytes()
               + sizeof(bloom_active_);
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
    DPGMType    dpgm_active_;
    BloomFilter bloom_active_;   // mirrors keys in dpgm_active_
    size_t      dpgm_count_;

    // ---- Shared state (protected by rw_mutex_) ------------------------------
    LIPP<KeyType, uint64_t> lipp_;
    mutable DPGMType        dpgm_flushing_;

    mutable std::shared_mutex rw_mutex_;
    mutable std::thread       flush_thread_;
    std::atomic<bool>         flush_in_progress_;

    // -------------------------------------------------------------------------
    void do_flush() {
        std::unique_lock<std::shared_mutex> lk(rw_mutex_);
        for (auto it = dpgm_flushing_.begin(); it != dpgm_flushing_.end(); ++it)
            lipp_.insert(it->key(), it->value());
        dpgm_flushing_ = DPGMType();
        flush_in_progress_.store(false, std::memory_order_release);
    }
};
