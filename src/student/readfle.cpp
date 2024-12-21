#include "fle.hpp"
#include <iostream>

void print_fle_info(const FLEObject& obj)
{
    // Calculate the total number of relocation items
    size_t total_relocs = 0;
    for (const auto& [name, section] : obj.sections) {
        total_relocs += section.relocs.size();
    }

    // Print basic information
    std::cout << "FLE File Information:" << std::endl;
    std::cout << "Sections: " << obj.sections.size() << std::endl;
    std::cout << "Symbols: " << obj.symbols.size() << std::endl;
    std::cout << "Relocations: " << total_relocs << std::endl;
    std::cout << std::endl;

    // Print section information
    std::cout << "Section Summary:" << std::endl;
    for (const auto& [name, section] : obj.sections) {
        std::cout << name << ": " << section.data.size() << " bytes";

        // Determine the type of the section
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
    // Print file information
    print_fle_info(obj);
}