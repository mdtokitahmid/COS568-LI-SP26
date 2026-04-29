#pragma once

#include <vector>
#include <cstdlib>
#include <string>

#include "../util.h"
#include "base.h"
#include "./lipp/src/core/lipp.h"
#include "pgm_index_dynamic.hpp"
#include "pgm_index.hpp"
#include "searches/branching_binary_search.h"

/**
 * Hybrid DPGM + LIPP index.
 *
 * Strategy:
 *   - Initial keys are bulk-loaded into LIPP.
 *   - New inserts go into a DPGM write buffer.
 *   - When the DPGM buffer reaches flush_threshold_pct% of the initial
 *     bulk-loaded size, all keys in DPGM are flushed into LIPP one by one,
 *     and the DPGM is reset to empty.
 *   - Lookups check DPGM first (smaller, cheaper miss), then LIPP.
 *
 * Template parameters:
 *   pgm_error          : epsilon for the internal DPGM (controls insert cost)
 *   flush_threshold_pct: flush when dpgm holds >= this % of initial_size keys
 */
template <class KeyType, size_t pgm_error = 64, size_t flush_threshold_pct = 5>
class HybridPGMLIPP : public Base<KeyType> {

    // Fix the search class for the internal DPGM.
    // BranchingBinarySearch<0> is the standard non-recording variant.
    using SearchClass = BranchingBinarySearch<0>;
    using DPGMType    = DynamicPGMIndex<KeyType, uint64_t, SearchClass,
                                        PGMIndex<KeyType, SearchClass, pgm_error, 16>>;

public:
    HybridPGMLIPP(const std::vector<int>& /*params*/)
        : dpgm_count_(0), initial_size_(0) {}

    uint64_t Build(const std::vector<KeyValue<KeyType>>& data, size_t /*num_threads*/) {
        std::vector<std::pair<KeyType, uint64_t>> loading_data;
        loading_data.reserve(data.size());
        for (const auto& itm : data) {
            loading_data.emplace_back(itm.key, itm.value);
        }

        initial_size_ = data.size();
        dpgm_count_   = 0;

        // Bulk load all initial keys into LIPP. DPGM starts empty.
        return util::timing([&] {
            lipp_.bulk_load(loading_data.data(), (int)loading_data.size());
        });
    }

    size_t EqualityLookup(const KeyType& lookup_key, uint32_t /*thread_id*/) const {
        // Check the (small) DPGM write buffer first.
        auto it = dpgm_.find(lookup_key);
        if (it != dpgm_.end()) {
            return it->value();
        }

        // Fall through to LIPP.
        uint64_t value;
        if (lipp_.find(lookup_key, value)) {
            return value;
        }

        return util::NOT_FOUND;
    }

    void Insert(const KeyValue<KeyType>& data, uint32_t /*thread_id*/) {
        dpgm_.insert(data.key, data.value);
        dpgm_count_++;

        // Flush when DPGM holds >= flush_threshold_pct% of the initial size.
        // Using integer arithmetic to avoid floating point:
        //   dpgm_count_ / initial_size_ >= flush_threshold_pct / 100
        //   => dpgm_count_ * 100 >= flush_threshold_pct * initial_size_
        if (dpgm_count_ * 100 >= flush_threshold_pct * initial_size_) {
            flush();
        }
    }

    std::string name() const { return "HybridPGMLIPP"; }

    std::size_t size() const {
        return lipp_.index_size() + dpgm_.size_in_bytes();
    }

    bool applicable(bool unique, bool /*range_query*/, bool /*insert*/,
                    bool multithread, const std::string& /*ops_filename*/) const {
        // LIPP only supports unique keys and single-threaded access.
        return unique && !multithread;
    }

    std::vector<std::string> variants() const {
        return {std::to_string(pgm_error), std::to_string(flush_threshold_pct)};
    }

private:
    LIPP<KeyType, uint64_t> lipp_;
    DPGMType                dpgm_;
    size_t                  dpgm_count_;
    size_t                  initial_size_;

    void flush() {
        // Iterate DPGM in sorted key order and insert every key into LIPP.
        for (auto it = dpgm_.begin(); it != dpgm_.end(); ++it) {
            lipp_.insert(it->key(), it->value());
        }
        // Reset DPGM to a fresh empty instance.
        dpgm_       = DPGMType();
        dpgm_count_ = 0;
    }
};
