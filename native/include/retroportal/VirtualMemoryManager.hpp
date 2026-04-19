#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace retroportal::vm {

enum class PagePerm : std::uint8_t {
    None = 0,
    Read = 1 << 0,
    Write = 1 << 1,
    Exec = 1 << 2,
};

inline constexpr PagePerm operator|(PagePerm a, PagePerm b) {
    return static_cast<PagePerm>(
        static_cast<std::uint8_t>(a) | static_cast<std::uint8_t>(b));
}

inline constexpr PagePerm operator&(PagePerm a, PagePerm b) {
    return static_cast<PagePerm>(
        static_cast<std::uint8_t>(a) & static_cast<std::uint8_t>(b));
}

inline constexpr bool Has(PagePerm set, PagePerm bit) {
    return (static_cast<std::uint8_t>(set) & static_cast<std::uint8_t>(bit)) != 0;
}

struct GuestRegion {
    std::uintptr_t guest_base = 0;
    std::size_t size_bytes = 0;
    void* host_mapping = nullptr;
};

struct PageTableStats {
    std::size_t host_page_size = 4096;
    std::size_t guest_page_size = 4096;
    std::size_t tracked_guest_pages = 0;
    bool userfaultfd_active = false;
    bool mprotect_shim_active = false;
};

class VirtualMemoryManager {
public:
    VirtualMemoryManager();
    ~VirtualMemoryManager();

    VirtualMemoryManager(VirtualMemoryManager const&) = delete;
    VirtualMemoryManager& operator=(VirtualMemoryManager const&) = delete;

    bool Init(std::string* out_error);

    PageTableStats GetStats() const;

    bool AllocateGuestRegion(std::uintptr_t guest_base,
                             std::size_t size_bytes,
                             PagePerm initial_perm,
                             std::string* out_error);

    bool SetGuestPagePermissions(std::uintptr_t guest_address,
                                 std::size_t length_bytes,
                                 PagePerm perm,
                                 std::string* out_error);

    bool WriteGuestMemory(std::uintptr_t guest_address,
                          void const* src,
                          std::size_t len,
                          std::string* out_error);

    bool ReadGuestMemory(std::uintptr_t guest_address,
                         void* dst,
                         std::size_t len,
                         std::string* out_error) const;

    void SetInvalidationHook(
        std::function<void(std::uintptr_t, std::size_t)> cb) {
        std::lock_guard<std::mutex> lock(mutex_);
        invalidation_hook_ = std::move(cb);
    }

private:
    struct GuestPageMeta {
        PagePerm perm = PagePerm::None;
        bool dirty = false;
        std::uint64_t generation = 0;
    };

    bool MapHostBackingForRange(std::uintptr_t guest_begin,
                                  std::size_t length_bytes,
                                  std::string* out_error);

    bool ApplyMprotectForHostSlice(std::uintptr_t guest_begin,
                                   std::size_t length_bytes,
                                   std::string* out_error);

    bool SetGuestPagePermissionsLocked(std::uintptr_t guest_address,
                                       std::size_t length_bytes,
                                       PagePerm perm,
                                       std::string* out_error);

    using RegionMap = std::map<std::uintptr_t, GuestRegion>;
    RegionMap::iterator FindRegionContaining(std::uintptr_t guest_addr);
    RegionMap::const_iterator FindRegionContaining(std::uintptr_t guest_addr) const;

    std::uintptr_t AdvanceGeneration(std::uintptr_t begin, std::size_t len);

    void NotifyInvalidation(std::uintptr_t begin, std::size_t len);

    static std::size_t HostPageSize();
    static std::size_t FloorToGuestPage(std::uintptr_t addr);
    static std::size_t CeilToGuestPage(std::uintptr_t addr, std::size_t len);
    static bool Aligned(std::uintptr_t addr, std::size_t align);

    mutable std::mutex mutex_{};
    std::size_t host_page_size_ = 4096;
    RegionMap regions_;
    std::unordered_map<std::uintptr_t, GuestPageMeta> guest_page_map_;
    std::function<void(std::uintptr_t, std::size_t)> invalidation_hook_;
    std::uint64_t generation_counter_ = 1;

#if defined(RETROPORTAL_HAVE_USERFAULTFD) && defined(__linux__)
    struct UffdState;
    std::unique_ptr<UffdState> uffd_;
#endif
};

}  // namespace retroportal::vm
