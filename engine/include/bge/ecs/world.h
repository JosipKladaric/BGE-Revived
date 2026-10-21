// BGE Revived - sparse-set entity component system.
#pragma once

#include <algorithm>
#include <cstdint>
#include <functional>
#include <memory>
#include <typeindex>
#include <typeinfo>
#include <unordered_map>
#include <utility>
#include <vector>

#include "bge/core/log.h"

namespace bge {

struct Entity {
    static constexpr uint32_t invalid_index = 0xFFFFFFFFu;
    uint32_t index = invalid_index;
    uint32_t generation = 0;

    constexpr bool valid() const { return index != invalid_index; }
    constexpr explicit operator bool() const { return valid(); }
    friend constexpr bool operator==(Entity a, Entity b) { return a.index == b.index && a.generation == b.generation; }
    friend constexpr bool operator!=(Entity a, Entity b) { return !(a == b); }
};

struct EntityHash {
    size_t operator()(Entity e) const { return (static_cast<size_t>(e.generation) << 32) ^ e.index; }
};

class IComponentPool {
public:
    virtual ~IComponentPool() = default;
    virtual void remove(uint32_t entity_index) = 0;
    virtual bool has(uint32_t entity_index) const = 0;
    virtual size_t size() const = 0;
    virtual const std::vector<uint32_t>& entities() const = 0;
    virtual void clear() = 0;
};

template <typename T>
class ComponentPool final : public IComponentPool {
public:
    T& add(uint32_t entity_index, T value) {
        BGE_ASSERT(!has(entity_index), "component already exists");
        if (sparse_.size() <= entity_index) sparse_.resize(entity_index + 1, Entity::invalid_index);
        sparse_[entity_index] = static_cast<uint32_t>(dense_.size());
        indices_.push_back(entity_index);
        dense_.push_back(std::move(value));
        return dense_.back();
    }

    template <typename... Args>
    T& emplace(uint32_t entity_index, Args&&... args) {
        return add(entity_index, T{std::forward<Args>(args)...});
    }

    T* get(uint32_t entity_index) {
        if (entity_index >= sparse_.size()) return nullptr;
        uint32_t i = sparse_[entity_index];
        return i == Entity::invalid_index ? nullptr : &dense_[i];
    }
    const T* get(uint32_t entity_index) const {
        if (entity_index >= sparse_.size()) return nullptr;
        uint32_t i = sparse_[entity_index];
        return i == Entity::invalid_index ? nullptr : &dense_[i];
    }

    void remove(uint32_t entity_index) override {
        if (!has(entity_index)) return;
        uint32_t i = sparse_[entity_index];
        uint32_t last = static_cast<uint32_t>(dense_.size() - 1);
        if (i != last) {
            dense_[i] = std::move(dense_[last]);
            indices_[i] = indices_[last];
            sparse_[indices_[i]] = i;
        }
        dense_.pop_back();
        indices_.pop_back();
        sparse_[entity_index] = Entity::invalid_index;
    }

    bool has(uint32_t entity_index) const override {
        return entity_index < sparse_.size() && sparse_[entity_index] != Entity::invalid_index;
    }
    size_t size() const override { return dense_.size(); }
    const std::vector<uint32_t>& entities() const override { return indices_; }
    void clear() override { dense_.clear(); indices_.clear(); sparse_.clear(); }

private:
    std::vector<T> dense_;
    std::vector<uint32_t> indices_;
    std::vector<uint32_t> sparse_;
};

class World {
public:
    World() = default;
    World(const World&) = delete;
    World& operator=(const World&) = delete;

    Entity create() {
        uint32_t index;
        if (!free_.empty()) {
            index = free_.back();
            free_.pop_back();
        } else {
            index = static_cast<uint32_t>(records_.size());
            records_.push_back({});
        }
        records_[index].alive = true;
        ++records_[index].generation;
        ++alive_;
        return Entity{index, records_[index].generation};
    }

    void destroy(Entity e) {
        if (!valid(e)) return;
        for (auto& [type, pool] : pools_) pool->remove(e.index);
        records_[e.index].alive = false;
        ++records_[e.index].generation;
        free_.push_back(e.index);
        --alive_;
    }

