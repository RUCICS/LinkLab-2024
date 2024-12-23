#include "fle.hpp"
#include "string_utils.hpp"
#include <filesystem>
#include <iostream>
#include <regex>
#include <sstream>
#include <string>
#include <string_view>

using namespace std::string_literals;
using namespace std::string_view_literals;

namespace {
std::string exec(std::string_view cmd)
{
    auto final_cmd = cmd.data() + " 2>/dev/null"s;
    FILE* pipe = popen(final_cmd.c_str(), "r");
    if (!pipe) {
        throw std::runtime_error("popen() failed!");
    }

    std::string result;
    char buffer[128];
    size_t bytes_read;

    while ((bytes_read = fread(buffer, 1, sizeof(buffer), pipe)) != 0) {
        result.append(buffer, bytes_read);
    }

    pclose(pipe);
    return result;
}

std::vector<std::string> elf_to_fle(const std::string& binary, const std::string& section, bool is_bss = false)
{
    std::vector<std::string> res;

    // 先处理符号表
    std::string command = "objdump -t " + binary;
    std::string names = exec(command.c_str());

    struct Symbol {
        char symb_type;
        std::string section;
        unsigned int offset;
        unsigned int size;
        std::string name;
    };

    std::vector<Symbol> symbols;
    std::regex symbol_pattern(
        R"(^([0-9a-fA-F]+)\s+(l|g|w)\s+(\w+)?\s+([.a-zA-Z0-9_]+)\s+([0-9a-fA-F]+)\s+(.*)$)");
    for (auto& line : splitlines(names)) {
        if (std::smatch match; std::regex_match(line, match, symbol_pattern)) {
            if (match[4].str() == section) {
                unsigned int offset = std::stoul(match[1].str(), nullptr, 16);
                char symb_type = match[2].str()[0];
                unsigned int size = std::stoul(match[5].str(), nullptr, 16);
                std::string name = match[6].str();

                symbols.push_back(Symbol(symb_type, section, offset, size, name));
            }
        }
    }

    // 如果是BSS段，只需要处理符号
    if (is_bss) {
        for (const auto& sym : symbols) {
            std::string sym_line;
            if (sym.symb_type == 'l') {
                sym_line = "🏷️: " + sym.name + " " + std::to_string(sym.size);
            } else if (sym.symb_type == 'g') {
                sym_line = "📤: " + sym.name + " " + std::to_string(sym.size);
            } else if (sym.symb_type == 'w') {
                sym_line = "📎: " + sym.name + " " + std::to_string(sym.size);
            }
            res.push_back(sym_line);
        }
        return res;
    }

    // 对于非BSS段，还需要处理数据和重定位
    command = "objcopy --dump-section " + section + "=/dev/stdout " + binary;
    std::string section_data = exec(command.c_str());

    command = "readelf -r " + binary;
    std::string relocs = exec(command.c_str());

    std::map<int, std::pair<int, std::string>> relocations;
    bool enabled = true;
    std::regex reloc_pattern(
        R"(^\s*([0-9a-fA-F]+)\s+([0-9a-fA-F]+)\s+(\S+)\s+([0-9a-fA-F]+)\s+(.*)$)");
    for (auto& line : splitlines(relocs)) {
        if (line.find("Relocation section") != std::string::npos) {
            enabled = line.find(".rela" + section) != std::string::npos;
        } else if (enabled) {
            if (std::smatch match; std::regex_match(line, match, reloc_pattern)) {
                int offset = std::stoi(match[1], nullptr, 16);
                std::string symbol = match[5];

                size_t at_pos = symbol.find('@');
                if (at_pos != std::string::npos) {
                    symbol = symbol.substr(0, at_pos);
                }

                std::string reloc_type = match[3];
                std::string reloc_format;
                if (reloc_type == "R_X86_64_PC32" || reloc_type == "R_X86_64_PLT32") {
                    reloc_format = ".rel";
                } else if (reloc_type == "R_X86_64_64") {
                    reloc_format = ".abs64";
                } else if (reloc_type == "R_X86_64_32") {
                    reloc_format = ".abs";
                } else if (reloc_type == "R_X86_64_32S") {
                    reloc_format = ".abs32s";
                } else {
                    throw std::runtime_error("Unsupported relocation type: " + reloc_type);
                }

                std::stringstream ss;
                ss << reloc_format << "(" << symbol << ")";
                size_t size = (reloc_type == "R_X86_64_64") ? 8 : 4;
                relocations[offset] = { size, ss.str() };
            }
        }
    }

    int skip = 0;
    std::vector<uint8_t> holding;

    auto do_dump = [&](std::vector<uint8_t>& holding) {
        if (!holding.empty()) {
            std::stringstream ss;
            ss << "🔢: ";
            for (auto& byte : holding) {
                ss << std::hex << std::setw(2) << std::setfill('0') << (int)byte << " ";
            }
            res.push_back(trim(ss.str()));
            holding.clear();
        }
    };

    for (size_t i = 0, len = section_data.size(); i < len; ++i) {
        for (auto& sym : symbols) {
            if (sym.offset == i) {
                do_dump(holding);
                if (sym.symb_type == 'l') {
                    res.push_back("🏷️: " + sym.name + " " + std::to_string(sym.size));
                } else if (sym.symb_type == 'g') {
                    res.push_back("📤: " + sym.name + " " + std::to_string(sym.size));
                } else if (sym.symb_type == 'w') {
                    res.push_back("📎: " + sym.name + " " + std::to_string(sym.size));
                } else {
                    throw std::runtime_error("Unsupported symbol type: " + std::string(1, sym.symb_type));
                }
            }
        }
        if (relocations.find(i) != relocations.end()) {
            auto [skip_temp, reloc] = relocations[i];
            do_dump(holding);
            res.push_back("❓: " + reloc);
            skip = skip_temp;
        }
        if (skip > 0) {
            --skip;
        } else {
            holding.push_back(section_data[i]);
            if (holding.size() == 16) {
                do_dump(holding);
            }
        }
    }
    do_dump(holding);

    return res;
}
}

