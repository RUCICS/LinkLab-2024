#include "fle.hpp"
#include "utils.hpp"
#include <cstring>
#include <iostream>
#include <sys/mman.h>
#include <unistd.h>

void FLE_exec(const std::vector<std::string>& files)
{
    json fle = load_fle(files[0]);
    if (fle["type"] != ".exe") {
        std::cerr << "File is not an executable FLE." << std::endl;
        return;
    }

    std::vector<uint8_t> bs;
    for (const auto& line : fle[".load"]) {
        if (line.get<std::string>().substr(0, 5) == "🔢:") {
            std::stringstream ss(line.get<std::string>().substr(5));
            uint32_t byte;
            while (ss >> std::hex >> byte) {
                bs.push_back(static_cast<uint8_t>(byte));
            }
        }
    }

    size_t size = bs.size();
    void* mem = mmap(nullptr, size, PROT_READ | PROT_WRITE | PROT_EXEC,
        MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (mem == MAP_FAILED) {
        perror("mmap");
        exit(EXIT_FAILURE);
    }

    memcpy(mem, bs.data(), size);

    using FuncType = int (*)();
    FuncType func = reinterpret_cast<FuncType>(
        static_cast<uint8_t*>(mem) + fle["symbols"]["_start"].get<int>());
    func();
}