    bool valid(Entity e) const {
        return e.index < records_.size() && records_[e.index].alive && records_[e.index].generation == e.generation;
    }

    // Returns the entity stored at a raw index, or an invalid handle.
    Entity entity_at(uint32_t index) const {
        if (index >= records_.size() || !records_[index].alive) return Entity{};
        return Entity{index, records_[index].generation};
    }

    size_t alive_count() const { return alive_; }
    size_t pool_count() const { return pools_.size(); }

    template <typename T>
    T& add(Entity e, T value) {
        BGE_ASSERT(valid(e), "add component to invalid entity");
        return pool<T>().add(e.index, std::move(value));
    }

    template <typename T, typename... Args>
    T& emplace(Entity e, Args&&... args) {
        BGE_ASSERT(valid(e), "emplace component on invalid entity");
        return pool<T>().emplace(e.index, std::forward<Args>(args)...);
    }

    template <typename T>
    T* get(Entity e) {
        return valid(e) ? pool<T>().get(e.index) : nullptr;
    }
    template <typename T>
    const T* get(Entity e) const {
        return valid(e) ? pool<T>().get(e.index) : nullptr;
    }

    template <typename T>
    bool has(Entity e) const {
        if (!valid(e)) return false;
        auto it = pools_.find(std::type_index(typeid(T)));
        if (it == pools_.end()) return false;
        return it->second->has(e.index);
    }

    template <typename T>
    void remove(Entity e) {
        auto it = pools_.find(std::type_index(typeid(T)));
        if (it != pools_.end()) it->second->remove(e.index);
    }

    template <typename... Ts, typename Fn>
    void each(Fn&& fn) {
        IComponentPool* pools[] = { find_pool<Ts>()... };
        IComponentPool* smallest = nullptr;
        for (IComponentPool* pool : pools) {
            if (!pool) return;
            if (!smallest || pool->size() < smallest->size()) smallest = pool;
        }
        if (!smallest) return;
        // Copy the entity list: callbacks may add/remove components (not entities).
        std::vector<uint32_t> indices = smallest->entities();
        for (uint32_t index : indices) {
            if (index >= records_.size() || !records_[index].alive) continue;
            Entity e{index, records_[index].generation};
            if (((get<Ts>(e) != nullptr) && ...)) {
                fn(e, *get<Ts>(e)...);
            }
        }
    }

    void clear() {
        for (auto& [type, pool] : pools_) pool->clear();
        pools_.clear();
        records_.clear();
        free_.clear();
        alive_ = 0;
    }

    // Debug/testing helper.
    std::vector<Entity> entities() const {
        std::vector<Entity> out;
        out.reserve(alive_);
        for (uint32_t i = 0; i < records_.size(); ++i) {
            if (records_[i].alive) out.push_back(Entity{i, records_[i].generation});
        }
        return out;
    }

private:
    struct Record {
        uint32_t generation = 0;
        bool alive = false;
    };

    template <typename T>
    ComponentPool<T>& pool() {
        auto key = std::type_index(typeid(T));
        auto it = pools_.find(key);
        if (it == pools_.end()) {
            auto owned = std::make_unique<ComponentPool<T>>();
            auto* raw = owned.get();
            pools_.emplace(key, std::move(owned));
            return *raw;
        }
        return static_cast<ComponentPool<T>&>(*it->second);
    }

    template <typename T>
    ComponentPool<T>* find_pool() {
        auto it = pools_.find(std::type_index(typeid(T)));
        return it == pools_.end() ? nullptr : static_cast<ComponentPool<T>*>(it->second.get());
    }

    template <typename T>
    const ComponentPool<T>* find_pool() const {
        auto it = pools_.find(std::type_index(typeid(T)));
        return it == pools_.end() ? nullptr : static_cast<const ComponentPool<T>*>(it->second.get());
    }

    std::unordered_map<std::type_index, std::unique_ptr<IComponentPool>> pools_;
    std::vector<Record> records_;
    std::vector<uint32_t> free_;
    size_t alive_ = 0;
};

} // namespace bge
