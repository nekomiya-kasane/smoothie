#pragma once

/// @file pak_diff.h
/// @brief Incremental .lpak update: diff generation and patch application.
///
/// Compares two .lpak files (old and new) and produces a compact binary patch
/// that contains only added/modified/removed entries. The patch can be applied
/// to the old .lpak to produce the new one without retransmitting unchanged data.
///
/// Usage (diff):
///   auto patch = pak_diff::create(old_reader, new_reader);
///   patch.write("update.lpatch");
///
/// Usage (apply):
///   auto result = pak_patch::apply(old_reader, patch_data, "output.lpak");

#include "smoothie/exports.h"
#include "smoothie/resource/lpak_format.h"
#include "smoothie/resource/pak_reader.h"
#include "smoothie/types.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace smoothie::resource {

    // ── Patch entry operations ───────────────────────────────────────────

    enum class patch_op : uint8_t {
        keep = 0,   // entry unchanged, copy from old pak
        add = 1,    // new entry not present in old pak
        modify = 2, // entry exists in both but data changed
        remove = 3, // entry removed in new pak
    };

    /// @brief A single entry in the diff patch.
    struct patch_entry {
        patch_op op = patch_op::keep;
        uint64_t semantic_hash = 0;
        uint16_t type = 0;
        uint16_t flags = 0;
        uint32_t new_data_size = 0; // size of data in patch (0 for keep/remove)
        uint32_t old_data_size = 0; // original size (for verification)
        uint32_t patch_offset = 0;  // offset into patch data payload
    };

    // ── Patch file format ────────────────────────────────────────────────

    inline constexpr std::array<char, 4> lpatch_magic = {'L', 'P', 'C', 'H'};
    inline constexpr uint32_t lpatch_version = 1;

#pragma pack(push, 1)
    struct patch_header {
        char magic[4];              // "LPCH"
        uint32_t version;           // patch format version
        uint32_t entry_count;       // number of patch_entry records
        uint64_t old_data_checksum; // xxHash64 of old pak's data payload (verification)
        uint64_t new_data_checksum; // xxHash64 of expected result
        uint64_t data_payload_size; // total bytes of new/modified entry data
        uint32_t added_count;       // stats: entries added
        uint32_t modified_count;    // stats: entries modified
        uint32_t removed_count;     // stats: entries removed
        uint32_t kept_count;        // stats: entries kept unchanged
    };
#pragma pack(pop)

    // ── Diff generator ───────────────────────────────────────────────────

    /// @brief Generates a diff between two .lpak files.
    class SMOOTHIE_API pak_diff {
      public:
        /// @brief Compare old and new pak readers, producing a patch.
        [[nodiscard]] static auto create(const pak_reader &old_pak, const pak_reader &new_pak)
            -> diagnostic_result<pak_diff>;

        /// @brief Write the patch to a file.
        [[nodiscard]] auto write(const std::filesystem::path &output) const -> diagnostic_result<void>;

        /// @brief Serialize the patch to a byte buffer.
        [[nodiscard]] auto serialize() const -> std::vector<std::byte>;

        /// @brief Get patch statistics.
        [[nodiscard]] auto added_count() const noexcept -> uint32_t { return stats_.added; }
        [[nodiscard]] auto modified_count() const noexcept -> uint32_t { return stats_.modified; }
        [[nodiscard]] auto removed_count() const noexcept -> uint32_t { return stats_.removed; }
        [[nodiscard]] auto kept_count() const noexcept -> uint32_t { return stats_.kept; }

        /// @brief Total patch payload size (bytes of new/modified data only).
        [[nodiscard]] auto payload_size() const noexcept -> size_t { return data_payload_.size(); }

      private:
        struct stats {
            uint32_t added = 0;
            uint32_t modified = 0;
            uint32_t removed = 0;
            uint32_t kept = 0;
        };
        stats stats_{};
        std::vector<patch_entry> entries_;
        std::vector<std::byte> data_payload_; // concatenated data for add/modify ops
        uint64_t old_checksum_ = 0;
        uint64_t new_checksum_ = 0;
    };

    // ── Patch applicator ─────────────────────────────────────────────────

    /// @brief Applies a patch to an old .lpak to produce a new .lpak.
    class SMOOTHIE_API pak_patch {
      public:
        /// @brief Apply a patch to an old pak, writing the result to output.
        ///
        /// @param old_pak   Reader for the original .lpak file.
        /// @param patch_data  Raw bytes of the .lpatch file.
        /// @param output    Path for the resulting .lpak file.
        [[nodiscard]] static auto apply(const pak_reader &old_pak, std::span<const std::byte> patch_data,
                                        const std::filesystem::path &output) -> diagnostic_result<void>;

        /// @brief Parse and validate a patch header without applying.
        [[nodiscard]] static auto validate(std::span<const std::byte> patch_data) -> diagnostic_result<patch_header>;
    };

} // namespace smoothie::resource
