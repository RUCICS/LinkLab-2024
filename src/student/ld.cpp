#include "fle.hpp"
#include <cassert>
#include <iomanip>
#include <iostream>
#include <map>
#include <stdexcept>
#include <vector>

FLEObject FLE_ld(const std::vector<FLEObject>& objects)
{
    if (objects.empty()) {
        throw std::runtime_error("No input objects specified.");
    }

    FLEObject result;
    result.type = ".exe";

    using SectionName = std::string;
    struct RawSection {
        std::string file_name;
        FLESection section;
        int offset;
        int global_offset;
    };

    // 1. Collect all sections
    std::map<SectionName, std::vector<RawSection>> section_groups;
    std::vector<SectionName> ordered_section_names;

    for (const auto& obj : objects) {
        for (const auto& [section_name, raw_section] : obj.sections) {
            if (!raw_section.data.size())
                continue;

            section_groups[section_name].push_back({
                .file_name = obj.name,
                .section = raw_section,
                .offset = 0, // To be calculated later
                .global_offset = 0, // To be calculated later
            });
            if (std::find(ordered_section_names.begin(), ordered_section_names.end(), section_name) == ordered_section_names.end()) {
                ordered_section_names.push_back(section_name);
            }
        }
    }

    // 2. Merge sections and generate program headers

    uint64_t section_vaddr = 0;
    constexpr uint64_t BASE_VADDR = 0x400000;
    constexpr uint64_t PAGE_SIZE = 0x1000;

    for (auto name : ordered_section_names) {
        std::cout << "\nMerging section: " << name << std::endl;
        auto& sections = section_groups[name];

        FLESection merged_section;
        for (auto& raw_section : sections) {
            raw_section.offset = merged_section.data.size();
            raw_section.global_offset = section_vaddr + merged_section.data.size();
            merged_section.data.insert(merged_section.data.end(),
                raw_section.section.data.begin(), raw_section.section.data.end());
        }

        uint32_t flags = 0;
        if (name == ".text" || name.starts_with(".text.")) {
            flags = static_cast<uint32_t>(PHF::R) | static_cast<uint32_t>(PHF::X);
        } else if (name == ".rodata" || name.starts_with(".rodata.")) {
            flags = static_cast<uint32_t>(PHF::R);
        } else if (name == ".data" || name.starts_with(".data.")) {
            flags = static_cast<uint32_t>(PHF::R) | static_cast<uint32_t>(PHF::W);
        }

        auto section_size = static_cast<uint32_t>(merged_section.data.size());
        result.phdrs.push_back(ProgramHeader {
            .name = name,
            .vaddr = BASE_VADDR + section_vaddr,
            .size = section_size,
            .flags = flags });

        result.sections[name] = merged_section;
        section_vaddr += section_size;
        section_vaddr = (section_vaddr + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    }

    // 3. Collect all symbols
    std::map<std::string, Symbol> global_symbols;
    std::map<std::string, size_t> local_symbols;

    auto local_name_prefix = [](std::string_view obj_name, std::string_view sym_name) {
        return std::string(obj_name) + "." + std::string(sym_name);
    };

    std::cout << "\n=== Phase 2: Processing Symbols ===\n";
    for (const auto& obj : objects) {
        for (const auto& sym : obj.symbols) {
            std::cout << "Symbol: " << sym.name
                      << " from " << obj.name
                      << " type=" << (sym.type == SymbolType::LOCAL ? "LOCAL" : sym.type == SymbolType::WEAK ? "WEAK"
                                                                                                             : "GLOBAL")
                      << " section=" << sym.section
                      << " offset=0x" << std::hex << sym.offset << std::dec << std::endl;

            // First, find the section
            auto offset_it = section_groups.find(sym.section);
            if (offset_it == section_groups.end()) {
                throw std::runtime_error("Symbol " + sym.name + " refers to non-existent section " + sym.section);
            }

            // Second, find the raw section
            auto raw_section_it = std::find_if(offset_it->second.begin(), offset_it->second.end(),
                [&](const RawSection& section) { return section.file_name == obj.name; });
            if (raw_section_it == offset_it->second.end()) {
                throw std::runtime_error("Symbol " + sym.name + " in " + obj.name + " refers to non-existent section " + sym.section);
            }
            size_t symbol_global_offset = raw_section_it->global_offset + sym.offset;

            std::cout << "Symbol " << sym.name << " in " << obj.name << " at offset " << symbol_global_offset << std::endl;

            if (sym.type == SymbolType::LOCAL) {
                local_symbols[local_name_prefix(obj.name, sym.name)] = symbol_global_offset;
            } else {
                auto it = global_symbols.find(sym.name);
                if (it == global_symbols.end()) {
                    Symbol new_sym = sym;
                    new_sym.offset = symbol_global_offset;
                    global_symbols[sym.name] = new_sym;
                } else if (sym.type == SymbolType::GLOBAL && it->second.type == SymbolType::GLOBAL) {
                    throw std::runtime_error("Multiple definition of strong symbol: " + sym.name);
                } else if (sym.type == SymbolType::GLOBAL && it->second.type == SymbolType::WEAK) {
                    Symbol new_sym = sym;
                    new_sym.offset = symbol_global_offset;
                    it->second = new_sym;
                }
            }
        }
    }

    // 第三遍：处理重定位
    std::cout << "\n=== Phase 3: Processing Relocations ===\n";
    for (const auto& [name, sections] : section_groups) {
        for (const auto& raw_section : sections) {
            auto& section = raw_section.section;
            for (const auto& reloc : section.relocs) {
                size_t reloc_global_offset = raw_section.global_offset + reloc.offset;

                int64_t symbol_value;
                // First, check if it's a local symbol
                auto local_it = local_symbols.find(local_name_prefix(raw_section.file_name, reloc.symbol));
                if (local_it != local_symbols.end()) {
                    symbol_value = local_it->second;
                } else {
                    // Then, check if it's a global symbol
                    auto global_it = global_symbols.find(reloc.symbol);
                    if (global_it != global_symbols.end()) {
                        symbol_value = global_it->second.offset;
                    } else {
                        throw std::runtime_error("Undefined symbol: " + reloc.symbol);
                    }
                }

                std::cout << "\nRelocation in " << name
                          << " from " << raw_section.file_name << std::endl;
                std::cout << "  Type: " << (reloc.type == RelocationType::R_X86_64_32 ? "R_X86_64_32" : reloc.type == RelocationType::R_X86_64_PC32 ? "R_X86_64_PC32"
                                                                                                                                                    : "R_X86_64_64")
                          << " at offset 0x" << std::hex << reloc_global_offset
                          << " symbol=" << reloc.symbol
                          << " addend=" << reloc.addend << std::dec << std::endl;

                std::cout << "  Symbol value: 0x" << std::hex << symbol_value << std::dec << std::endl;

                // 计算重定位值
                int64_t value;
                switch (reloc.type) {
                case RelocationType::R_X86_64_32:
                    value = BASE_VADDR + symbol_value + reloc.addend;
                    break;
                case RelocationType::R_X86_64_PC32:
                    value = symbol_value + reloc.addend - reloc_global_offset - 8;
                    break;
                case RelocationType::R_X86_64_64:
                    value = BASE_VADDR + symbol_value + reloc.addend;
                    break;
                default:
                    throw std::runtime_error("Unsupported relocation type");
                }

                std::cout << "  Final value: 0x" << std::hex << value << std::dec << std::endl;

                // 写入重定位值
                size_t size = (reloc.type == RelocationType::R_X86_64_64) ? 8 : 4;
                size_t reloc_offset = raw_section.offset + reloc.offset;
                for (size_t i = 0; i != size; ++i) {
                    result.sections[name].data[reloc_offset + i] = (value >> (i * 8)) & 0xFF;
                }
            }
        }
    }

    // 设置入口点（_start 符号的位置）
    auto start_it = global_symbols.find("_start");
    if (start_it == global_symbols.end()) {
        throw std::runtime_error("No _start symbol found");
    }
    result.entry = BASE_VADDR + start_it->second.offset;

    std::cout << "\n=== Phase 4: Finalizing ===\n";
    std::cout << "Entry point: 0x" << std::hex << result.entry << std::dec << std::endl;
    std::cout << "Total size: 0x" << std::hex << result.sections[".text"].data.size() << std::dec << " bytes\n";

    return result;
}