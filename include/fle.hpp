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
    R_X86_64_PLT32 // PLT重定位
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
    std::string name; // 符号名称
};

// FLE memory structure
struct FLESection {
    std::vector<uint8_t> data; // Raw data
    std::vector<Relocation> relocs; // Relocations for this section
};

struct FLEObject {
    std::string type; // ".obj" or ".exe"
    std::map<std::string, FLESection> sections; // Section name -> section data
    std::vector<Symbol> symbols; // Global symbol table
    size_t entry = 0; // Entry point (valid only for .exe)
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
