#include "fle.hpp"
#include "string_utils.hpp"
#include <filesystem>
#include <fstream>
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

json elf_to_fle(const std::string& binary, const std::string& section)
{
    std::string command = "objcopy --dump-section " + section + "=/dev/stdout " + binary;
    std::string section_data = exec(command.c_str());

    command = "readelf -r " + binary;
    std::string relocs = exec(command.c_str());

    command = "objdump -t " + binary;
    std::string names = exec(command.c_str());

    struct Symbol {
        char symb_type;
        std::string section;
        unsigned int offset;
        std::string name;
    };

    std::vector<Symbol> symbols;
    // e.g.
    // Addr            Flag Bind Sect      Size        Name
    // 0000000000000000 g     F .text  000000000000001d foo
    std::regex symbol_pattern(
        R"(^([0-9a-fA-F]+)\s+(l|g)\s+(\w+)?\s+([.a-zA-Z0-9_]+)\s+([0-9a-fA-F]+)\s+(.*)$)");
    for (auto& line : splitlines(names)) {
        if (std::smatch match; std::regex_match(line, match, symbol_pattern)) {
            unsigned int offset = std::stoul(match[1].str(), nullptr, 16);
            char symb_type = match[2].str()[0];
            std::string section = match[4].str();
            std::string name = match[6].str();

            symbols.push_back(Symbol(symb_type, section, offset, name));
        }
    }

    std::map<int, std::pair<int, std::string>> relocations;
    bool enabled = true;
    // e.g.
    //   Offset          Info           Type           Sym. Value    Sym. Name + Addend
    // 000000000059  001100000001 R_X86_64_64       0000000000000000 n + 0
    std::regex reloc_pattern(
        R"(^\s*([0-9a-fA-F]+)\s+([0-9a-fA-F]+)\s+(\S+)\s+([0-9a-fA-F]+)\s+(.*)$)");
    for (auto& line : splitlines(relocs)) {
        if (line.find("Relocation section") != std::string::npos) {
            enabled = line.find(".rela" + section) != std::string::npos;
        } else if (enabled) {
            if (std::smatch match; std::regex_match(line, match, reloc_pattern)) {
                int offset = std::stoi(match[1], nullptr, 16);
                std::string symbol = match[5];

                // 清理符号名（只去掉@PLT等后缀）
                size_t at_pos = symbol.find('@');
                if (at_pos != std::string::npos) {
                    symbol = symbol.substr(0, at_pos);
                }

                // 确定重定位类型和格式
                std::string reloc_type = match[3];
                std::string reloc_format;
                if (reloc_type == "R_X86_64_PC32" || reloc_type == "R_X86_64_PLT32") {
                    reloc_format = ".rel"; // 相对重定位
                } else if (reloc_type == "R_X86_64_32") {
                    reloc_format = ".abs"; // 绝对重定位
                } else {
                    throw std::runtime_error("Unsupported relocation type: " + reloc_type);
                }

                // 生成重定位表达式
                std::stringstream ss;
                ss << reloc_format << "(" << symbol << ")";
                relocations[offset] = { 4, ss.str() };
            }
        }
    }

    std::vector<std::string> res;
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
            if (sym.section == section && sym.offset == i) {
                do_dump(holding);
                if (sym.symb_type == 'l') {
                    res.push_back("🏷️: " + sym.name);
                } else {
                    res.push_back("📤: " + sym.name);
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
    "-fPIE"sv,
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

    json res;
    res["type"] = ".obj";

    // e.g.
    // Idx Name          Size      VMA               LMA               File off  Algn
    // 0 .text         0000001d  0000000000000000  0000000000000000  00000040  2**0
    //                  CONTENTS, ALLOC, LOAD, RELOC, READONLY, CODE
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
                res[section] = elf_to_fle(binary, section);
            }
        }
    }

    std::filesystem::path input_path(binary);
    std::filesystem::path output_path = input_path.parent_path() / (input_path.stem().string() + ".fle");
    std::cout << "output_path: " << output_path << std::endl;
    std::ofstream outfile(output_path);
    outfile << res.dump(4) << std::endl;

    std::filesystem::remove(binary);
}