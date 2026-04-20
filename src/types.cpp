#include "smoothie/smoothie.h"
#include "smoothie/types.h"

namespace smoothie {

// ── Version ─────────────────────────────────────────────────────────────

const char* version_string() noexcept {
    return "0.1.0";
}

int version_int() noexcept {
    return 0 * 10000 + 1 * 100 + 0;
}

// ── resource_view implementation ────────────────────────────────────────

resource_view::resource_view(std::span<const std::byte> borrowed) noexcept
    : storage_(borrowed), cached_data_(borrowed) {}

resource_view::resource_view(std::vector<std::byte> owned) noexcept
    : storage_(std::move(owned)) {
    auto& v = std::get<std::vector<std::byte>>(storage_);
    cached_data_ = {v.data(), v.size()};
}

auto resource_view::data() const noexcept -> std::span<const std::byte> {
    return cached_data_;
}

auto resource_view::size() const noexcept -> size_t {
    return cached_data_.size();
}

auto resource_view::empty() const noexcept -> bool {
    return cached_data_.empty();
}

auto resource_view::is_owned() const noexcept -> bool {
    return std::holds_alternative<std::vector<std::byte>>(storage_);
}

auto resource_view::as_string_view() const noexcept -> std::string_view {
    return {reinterpret_cast<const char*>(cached_data_.data()), cached_data_.size()};
}

auto resource_view::as_string() const -> std::string {
    return std::string(as_string_view());
}

auto resource_view::subspan(size_t offset, size_t count) const -> std::span<const std::byte> {
    if (offset >= cached_data_.size()) {
        return {};
    }
    if (count == std::dynamic_extent || offset + count > cached_data_.size()) {
        count = cached_data_.size() - offset;
    }
    return cached_data_.subspan(offset, count);
}

resource_view::resource_view(const resource_view& other)
    : storage_(other.is_owned()
                   ? decltype(storage_)(std::get<std::vector<std::byte>>(other.storage_))
                   : decltype(storage_)(std::get<std::span<const std::byte>>(other.storage_)))
    , cached_data_([this]() noexcept {
          if (auto* v = std::get_if<std::vector<std::byte>>(&storage_)) {
              return std::span<const std::byte>{v->data(), v->size()};
          }
          return std::get<std::span<const std::byte>>(storage_);
      }()) {}

resource_view& resource_view::operator=(const resource_view& other) {
    if (this != &other) {
        if (other.is_owned()) {
            storage_ = std::get<std::vector<std::byte>>(other.storage_);
        }
        else {
            storage_ = std::get<std::span<const std::byte>>(other.storage_);
        }
        if (auto* v = std::get_if<std::vector<std::byte>>(&storage_)) {
            cached_data_ = {v->data(), v->size()};
        }
        else {
            cached_data_ = std::get<std::span<const std::byte>>(storage_);
        }
    }
    return *this;
}

}  // namespace smoothie
