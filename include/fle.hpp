#pragma once
#include "nlohmann/json.hpp"
#include <cstdint>
#include <fstream>
#include <map>
#include <string>
#include <vector>

using json = nlohmann::ordered_json;

// 重定位类型
enum class RelocationType {
    R_X86_64_32, // 32位绝对寻址
    R_X86_64_PC32, // 32位相对寻址
    R_X86_64_64, // 64位绝对寻址
    R_X86_64_32S, // 32位有符号绝对寻址
};

// 重定位项
struct Relocation {
    RelocationType type;
    size_t offset; // 重定位位置
    std::string symbol; // 重定位符号
    int64_t addend; // 重定位加数
};

// 符号类型
enum class SymbolType {
    LOCAL, // 局部符号 (🏷️)
    WEAK, // 弱全局符号 (📎)
    GLOBAL // 强全局符号 (📤)
};

// 符号项
struct Symbol {
    SymbolType type;
    std::string section; // 符号所在的节名
    size_t offset; // 在节内的偏移
    size_t size; // 符号大小
    std::string name; // 符号名称
};

// FLE memory structure
struct FLESection {
    std::vector<uint8_t> data; // Raw data
    std::vector<Relocation> relocs; // Relocations for this section
    size_t bss_size; // BSS section size (if this is a BSS section)
};

enum class PHF { // Program Header Flags
    X = 1, // 可执行
    W = 2, // 可写
    R = 4 // 可读
};

enum class SHF { // Section Header Flags
    ALLOC = 1, // 需要在运行时分配内存
    WRITE = 2, // 可写
    EXEC = 4, // 可执行
    NOBITS = 8, // 不占用文件空间（如BSS）
};

struct SectionHeader {
    std::string name; // 节名
    uint32_t type; // 节类型
    uint32_t flags; // 节标志
    uint64_t addr; // 虚拟地址
    uint64_t offset; // 在文件中的偏移
    uint64_t size; // 节大小
    uint32_t addralign; // 地址对齐要求
};

struct ProgramHeader {
    std::string name; // 段名
    uint64_t vaddr; // 虚拟地址（改用64位）
    uint32_t size; // 段大小
    uint32_t flags; // 权限
};

struct FLEObject {
    std::string name; // object name
    std::string type; // ".obj" or ".exe"
    std::map<std::string, FLESection> sections; // Section name -> section data
    std::vector<Symbol> symbols; // Global symbol table
    std::vector<ProgramHeader> phdrs; // Program headers (for .exe)
    std::vector<SectionHeader> shdrs; // Section headers
    size_t entry = 0; // Entry point (for .exe)
};

class FLEWriter {
public:
    void set_type(std::string_view type)
    {
        result["type"] = type;
    }

    void begin_section(std::string_view name)
    {
        current_section = name;
        current_lines.clear();
    }
    void end_section()
    {
        result[current_section] = current_lines;
        current_section.clear();
        current_lines.clear();
    }

    void write_line(std::string line)
    {
        if (current_section.empty()) {
            throw std::runtime_error("FLEWriter: begin_section must be called before write_line");
        }
        current_lines.push_back(line);
    }

    void write_to_file(const std::string& filename)
    {
        std::ofstream out(filename);
        out << result.dump(4) << std::endl;
    }

    void write_program_headers(const std::vector<ProgramHeader>& phdrs)
    {
        json phdrs_json = json::array();
        for (const auto& phdr : phdrs) {
            json phdr_json;
            phdr_json["name"] = phdr.name;
            phdr_json["vaddr"] = phdr.vaddr;
            phdr_json["size"] = phdr.size;
            phdr_json["flags"] = phdr.flags;
            phdrs_json.push_back(phdr_json);
        }
        result["phdrs"] = phdrs_json;
    }

    void write_entry(size_t entry)
    {
        result["entry"] = entry;
    }

    void write_section_headers(const std::vector<SectionHeader>& shdrs)
    {
        json shdrs_json = json::array();
        for (const auto& shdr : shdrs) {
            json shdr_json;
            shdr_json["name"] = shdr.name;
            shdr_json["type"] = shdr.type;
            shdr_json["flags"] = shdr.flags;
            shdr_json["addr"] = shdr.addr;
            shdr_json["offset"] = shdr.offset;
            shdr_json["size"] = shdr.size;
            shdr_json["addralign"] = shdr.addralign;
            shdrs_json.push_back(shdr_json);
        }
        result["shdrs"] = shdrs_json;
    }

private:
    std::string current_section;
    nlohmann::ordered_json result;
    std::vector<std::string> current_lines;
};

// Core functions that we provide
FLEObject load_fle(const std::string& filename); // Load FLE file into memory
void FLE_cc(const std::vector<std::string>& args); // Compile source files to FLE

// Functions for students to implement
/**
 * Display the contents of an FLE object file
 * @param obj The FLE object to display
 *
 * Expected output format:
 * Section .text:
 * 0000: 55 48 89 e5 48 83 ec 10  90 48 8b 45 f8 48 89 c7
 * Labels:
 *   _start: 0x0000
 * Relocations:
 *   0x0010: helper_func - 📍
 */
void FLE_objdump(const FLEObject& obj, FLEWriter& writer);

/**
 * Display the symbol table of an FLE object
 * @param obj The FLE object to analyze
 *
 * Expected output format:
 * 0000000000000000 T _start
 * 0000000000000020 t helper_func
 * 0000000000001000 D data_var
 */
void FLE_nm(const FLEObject& obj);

/**
 * Execute an FLE executable file
 * @param obj The FLE executable object
 * @throws runtime_error if the file is not executable or _start symbol is not found
 */
void FLE_exec(const FLEObject& obj);

/**
 * Link multiple FLE objects into an executable
 * @param objects Vector of FLE objects to link
 * @return A new FLE object of type ".exe"
 *
 * The linker should:
 * 1. Merge all sections with the same name
 * 2. Resolve symbols according to their binding:
 *    - Multiple strong symbols with same name: error
 *    - Strong and weak symbols: use strong
 *    - Multiple weak symbols: use first one
 * 3. Process relocations
 */
FLEObject FLE_ld(const std::vector<FLEObject>& objects);

/**
 * Read FLE object file
 * @param obj The FLE object to read
 */
void FLE_readfle(const FLEObject& obj);
