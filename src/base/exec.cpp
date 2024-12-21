#include "fle.hpp"
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <sys/mman.h>
#include <unistd.h>
#include <vector>

void FLE_exec(const FLEObject& obj)
{
    if (obj.type != ".exe") {
        throw std::runtime_error("File is not an executable FLE.");
    }

    // 根据程序头映射内存
    for (const auto& phdr : obj.phdrs) {
        void* addr = mmap((void*)phdr.vaddr, phdr.size,
            (phdr.flags & static_cast<uint32_t>(PHF::R) ? PROT_READ : 0)
                | (phdr.flags & static_cast<uint32_t>(PHF::W) ? PROT_WRITE : 0)
                | (phdr.flags & static_cast<uint32_t>(PHF::X) ? PROT_EXEC : 0),
            MAP_PRIVATE | MAP_FIXED | MAP_ANONYMOUS, -1, 0);

        if (addr == MAP_FAILED) {
            throw std::runtime_error(std::string("mmap failed: ") + strerror(errno));
        }

        // 复制段数据
        auto it = obj.sections.find(phdr.name);
        if (it == obj.sections.end()) {
            throw std::runtime_error("Section not found: " + phdr.name);
        }
        memcpy(addr, it->second.data.data(), phdr.size);
    }

    // 跳转到入口点
    using FuncType = int (*)();
    FuncType func = reinterpret_cast<FuncType>(obj.entry);
    func();
}