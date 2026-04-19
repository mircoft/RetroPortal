#include "retroportal/VirtualMemoryManager.hpp"

#include <algorithm>
#include <cerrno>
#include <cstring>

#if defined(__ANDROID__)
#include <sys/mman.h>
#include <unistd.h>
#elif defined(__linux__)
#include <sys/mman.h>
#include <unistd.h>
#endif

#if defined(RETROPORTAL_HAVE_USERFAULTFD) && defined(__linux__)
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <linux/userfaultfd.h>
#endif

#include <android/log.h>

namespace retroportal::vm {

namespace {

constexpr std::uintptr_t kGuestPageSize = 4096;

void LogViolation(char const* msg) {
    __android_log_print(ANDROID_LOG_WARN, "RetroPortalVM", "%s", msg);
}

int MprotectBits(PagePerm p) {
    bool const r = Has(p, PagePerm::Read);
    bool const w = Has(p, PagePerm::Write);
    bool const x = Has(p, PagePerm::Exec);
    if (!r && !w && !x) {
        return PROT_NONE;
    }
    int prot = 0;
    if (r) {
        prot |= PROT_READ;
    }
    if (w) {
        prot |= PROT_WRITE;
    }
    if (x) {
        prot |= PROT_EXEC;
    }
    return prot;
}

bool MergeSliceUniform(std::vector<PagePerm> const& slice, PagePerm* out) {
    if (slice.empty()) {
        return false;
    }
    PagePerm first = slice.front();
    for (auto const p : slice) {
        if (static_cast<std::uint8_t>(p) != static_cast<std::uint8_t>(first)) {
            return false;
        }
    }
    *out = first;
    return true;
}

}  // namespace

VirtualMemoryManager::VirtualMemoryManager() = default;

VirtualMemoryManager::~VirtualMemoryManager() {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& kv : regions_) {
        GuestRegion& gr = kv.second;
        if (gr.host_mapping != nullptr && gr.size_bytes > 0) {
            ::munmap(gr.host_mapping, gr.size_bytes);
            gr.host_mapping = nullptr;
            gr.size_bytes = 0;
        }
    }
    regions_.clear();
    guest_page_map_.clear();
#if defined(RETROPORTAL_HAVE_USERFAULTFD) && defined(__linux__)
    uffd_.reset();
#endif
}

std::size_t VirtualMemoryManager::HostPageSize() {
    long const ps = ::sysconf(_SC_PAGESIZE);
    if (ps <= 0) {
        return 4096;
    }
    return static_cast<std::size_t>(ps);
}

std::size_t VirtualMemoryManager::FloorToGuestPage(std::uintptr_t addr) {
    return addr - (addr % kGuestPageSize);
}

std::size_t VirtualMemoryManager::CeilToGuestPage(std::uintptr_t addr,
                                                  std::size_t len) {
    std::uintptr_t const end = addr + len;
    std::uintptr_t const rounded =
        ((end + kGuestPageSize - 1) / kGuestPageSize) * kGuestPageSize;
    return static_cast<std::size_t>(rounded - FloorToGuestPage(addr));
}

bool VirtualMemoryManager::Aligned(std::uintptr_t addr, std::size_t align) {
    if (align == 0) {
        return false;
    }
    return (addr % align) == 0;
}

bool VirtualMemoryManager::Init(std::string* out_error) {
    host_page_size_ = HostPageSize();
    if (host_page_size_ < kGuestPageSize) {
        if (out_error) {
            *out_error = "Host page size smaller than guest page size";
        }
        return false;
    }
    if ((host_page_size_ % kGuestPageSize) != 0) {
        if (out_error) {
            *out_error = "Host page size must be multiple of guest 4KiB pages";
        }
        return false;
    }
#if defined(RETROPORTAL_HAVE_USERFAULTFD) && defined(__linux__)
    // Engineering-only path: would register userfaultfd and fault thread.
    // Disabled by default on Android retail builds.
#endif
    return true;
}

PageTableStats VirtualMemoryManager::GetStats() const {
    std::lock_guard<std::mutex> lock(mutex_);
    PageTableStats s{};
    s.host_page_size = host_page_size_;
    s.guest_page_size = kGuestPageSize;
    s.tracked_guest_pages = guest_page_map_.size();
#if defined(RETROPORTAL_HAVE_USERFAULTFD) && defined(__linux__)
    s.userfaultfd_active = uffd_ != nullptr;
#else
    s.userfaultfd_active = false;
#endif
    s.mprotect_shim_active = host_page_size_ >= kGuestPageSize;
    return s;
}

bool VirtualMemoryManager::MapHostBackingForRange(std::uintptr_t /*guest_begin*/,
                                                  std::size_t /*length_bytes*/,
                                                  std::string* /*out_error*/) {
    return true;
}

