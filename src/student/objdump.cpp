#include "fle.hpp"
#include <iomanip>
#include <iostream>
#include <sstream>

void FLE_objdump(const FLEObject& obj, FLEWriter& writer)
{
    writer.set_type(obj.type);

    // 如果是可执行文件，写入程序头和入口点
    if (obj.type == ".exe") {
        writer.write_program_headers(obj.phdrs);
        writer.write_entry(obj.entry);
    }

    // 写入所有段的内容
    for (const auto& [name, section] : obj.sections) {
        writer.begin_section(name);

        // 收集所有断点（符号和重定位的位置）
        std::vector<size_t> breaks;
        for (const auto& sym : obj.symbols) {
            if (sym.section == name) { // only collect symbols for current section
                breaks.push_back(sym.offset);
            }
        }
        for (const auto& reloc : section.relocs) {
            breaks.push_back(reloc.offset);
        }
        std::sort(breaks.begin(), breaks.end());
        breaks.erase(std::unique(breaks.begin(), breaks.end()), breaks.end());

        size_t pos = 0;
        while (pos < section.data.size()) {
            // 1. 检查当前位置是否有符号或重定位
            for (const auto& sym : obj.symbols) {
                if (sym.section == name && sym.offset == pos) {
                    std::string line;
                    switch (sym.type) {
                    case SymbolType::LOCAL:
                        line = "🏷️: " + sym.name;
                        break;
                    case SymbolType::WEAK:
                        line = "📎: " + sym.name;
                        break;
                    case SymbolType::GLOBAL:
                        line = "📤: " + sym.name;
                        break;
                    }
                    writer.write_line(line);
                }
            }

            for (const auto& reloc : section.relocs) {
                if (reloc.offset == pos) {
                    std::string reloc_format;
                    if (reloc.type == RelocationType::R_X86_64_PC32) {
                        reloc_format = ".rel";
                    } else if (reloc.type == RelocationType::R_X86_64_32) {
                        reloc_format = ".abs";
                    } else if (reloc.type == RelocationType::R_X86_64_64) {
                        reloc_format = ".abs64";
                    } else if (reloc.type == RelocationType::R_X86_64_32S) {
                        reloc_format = ".abs32s";
                    }

                    std::stringstream ss;
                    ss << "❓: " << reloc_format << "(" << reloc.symbol << ", 0x"
                       << std::hex << reloc.addend << ")";
                    writer.write_line(ss.str());
                }
            }

            // 2. 找出下一个断点
            size_t next_break = section.data.size();
            for (size_t brk : breaks) {
                if (brk > pos) {
                    next_break = brk;
                    break;
                }
            }

            // 3. 输出数据，每16字节一组
            while (pos < next_break) {
                std::stringstream ss;
                ss << "🔢: ";
                size_t chunk_size = std::min({
                    size_t(16), // 最大16字节
                    next_break - pos, // 到下一个断点
                    section.data.size() - pos // 剩余数据
                });

                for (size_t i = 0; i < chunk_size; ++i) {
                    ss << std::hex << std::setw(2) << std::setfill('0')
                       << static_cast<int>(section.data[pos + i]) << " ";
                }
                writer.write_line(ss.str());
                pos += chunk_size;
            }

            // 4. 如果是重定位，跳过4字节
            if (std::any_of(section.relocs.begin(), section.relocs.end(),
                    [pos](const auto& r) { return r.offset == pos; })) {
                pos += 4;
            }
        }

        writer.end_section();
    }
}