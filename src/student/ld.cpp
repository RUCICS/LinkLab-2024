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

    // 第一遍：收集所有段并计算偏移
    // 先收集所有段，按名字分组，使用值而不是指针

    using SectionName = std::string;
    struct RawSection {
        std::string file_name;
        FLESection section;
        int offset;
    };

    // 1. Collect all sections
    std::map<SectionName, std::vector<RawSection>> section_groups;
    std::vector<SectionName> ordered_section_names;
    for (const auto& obj : objects) {
        for (const auto& [section_name, raw_section] : obj.sections) {
            if (!raw_section.data.size()) {
                continue;
            }
            section_groups[section_name].push_back({
                .file_name = obj.name,
                .section = raw_section,
                .offset = 0, // To be calculated later
            });
            if (std::find(ordered_section_names.begin(), ordered_section_names.end(), section_name) == ordered_section_names.end()) {
                ordered_section_names.push_back(section_name);
            }
        }
    }

    // 2. Concatenate all sections
    FLESection load_section;
    for (auto name : ordered_section_names) {
        auto& sections = section_groups[name];
        std::cout << "Processing section group: " << name << std::endl;
        for (auto& raw_section : sections) {
            std::cout << "Processing section: " << name << " from " << raw_section.file_name << std::endl;

            auto& section = raw_section.section;
            std::cout << "Adding bytes from 0x" << std::hex << std::setfill('0') << std::setw(2)
                      << static_cast<int>(section.data.front()) << " to 0x"
                      << std::hex << std::setfill('0') << std::setw(2)
                      << static_cast<int>(section.data.back())
                      << " into " << std::dec << load_section.data.size() << std::endl;

            raw_section.offset = load_section.data.size();

            load_section.data.insert(load_section.data.end(),
                section.data.begin(), section.data.end());
        }
    }

    // 2. Collect all symbols
    std::map<std::string, Symbol> global_symbols; // 全局符号表
    std::map<std::string, size_t> local_symbols; // 局部符号表

    for (const auto& obj : objects) {
        for (const auto& sym : obj.symbols) {
            // First, find the section
            auto offset_it = section_groups.find(sym.section);
            if (offset_it == section_groups.end()) {
                throw std::runtime_error("Symbol " + sym.name + " refers to non-existent section " + sym.section);
            }

            // Second, find the raw section
            auto raw_section_it = std::find_if(offset_it->second.begin(), offset_it->second.end(), [sym, &obj](const RawSection& section) {
                return section.file_name == obj.name;
            });
            if (raw_section_it == offset_it->second.end()) {
                throw std::runtime_error("Symbol " + sym.name + " in " + obj.name + " refers to non-existent section " + sym.section);
            }
            size_t abs_offset = raw_section_it->offset + sym.offset;

            std::cout << "Symbol " << sym.name << " in " << obj.name << " at offset " << abs_offset << std::endl;

            if (sym.type == SymbolType::LOCAL) {
                local_symbols[sym.name] = abs_offset;
            } else {
                auto it = global_symbols.find(sym.name);
                if (it == global_symbols.end()) {
                    Symbol new_sym = sym;
                    new_sym.section = ".load";
                    new_sym.offset = abs_offset;
                    global_symbols[sym.name] = new_sym;
                } else if (it->second.type == SymbolType::GLOBAL && sym.type == SymbolType::GLOBAL) {
                    throw std::runtime_error("Multiple definition of strong symbol: " + sym.name);
                } else if (sym.type == SymbolType::GLOBAL || (sym.type == SymbolType::WEAK && it->second.type == SymbolType::WEAK)) {
                    Symbol new_sym = sym;
                    new_sym.section = ".load";
                    new_sym.offset = abs_offset;
                    it->second = new_sym;
                }
            }
        }
    }

    // 第三遍：处理重定位
    for (const auto& [name, sections] : section_groups) {
        for (const auto& raw_section : sections) {
            auto& section = raw_section.section;
            for (const auto& reloc : section.relocs) {
                Relocation new_reloc = reloc;
                new_reloc.offset += raw_section.offset;

                // 查找符号值
                int64_t symbol_value;
                auto global_it = global_symbols.find(reloc.symbol);
                if (global_it != global_symbols.end()) {
                    symbol_value = global_it->second.offset;
                } else {
                    auto local_it = local_symbols.find(reloc.symbol);
                    if (local_it == local_symbols.end()) {
                        throw std::runtime_error("Undefined symbol: " + reloc.symbol);
                    }
                    symbol_value = local_it->second;
                }

                // 计算重定位值
                int64_t value;
                switch (reloc.type) {
                case RelocationType::R_X86_64_32:
                    value = symbol_value + reloc.addend;
                    break;
                case RelocationType::R_X86_64_PC32:
                    value = symbol_value + reloc.addend - new_reloc.offset - 8;
                    break;
                default:
                    throw std::runtime_error("Unsupported relocation type");
                }

                // 写入重定位值
                for (int i = 0; i < 4; ++i) {
                    // Assume little-endian
                    load_section.data[new_reloc.offset + i] = (value >> (i * 8)) & 0xFF;
                }
            }
        }
    }

    // 设置入口点（_start 符号的位置）
    auto start_it = global_symbols.find("_start");
    if (start_it == global_symbols.end()) {
        throw std::runtime_error("No _start symbol found");
    }
    result.entry = start_it->second.offset;

    // 保存结果
    result.sections[".load"] = load_section;

    return result;
}