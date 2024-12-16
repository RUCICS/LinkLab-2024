#pragma once
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

// 使用 ordered_json 以保持 JSON 对象的键值对顺序
using json = nlohmann::ordered_json;

// FLE 工具函数声明
void FLE_objdump(const std::vector<std::string> &files);  // 查看目标文件内容
void FLE_readfle(const std::vector<std::string> &files);  // 读取 FLE 文件
void FLE_nm(const std::vector<std::string> &files);       // 查看符号表
void FLE_exec(const std::vector<std::string> &files);     // 执行 FLE 文件
void FLE_ld(const std::vector<std::string> &files);       // 链接器
void FLE_cc(const std::vector<std::string> &files);       // 编译器
