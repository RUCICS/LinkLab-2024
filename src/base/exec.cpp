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

    if (obj.sections.find(".load") == obj.sections.end()) {
        throw std::runtime_error("No .load section found.");
    }

    const auto& load_section = obj.sections.at(".load");
    const auto& data = load_section.data;

    auto start_it = std::find_if(load_section.symbols.begin(), load_section.symbols.end(), [](const Symbol& s) {
        return s.name == "_start";
    });
    if (start_it == load_section.symbols.end()) {
        throw std::runtime_error("No _start symbol found.");
    }

    size_t size = data.size();
    void* mem = mmap(nullptr, size, PROT_READ | PROT_WRITE | PROT_EXEC,
        MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (mem == MAP_FAILED) {
        throw std::runtime_error(std::string("mmap failed: ") + strerror(errno));
    }

    memcpy(mem, data.data(), size);

    using FuncType = int (*)();
    FuncType func = reinterpret_cast<FuncType>(
        static_cast<uint8_t*>(mem) + start_it->offset);

    func(); // NoReturn

    assert(false);

    // munmap(mem, size);
}