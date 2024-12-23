#include "fle.hpp"
#include "string_utils.hpp"
#include <fstream>
#include <iostream>
#include <regex>
#include <string>
#include <vector>

using namespace std::string_literals;

FLEObject load_fle(const std::string& file)
{
    std::ifstream infile(file);
    std::string content((std::istreambuf_iterator<char>(infile)),
        std::istreambuf_iterator<char>());

    if (content.substr(0, 2) == "#!") {
        content = content.substr(content.find('\n') + 1);
    }

    json j = json::parse(content);
    FLEObject obj;
    obj.name = get_basename(file);
    obj.type = j["type"].get<std::string>();

    // 如果是可执行文件，读取入口点和程序头
    if (obj.type == ".exe") {
        if (j.contains("entry")) {
            obj.entry = j["entry"].get<size_t>();
        }
        if (j.contains("phdrs")) {
            for (const auto& phdr_json : j["phdrs"]) {
                ProgramHeader phdr;
                phdr.name = phdr_json["name"].get<std::string>();
                phdr.vaddr = phdr_json["vaddr"].get<uint64_t>();
                phdr.size = phdr_json["size"].get<uint32_t>();
                phdr.flags = phdr_json["flags"].get<uint32_t>();
                obj.phdrs.push_back(phdr);
            }
        }
        if (j.contains("shdrs")) {
            for (const auto& shdr_json : j["shdrs"]) {
                SectionHeader shdr;
                shdr.name = shdr_json["name"].get<std::string>();
                shdr.type = shdr_json["type"].get<uint32_t>();
                shdr.flags = shdr_json["flags"].get<uint32_t>();
                shdr.addr = shdr_json["addr"].get<uint64_t>();
                shdr.offset = shdr_json["offset"].get<uint64_t>();
                shdr.size = shdr_json["size"].get<uint64_t>();
                shdr.addralign = shdr_json["addralign"].get<uint32_t>();
                obj.shdrs.push_back(shdr);
            }
        }
    }

    // 处理每个段
    for (auto& [key, value] : j.items()) {
        if (key == "type" || key == "entry" || key == "phdrs" || key == "shdrs")
            continue;

        FLESection section;
        size_t bss_size = 0;

        // 处理段的内容
        for (const auto& line : value) {
            std::string line_str = line.get<std::string>();

            size_t colon_pos = line_str.find(':');
            std::string prefix = line_str.substr(0, colon_pos);
            std::string content = line_str.substr(colon_pos + 1);

            if (prefix == "🔢") {
                // 处理数据
                std::stringstream ss(content);
                uint32_t byte;
                while (ss >> std::hex >> byte) {
                    section.data.push_back(static_cast<uint8_t>(byte));
                }
            } else if (prefix == "🏷️") {
                // 处理局部符号
                std::string name;
                size_t size;
                std::istringstream ss(content);
                ss >> name >> size;
                Symbol sym {
                    SymbolType::LOCAL,
                    std::string(key),
                    section.data.size(),
                    size,
                    trim(name)
                };
                std::cerr << "Loading symbol: " << sym.name << " in section " << sym.section << " at offset " << sym.offset << std::endl;
                obj.symbols.push_back(sym);
                bss_size += size;
            } else if (prefix == "📎") {
                // 处理弱全局符号
                std::string name;
                size_t size;
                std::istringstream ss(content);
                ss >> name >> size;
                Symbol sym {
                    SymbolType::WEAK,
                    std::string(key),
                    section.data.size(),
                    size,
                    trim(name)
                };
                obj.symbols.push_back(sym);
                bss_size += size;
            } else if (prefix == "📤") {
                // 处理强全局符号
                std::string name;
                size_t size;
                std::istringstream ss(content);
                ss >> name >> size;
                Symbol sym {
                    SymbolType::GLOBAL,
                    std::string(key),
                    section.data.size(),
                    size,
                    trim(name)
                };
                obj.symbols.push_back(sym);
                bss_size += size;
            } else if (prefix == "❓") {
                // 处理重定位
                std::string reloc_str = trim(content);
                // e.g. rel(n - 4)
                std::regex reloc_pattern(R"(\.(rel|abs64|abs|abs32s)\(([\w.]+)\s*[-+]\s*(\d+)\))");
                std::smatch match;

                if (!std::regex_match(reloc_str, match, reloc_pattern)) {
                    throw std::runtime_error("Invalid relocation: " + reloc_str);
                }

                RelocationType type;
                if (match[1].str() == "rel") {
                    type = RelocationType::R_X86_64_PC32;
                } else if (match[1].str() == "abs64") {
                    type = RelocationType::R_X86_64_64;
                } else if (match[1].str() == "abs") {
                    type = RelocationType::R_X86_64_32;
                } else if (match[1].str() == "abs32s") {
                    type = RelocationType::R_X86_64_32S;
                } else {
                    throw std::runtime_error("Invalid relocation type: " + match[1].str());
                }

                Relocation reloc {
                    type,
                    section.data.size(),
                    match[2].str(),
                    std::stoi(match[3].str())
                };

                section.relocs.push_back(reloc);

                // 根据重定位类型预留空间
                size_t size = (type == RelocationType::R_X86_64_64) ? 8 : 4;
                for (size_t i = 0; i < size; ++i) {
                    section.data.push_back(0);
                }
            }
        }

        if (key == ".bss") {
            section.bss_size = bss_size;
        } else {
            section.bss_size = 0;
        }

        obj.sections[key] = section;
    }

    return obj;
}