VirtualMemoryManager::RegionMap::iterator
VirtualMemoryManager::FindRegionContaining(std::uintptr_t guest_addr) {
    for (auto it = regions_.begin(); it != regions_.end(); ++it) {
        GuestRegion const& gr = it->second;
        if (guest_addr >= gr.guest_base &&
            guest_addr < gr.guest_base + gr.size_bytes) {
            return it;
        }
    }
    return regions_.end();
}

VirtualMemoryManager::RegionMap::const_iterator
VirtualMemoryManager::FindRegionContaining(std::uintptr_t guest_addr) const {
    for (auto it = regions_.begin(); it != regions_.end(); ++it) {
        GuestRegion const& gr = it->second;
        if (guest_addr >= gr.guest_base &&
            guest_addr < gr.guest_base + gr.size_bytes) {
            return it;
        }
    }
    return regions_.end();
}

bool VirtualMemoryManager::ApplyMprotectForHostSlice(std::uintptr_t guest_begin,
                                                     std::size_t length_bytes,
                                                     std::string* out_error) {
    auto it = FindRegionContaining(guest_begin);
    if (it == regions_.end()) {
        if (out_error) {
            *out_error = "guest address not contained in any region";
        }
        return false;
    }
    GuestRegion& gr = it->second;
    std::uintptr_t const offset = guest_begin - gr.guest_base;
    if (offset + length_bytes > gr.size_bytes) {
        if (out_error) {
            *out_error = "range exceeds guest region";
        }
        return false;
    }

    std::uintptr_t const host_slice_start =
        reinterpret_cast<std::uintptr_t>(gr.host_mapping) + offset;
    std::size_t const host_slice_len = length_bytes;

    std::uintptr_t align = host_page_size_;
    std::uintptr_t slice_floor =
        (host_slice_start / align) * align;
    std::uintptr_t slice_ceil =
        ((host_slice_start + host_slice_len + align - 1) / align) * align;

    for (std::uintptr_t hs = slice_floor; hs < slice_ceil; hs += align) {
        std::uintptr_t guest_addr_start =
            gr.guest_base + (hs - reinterpret_cast<std::uintptr_t>(gr.host_mapping));
        std::size_t pages_in_host =
            host_page_size_ / kGuestPageSize;
        std::vector<PagePerm> perms;
        perms.reserve(pages_in_host);
        for (std::size_t i = 0; i < pages_in_host; ++i) {
            std::uintptr_t ga = guest_addr_start + i * kGuestPageSize;
            auto meta = guest_page_map_.find(ga);
            if (meta == guest_page_map_.end()) {
                perms.push_back(PagePerm::None);
            } else {
                perms.push_back(meta->second.perm);
            }
        }
        PagePerm uniform = PagePerm::None;
        if (MergeSliceUniform(perms, &uniform)) {
            void* addr = reinterpret_cast<void*>(hs);
            if (::mprotect(addr, host_page_size_, MprotectBits(uniform)) != 0) {
                if (out_error) {
                    *out_error = std::string("mprotect failed: ") + std::strerror(errno);
                }
                return false;
            }
        } else {
            LogViolation(
                "Mixed perms inside host page; using RW mapping as conservative "
                "fallback — enforce W^X in emulator layer.");
            void* addr = reinterpret_cast<void*>(hs);
            if (::mprotect(addr, host_page_size_, PROT_READ | PROT_WRITE) != 0) {
                if (out_error) {
                    *out_error = std::string("mprotect RW fallback failed: ") +
                                 std::strerror(errno);
                }
                return false;
            }
        }
    }
    return true;
}

std::uintptr_t VirtualMemoryManager::AdvanceGeneration(std::uintptr_t begin,
                                                       std::size_t len) {
    std::uintptr_t const gp0 = FloorToGuestPage(begin);
    std::uintptr_t const pages =
        (len + kGuestPageSize - 1) / kGuestPageSize + 1;
    for (std::uintptr_t i = 0; i < pages; ++i) {
        std::uintptr_t ga = gp0 + i * kGuestPageSize;
        auto it = guest_page_map_.find(ga);
        if (it != guest_page_map_.end()) {
            it->second.generation = generation_counter_;
            it->second.dirty = true;
        }
    }
    generation_counter_++;
    return generation_counter_;
}

void VirtualMemoryManager::NotifyInvalidation(std::uintptr_t begin,
                                                std::size_t len) {
    if (invalidation_hook_) {
        invalidation_hook_(begin, len);
    }
}

