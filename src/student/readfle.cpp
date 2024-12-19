#include "fle.hpp"
#include <iomanip>
#include <iostream>

void print_fle_info(const FLEObject& obj)
{
    // 计算重定位项总数
    size_t total_relocs = 0;
    for (const auto& [name, section] : obj.sections) {
        total_relocs += section.relocs.size();
    }

    // 打印基本信息
    std::cout << "FLE File Information:" << std::endl;
    std::cout << "Sections: " << obj.sections.size() << std::endl;
    std::cout << "Symbols: " << obj.symbols.size() << std::endl;
    std::cout << "Relocations: " << total_relocs << std::endl;
    std::cout << std::endl;

    // 打印节信息
    std::cout << "Section Summary:" << std::endl;
    for (const auto& [name, section] : obj.sections) {
        std::cout << name << ": " << section.data.size() << " bytes";

        // 判断节的类型
        std::string type;
        if (name == ".text") {
            type = "PROGRAM";
        } else if (name == ".data") {
            type = "DATA";
        } else if (name == ".bss") {
            type = "BSS";
        } else {
            type = "UNKNOWN";
        }

        std::cout << " (" << type << ")" << std::endl;
    }
}

void FLE_readfle(const FLEObject& obj)
{
    // 打印文件信息
    print_fle_info(obj);
}