int main(int argc, char* argv[])
{
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <command> [args...]\n"
                  << "Commands:\n"
                  << "  objdump <input.fle>              Display contents of FLE file\n"
                  << "  nm <input.fle>                   Display symbol table\n"
                  << "  ld [-o output.fle] input1.fle... Link FLE files\n"
                  << "  exec <input.fle>                 Execute FLE file\n"
                  << "  cc [-o output.fle] input.c...    Compile C files\n";
        return 1;
    }

    std::string tool = "FLE_"s + get_basename(argv[0]);
    std::vector<std::string> args(argv + 1, argv + argc);

    try {
        if (tool == "FLE_objdump") {
            if (args.size() != 1) {
                throw std::runtime_error("Usage: objdump <input.fle>");
            }
            FLEWriter writer;
            FLE_objdump(load_fle(args[0]), writer);
            writer.write_to_file(args[0] + ".objdump");
        } else if (tool == "FLE_nm") {
            if (args.size() != 1) {
                throw std::runtime_error("Usage: nm <input.fle>");
            }
            FLE_nm(load_fle(args[0]));
        } else if (tool == "FLE_exec") {
            if (args.size() != 1) {
                throw std::runtime_error("Usage: exec <input.fle>");
            }
            FLE_exec(load_fle(args[0]));
        } else if (tool == "FLE_ld") {
            std::string outfile = "a.out";
            std::vector<std::string> input_files;

            for (size_t i = 0; i < args.size(); ++i) {
                if (args[i] == "-o" && i + 1 < args.size()) {
                    outfile = args[++i];
                } else {
                    input_files.push_back(args[i]);
                }
            }

            if (input_files.empty()) {
                throw std::runtime_error("No input files specified");
            }

            std::vector<FLEObject> objects;
            for (const auto& file : input_files) {
                objects.push_back(load_fle(file));
            }

            for (const auto& obj : objects) {
                std::cerr << "Object type: " << obj.type << std::endl;
                std::cerr << "Symbols:" << std::endl;
                for (const auto& sym : obj.symbols) {
                    std::cerr << "  " << sym.name << " in " << sym.section << std::endl;
                }
            }

            // 链接
            FLEObject linked_obj = FLE_ld(objects);

            // 写入文件
            FLEWriter writer;
            FLE_objdump(linked_obj, writer);
            writer.write_to_file(outfile);
        } else if (tool == "FLE_cc") {
            FLE_cc(args);
        } else if (tool == "FLE_readfle") {
            if (args.size() != 1) {
                throw std::runtime_error("Usage: readfle <input.fle>");
            }
            FLE_readfle(load_fle(args[0]));
        } else {
            std::cerr << "Unknown tool: " << tool << std::endl;
            return 1;
        }
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}