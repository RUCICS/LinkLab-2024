#include "fle.hpp"
#include <iomanip>
#include <iostream>

void FLE_nm(const FLEObject& obj)
{
    // 遍历所有符号并按格式输出
    for (const auto& symbol : obj.symbols) {
        // 计算符号的值（偏移量）
        std::cout << std::hex << std::setfill('0') << std::setw(16) << symbol.offset << " ";

        // 输出符号类型
        char type;
        switch (symbol.type) {
        case SymbolType::LOCAL:
            type = 't'; // 局部符号用小写
            break;
        case SymbolType::GLOBAL:
            type = 'T'; // 全局符号用大写
            break;
        case SymbolType::WEAK:
            type = 'w'; // 弱符号用小写w
            break;
        default:
            type = '?';
            break;
        }

        // 如果符号在.data节中，使用D/d而不是T/t
        if (symbol.section == ".data") {
            type = (type == 'T') ? 'D' : 'd';
        }

        std::cout << type << " " << symbol.name << std::endl;
    }
}