#pragma once
#include <nlohmann/json.hpp>
#include <string>
#include <string_view>

using json = nlohmann::ordered_json;

// 文件路径相关函数
std::string get_basename(std::string_view path);                  // 获取文件名
std::string get_filename_without_extension(std::string_view path);// 获取不带扩展名的文件名

// 字符串处理函数
std::string trim(std::string_view s);                            // 修剪字符串两端的空白字符
std::string trim(std::string_view s, std::string_view chars);    // 修剪字符串两端的指定字符

// FLE 文件操作函数
json load_fle(const std::string &file);                          // 加载并解析 FLE 文件

// 系统相关函数
std::string exec(std::string_view cmd);