#include "fle.hpp"
#include <iomanip>
#include <iostream>

void FLE_nm(const FLEObject& obj)
{
    // 遍历所有符号并按格式输出
    for (const auto& symbol : obj.symbols) {
        // 如果是未定义符号，不显示地址，只显示空格
        if (symbol.section.empty()) {
            std::cout << "                 U " << symbol.name << std::endl;
            continue;
        }

        // 显示符号地址
        std::cout << std::hex << std::setfill('0') << std::setw(16) << symbol.offset << " ";

        // 确定符号类型
        char type;

        // 首先检查是否为弱符号
        if (symbol.type == SymbolType::WEAK) {
            // 如果在代码段，则为弱函数符号 'W'
            if (symbol.section == ".text") {
                type = 'W';
            }
            // 如果在数据段或BSS段，则为弱对象符号 'V'
            else {
                type = 'V';
            }
        }
        // 其他情况按段类型处理
        else if (symbol.section == ".text") {
            type = (symbol.type == SymbolType::GLOBAL) ? 'T' : 't';
        } else if (symbol.section == ".data") {
            type = (symbol.type == SymbolType::GLOBAL) ? 'D' : 'd';
        } else if (symbol.section == ".bss") {
            type = (symbol.type == SymbolType::GLOBAL) ? 'B' : 'b';
        } else if (symbol.section == ".rodata") {
            type = (symbol.type == SymbolType::GLOBAL) ? 'R' : 'r';
        } else {
            type = '?';
        }

        std::cout << type << " " << symbol.name << std::endl;
    }
}