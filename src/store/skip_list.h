#pragma once
#include <string>
#include <vector>
#include <shared_mutex>
#include <mutex>
#include <memory>
#include <random>

constexpr int MAX_LEVEL = 16;
constexpr float P = 0.5f;

struct Node {
    std::string key;
    std::string value;
    std::vector<std::shared_ptr<Node>> forward;
    
    Node(std::string k, std::string v, int level) 
        : key(std::move(k)), value(std::move(v)), forward(level, nullptr) {}
};

class SkipList {
    std::shared_ptr<Node> head;
    int current_level;
    mutable std::shared_mutex rw_lock;

    int random_level() {
        // using thread local rng to avoid locking issues
        static thread_local std::mt19937 generator(std::random_device{}());
        std::uniform_real_distribution<float> distribution(0.0f, 1.0f);
        int lvl = 1;
        while (distribution(generator) < P && lvl < MAX_LEVEL) {
            lvl++;
        }
        return lvl;
    }

public:
    SkipList() : current_level(1) {
        head = std::make_shared<Node>("", "", MAX_LEVEL);
    }

    [[nodiscard]] bool get(const std::string& key, std::string& out_value) const {
        std::shared_lock lock(rw_lock); // reader lock
        auto curr = head;
        for (int i = current_level - 1; i >= 0; i--) {
            while (curr->forward[i] && curr->forward[i]->key < key) {
                curr = curr->forward[i];
            }
        }
        curr = curr->forward[0];
        if (curr && curr->key == key) {
            out_value = curr->value;
            return true;
        }
        return false;
    }

    void set(const std::string& key, const std::string& value) {
        std::unique_lock lock(rw_lock); // writer lock
        std::vector<std::shared_ptr<Node>> update(MAX_LEVEL, nullptr);
        auto curr = head;

        for (int i = current_level - 1; i >= 0; i--) {
            while (curr->forward[i] && curr->forward[i]->key < key) {
                curr = curr->forward[i];
            }
            update[i] = curr;
        }

        curr = curr->forward[0];

        if (curr && curr->key == key) {
            curr->value = value;
        } else {
            int lvl = random_level();
            if (lvl > current_level) {
                for (int i = current_level; i < lvl; i++) {
                    update[i] = head;
                }
                current_level = lvl;
            }
            auto new_node = std::make_shared<Node>(key, value, lvl);
            for (int i = 0; i < lvl; i++) {
                new_node->forward[i] = update[i]->forward[i];
                update[i]->forward[i] = new_node;
            }
        }
    }
};

class ShardedSkipList {
    std::vector<SkipList> shards;
    
    [[nodiscard]] size_t hash_str(const std::string& key) const noexcept {
        size_t h = 5381;
        for (char c : key) h = ((h << 5) + h) + c; 
        return h;
    }

public:
    // sharding over 256 lists so threads dont block each other
    explicit ShardedSkipList(size_t num_shards = 256) : shards(num_shards) {}

    [[nodiscard]] bool get(const std::string& key, std::string& out_value) const {
        // std::cout << "routing get to shard " << hash_str(key) % shards.size() << std::endl;
        return shards[hash_str(key) % shards.size()].get(key, out_value);
    }

    void set(const std::string& key, const std::string& value) {
        shards[hash_str(key) % shards.size()].set(key, value);
    }
};
