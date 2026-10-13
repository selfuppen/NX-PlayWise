#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace tsl::gfx::detail {

// Fixed metadata avoids allocating hash nodes on the overlay's small heap.
// Value owns its bitmap; eviction, clear and destruction release that bitmap.
template<typename Value, std::size_t Capacity, std::size_t ByteLimit, std::size_t Ways = 4>
class GlyphCache {
    static_assert(Capacity > 0 && Ways > 0 && Capacity % Ways == 0);

    struct Entry {
        Value value{};
        std::uint64_t key = 0;
        std::uint64_t used = 0;
        std::size_t bytes = 0;
        bool occupied = false;
    };

public:
    GlyphCache() = default;
    GlyphCache(const GlyphCache &) = delete;
    GlyphCache &operator=(const GlyphCache &) = delete;

    Value *find(std::uint64_t key) {
        const auto first = bucket(key);
        for (std::size_t i = first; i < first + Ways; ++i) {
            auto &entry = entries_[i];
            if (entry.occupied && entry.key == key) {
                entry.used = ++clock_;
                return &entry.value;
            }
        }
        return nullptr;
    }

    // Reserve before rasterization, so even the next bitmap fits the budget.
    Value *insert(std::uint64_t key, std::size_t bytes) {
        if (bytes > ByteLimit) return nullptr;
        const auto first = bucket(key);
        auto *slot = &entries_[first];
        for (std::size_t i = first; i < first + Ways; ++i) {
            auto &entry = entries_[i];
            if (!entry.occupied || entry.key == key) {
                slot = &entry;
                break;
            }
            if (entry.used < slot->used) slot = &entry;
        }
        release(*slot);
        while (bytes_ > ByteLimit - bytes) {
            Entry *oldest = nullptr;
            for (auto &entry : entries_) {
                if (entry.occupied && (!oldest || entry.used < oldest->used)) oldest = &entry;
            }
            release(*oldest);
        }
        slot->key = key;
        slot->used = ++clock_;
        slot->bytes = bytes;
        slot->occupied = true;
        bytes_ += bytes;
        ++size_;
        return &slot->value;
    }

    void erase(std::uint64_t key) {
        const auto first = bucket(key);
        for (std::size_t i = first; i < first + Ways; ++i) {
            auto &entry = entries_[i];
            if (entry.occupied && entry.key == key) {
                release(entry);
                return;
            }
        }
    }

    void clear() {
        for (auto &entry : entries_) release(entry);
        clock_ = 0;
    }

    std::size_t size() const { return size_; }
    std::size_t bytes() const { return bytes_; }

private:
    static std::size_t bucket(std::uint64_t key) {
        key ^= key >> 33;
        key *= UINT64_C(0xff51afd7ed558ccd);
        key ^= key >> 33;
        return static_cast<std::size_t>(key % (Capacity / Ways)) * Ways;
    }

    void release(Entry &entry) {
        if (!entry.occupied) return;
        bytes_ -= entry.bytes;
        --size_;
        entry.value = Value{};
        entry.bytes = 0;
        entry.occupied = false;
    }

    std::array<Entry, Capacity> entries_{};
    std::uint64_t clock_ = 0;
    std::size_t size_ = 0;
    std::size_t bytes_ = 0;
};

} // namespace tsl::gfx::detail