bool VirtualMemoryManager::AllocateGuestRegion(std::uintptr_t guest_base,
                                               std::size_t size_bytes,
                                               PagePerm initial_perm,
                                               std::string* out_error) {
    if (!Aligned(guest_base, kGuestPageSize)) {
        if (out_error) {
            *out_error = "guest_base must be 4KiB aligned";
        }
        return false;
    }
    if (size_bytes == 0 || (size_bytes % kGuestPageSize) != 0) {
        if (out_error) {
            *out_error = "size must be positive multiple of 4KiB";
        }
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    if (regions_.count(guest_base) != 0) {
        if (out_error) {
            *out_error = "region already allocated at guest_base";
        }
        return false;
    }

    void* p = ::mmap(nullptr, size_bytes, PROT_NONE,
                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (p == MAP_FAILED) {
        if (out_error) {
            *out_error = std::string("mmap failed: ") + std::strerror(errno);
        }
        return false;
    }

    GuestRegion gr{};
    gr.guest_base = guest_base;
    gr.size_bytes = size_bytes;
    gr.host_mapping = p;
    regions_.emplace(guest_base, gr);

    if (!SetGuestPagePermissionsLocked(guest_base, size_bytes, initial_perm,
                                       out_error)) {
        ::munmap(p, size_bytes);
        regions_.erase(guest_base);
        return false;
    }
    return true;
}

bool VirtualMemoryManager::SetGuestPagePermissionsLocked(
    std::uintptr_t guest_address,
    std::size_t length_bytes,
    PagePerm perm,
    std::string* out_error) {
    auto it = FindRegionContaining(guest_address);
    if (it == regions_.end()) {
        if (out_error) {
            *out_error = "guest address outside regions";
        }
        return false;
    }
    GuestRegion const& gr = it->second;
    if (guest_address + length_bytes > gr.guest_base + gr.size_bytes) {
        if (out_error) {
            *out_error = "permission range not fully inside one region";
        }
        return false;
    }

    for (std::uintptr_t ga = guest_address;
         ga < guest_address + length_bytes; ga += kGuestPageSize) {
        GuestPageMeta& meta = guest_page_map_[ga];
        meta.perm = perm;
        meta.generation = generation_counter_;
    }
    generation_counter_++;

    std::uintptr_t const gp_start = FloorToGuestPage(guest_address);
    std::uintptr_t adjust_start = gp_start;
    std::size_t adjust_len =
        guest_address + length_bytes > gp_start
            ? (guest_address + length_bytes - gp_start)
            : length_bytes;

    if (!ApplyMprotectForHostSlice(adjust_start, adjust_len, out_error)) {
        return false;
    }

    NotifyInvalidation(guest_address, length_bytes);
    return true;
}

bool VirtualMemoryManager::SetGuestPagePermissions(std::uintptr_t guest_address,
                                                   std::size_t length_bytes,
                                                   PagePerm perm,
                                                   std::string* out_error) {
    if ((guest_address % kGuestPageSize) != 0) {
        if (out_error) {
            *out_error = "guest_address must be 4KiB aligned";
        }
        return false;
    }
    if (length_bytes == 0 || (length_bytes % kGuestPageSize) != 0) {
        if (out_error) {
            *out_error = "length must be multiple of 4KiB";
        }
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    return SetGuestPagePermissionsLocked(guest_address, length_bytes, perm,
                                         out_error);
}

bool VirtualMemoryManager::WriteGuestMemory(std::uintptr_t guest_address,
                                            void const* src,
                                            std::size_t len,
                                            std::string* out_error) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = FindRegionContaining(guest_address);
    if (it == regions_.end()) {
        if (out_error) {
            *out_error = "guest address outside regions";
        }
        return false;
    }
    GuestRegion const& gr = it->second;
    std::uintptr_t offset = guest_address - gr.guest_base;
    if (offset + len > gr.size_bytes) {
        if (out_error) {
            *out_error = "write spans past region";
        }
        return false;
    }
    auto meta_it = guest_page_map_.find(FloorToGuestPage(guest_address));
    if (meta_it == guest_page_map_.end() ||
        !Has(meta_it->second.perm, PagePerm::Write)) {
        if (out_error) {
            *out_error = "guest page not writable";
        }
        return false;
    }
    unsigned char* dst_ptr =
        static_cast<unsigned char*>(gr.host_mapping) + offset;
    std::memcpy(dst_ptr, src, len);
    AdvanceGeneration(guest_address, len);
    return true;
}

bool VirtualMemoryManager::ReadGuestMemory(std::uintptr_t guest_address,
                                           void* dst,
                                           std::size_t len,
                                           std::string* out_error) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = FindRegionContaining(guest_address);
    if (it == regions_.end()) {
        if (out_error) {
            *out_error = "guest address outside regions";
        }
        return false;
    }
    GuestRegion const& gr = it->second;
    std::uintptr_t offset = guest_address - gr.guest_base;
    if (offset + len > gr.size_bytes) {
        if (out_error) {
            *out_error = "read spans past region";
        }
        return false;
    }
    auto meta_it = guest_page_map_.find(FloorToGuestPage(guest_address));
    if (meta_it == guest_page_map_.end() ||
        !Has(meta_it->second.perm, PagePerm::Read)) {
        if (out_error) {
            *out_error = "guest page not readable";
        }
        return false;
    }
    unsigned char const* src_ptr =
        static_cast<unsigned char const*>(gr.host_mapping) + offset;
    std::memcpy(dst, src_ptr, len);
    return true;
}

}  // namespace retroportal::vm
