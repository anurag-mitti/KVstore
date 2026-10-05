#pragma once
#include <vector>
#include <cstdint>
#include <cstring>
#include <shared_mutex>

// Increased to 256 bytes for realistic AI RAG document chunks
struct Bucket {
    uint64_t key;
    char value[256];
    bool occupied{false};
};

// Shard protected by a standard Reader-Writer lock (std::shared_mutex)
struct Shard {
    std::shared_mutex rw_lock;
    std::vector<Bucket> buckets;
    
    Shard(size_t size) {
        // Slab Allocation: Pre-allocate all memory on startup. Zero runtime mallocs.
        buckets.resize(size);
    }
};

class TitanMap {
    std::vector<Shard> shards_;
    size_t num_shards_;

    inline size_t hash(uint64_t key) const {
        key ^= key >> 33;
        key *= 0xff51afd7ed558ccd;
        key ^= key >> 33;
        key *= 0xc4ceb9fe1a85ec53;
        key ^= key >> 33;
        return key;
    }

public:
    TitanMap(size_t num_shards, size_t buckets_per_shard) 
        : num_shards_(num_shards) {
        for(size_t i = 0; i < num_shards; ++i) {
            shards_.emplace_back(buckets_per_shard);
        }
    }

    bool get(uint64_t key, char* out_value) {
        size_t h = hash(key);
        size_t shard_idx = h % num_shards_;
        Shard& shard = shards_[shard_idx];
        size_t bucket_idx = h % shard.buckets.size();
        
        std::shared_lock lock(shard.rw_lock); // Reader Lock (Concurrent)
        
        size_t curr = bucket_idx;
        while (shard.buckets[curr].occupied) {
            if (shard.buckets[curr].key == key) {
                std::memcpy(out_value, shard.buckets[curr].value, 256);
                return true;
            }
            curr = (curr + 1) % shard.buckets.size();
            if (curr == bucket_idx) break; 
        }
        return false; 
    }

    bool set(uint64_t key, const char* value) {
        size_t h = hash(key);
        size_t shard_idx = h % num_shards_;
        Shard& shard = shards_[shard_idx];
        size_t bucket_idx = h % shard.buckets.size();

        std::unique_lock lock(shard.rw_lock); // Writer Lock (Exclusive)
        size_t curr = bucket_idx;
        while (shard.buckets[curr].occupied) {
            if (shard.buckets[curr].key == key) {
                std::memcpy(shard.buckets[curr].value, value, 256);
                return true; 
            }
            curr = (curr + 1) % shard.buckets.size();
            if (curr == bucket_idx) return false; // Full
        }
        
        shard.buckets[curr].key = key;
        std::memcpy(shard.buckets[curr].value, value, 256);
        shard.buckets[curr].occupied = true;
        return true;
    }
};