constexpr std::array CFLAGS = {
    "-static"sv,
    // "-fPIE"sv,
    "-fno-common"sv,
    "-nostdlib"sv,
    "-ffreestanding"sv,
    "-fno-asynchronous-unwind-tables"sv,
};

void FLE_cc(const std::vector<std::string>& options)
{
    std::cout << "options: " << join(options, " ") << std::endl;

    auto it = std::find(options.begin(), options.end(), "-o");
    std::string binary;
    if (it == options.end() || ++it == options.end()) {
        binary = "a.out";
    } else {
        binary = *it;
    }
    std::cout << "binary: " << binary << std::endl;

    std::vector<std::string> gcc_cmd = { "gcc", "-c" };
    gcc_cmd.insert(gcc_cmd.end(), CFLAGS.begin(), CFLAGS.end());
    gcc_cmd.insert(gcc_cmd.end(), options.begin(), options.end());

    std::string command = join(gcc_cmd, " ");
    if (std::system(command.c_str()) != 0) {
        throw std::runtime_error("gcc command failed");
    }

    command = "objdump -h " + binary;
    std::string objdump_output = exec(command);
    auto lines = splitlines(objdump_output);

    FLEWriter writer;
    writer.set_type(".obj");

    std::vector<SectionHeader> shdrs;

    std::regex section_pattern(R"(^\s*([0-9]+)\s+(\.(\w|\.)+)\s+([0-9a-fA-F]+)\s+.*$)");
    for (size_t i = 0; i < lines.size(); ++i) {
        std::smatch match;
        if (std::regex_match(lines[i], match, section_pattern)) {
            std::string section = match[2];
            std::string flags_line = lines[i + 1];
            std::vector<std::string> flags;
            std::stringstream ss(flags_line);
            std::string flag;
            while (std::getline(ss, flag, ',')) {
                flag = trim(flag);
                flags.push_back(flag);
            }

            if (std::find(flags.begin(), flags.end(), "ALLOC") != flags.end()
                && section.find("note.gnu.property") == std::string::npos) {
                uint32_t sh_flags = static_cast<uint32_t>(SHF::ALLOC);
                if (std::find(flags.begin(), flags.end(), "WRITE") != flags.end()) {
                    sh_flags |= static_cast<uint32_t>(SHF::WRITE);
                }
                if (std::find(flags.begin(), flags.end(), "EXECINSTR") != flags.end()) {
                    sh_flags |= static_cast<uint32_t>(SHF::EXEC);
                }

                bool is_nobits = std::find(flags.begin(), flags.end(), "CONTENTS") == flags.end();
                if (is_nobits) {
                    sh_flags |= static_cast<uint32_t>(SHF::NOBITS);
                }

                uint64_t section_size = std::stoul(match[4].str(), nullptr, 16);

                // 创建节头
                SectionHeader shdr;
                shdr.name = section;
                shdr.type = is_nobits ? 8 : 1; // SHT_NOBITS : SHT_PROGBITS
                shdr.flags = sh_flags;
                shdr.addr = 0;
                shdr.offset = 0;
                shdr.size = section_size;
                shdr.addralign = section == ".text" ? 16 : 8;
                shdrs.push_back(shdr);

                // 写入节的内容
                writer.begin_section(section);
                auto section_content = elf_to_fle(binary, section, is_nobits);
                for (const auto& line : section_content) {
                    writer.write_line(line);
                }
                writer.end_section();
            }
        }
    }

    writer.write_section_headers(shdrs);

    std::filesystem::path input_path(binary);
    std::filesystem::path output_path = input_path.parent_path() / (input_path.stem().string() + ".fle");
    std::cout << "output_path: " << output_path << std::endl;
    writer.write_to_file(output_path.string());

    std::filesystem::remove(binary);
}