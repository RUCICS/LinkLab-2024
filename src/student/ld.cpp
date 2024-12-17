#include "fle.hpp"
#include <cassert>
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
    std::map<std::string, size_t> section_offsets; // 每个原始段在 .load 中的偏移
    FLESection load_section;

    // 先收集所有段，按名字分组，使用值而不是指针
    std::map<std::string, std::vector<FLESection>> section_groups;
    for (const auto& obj : objects) {
        for (const auto& [name, section] : obj.sections) {
            section_groups[name].push_back(section);
        }
    }

    // 计算每个段的偏移
    for (const auto& [name, sections] : section_groups) {
        section_offsets[name] = load_section.data.size();
        for (const auto& section : sections) {
            load_section.data.insert(load_section.data.end(),
                section.data.begin(), section.data.end());
        }
    }

    // 第二遍：收集所有符号
    std::map<std::string, Symbol> global_symbols; // 全局符号表
    std::map<std::string, size_t> local_symbols; // 局部符号表

    for (const auto& obj : objects) {
        for (const auto& sym : obj.symbols) {
            std::cerr << "Processing symbol: " << sym.name << " in section " << sym.section << " at offset " << sym.offset << std::endl;
            // 检查符号所在的节是否存在
            auto offset_it = section_offsets.find(std::string(sym.section));
            if (offset_it == section_offsets.end()) {
                throw std::runtime_error("Symbol " + sym.name + " refers to non-existent section " + sym.section);
            }

            // 计算符号在 .load 段中的绝对偏移
            size_t abs_offset = offset_it->second + sym.offset;

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
        size_t base_offset = section_offsets[name];
        for (const auto& section : sections) {
            for (const auto& reloc : section.relocs) {
                Relocation new_reloc = reloc;
                new_reloc.offset += base_offset;

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
                case RelocationType::R_X86_64_PLT32:
                    value = symbol_value + reloc.addend - new_reloc.offset;
                    break;
                default:
                    throw std::runtime_error("Unsupported relocation type");
                }

                // 写入重定位值
                for (int i = 0; i < 4; ++i) {
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