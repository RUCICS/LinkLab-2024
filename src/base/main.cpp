#include "fle.hpp"
#include "utils.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

using namespace std::string_literals;

std::string get_basename(std::string_view path)
{
    return std::filesystem::path(path).filename().string();
}

std::string get_filename_without_extension(std::string_view path)
{
    return std::filesystem::path(path).stem().string();
}

std::string trim(std::string_view s)
{
    s.remove_prefix(std::min(s.find_first_not_of(" \t"), s.size()));
    s.remove_suffix(s.size() - s.find_last_not_of(" \t") - 1);
    return std::string(s);
}

std::string trim(std::string_view s, std::string_view chars)
{
    s.remove_prefix(std::min(s.find_first_not_of(chars), s.size()));
    s.remove_suffix(s.size() - s.find_last_not_of(chars) - 1);
    return std::string(s);
}

std::string exec(std::string_view cmd)
{
    auto final_cmd = cmd.data() + " 2>/dev/null"s;
    FILE* pipe = popen(final_cmd.c_str(), "r");
    if (!pipe)
        throw std::runtime_error("popen() failed!");

    std::string result;
    char buffer[128];
    size_t bytes_read;

    while ((bytes_read = fread(buffer, 1, sizeof(buffer), pipe)) != 0) {
        result.append(buffer, bytes_read);
    }

    pclose(pipe);
    return result;
}

json load_fle(const std::string& file)
{
    std::ifstream infile(file);
    std::string content((std::istreambuf_iterator<char>(infile)),
        std::istreambuf_iterator<char>());

    if (content.substr(0, 2) == "#!") {
        content = content.substr(content.find('\n') + 1);
    }

    return json::parse(content);
}

int main(int argc, char* argv[])
{
    std::string tool = "FLE_"s + get_basename(argv[0]);
    std::vector<std::string> args(argv + 1, argv + argc);

    if (tool == "FLE_objdump") {
        FLE_objdump(args);
    } else if (tool == "FLE_readfle") {
        FLE_readfle(args);
    } else if (tool == "FLE_nm") {
        FLE_nm(args);
    } else if (tool == "FLE_exec") {
        FLE_exec(args);
    } else if (tool == "FLE_ld") {
        FLE_ld(args);
    } else if (tool == "FLE_cc") {
        FLE_cc(args);
    } else {
        std::cerr << tool << " is not implemented." << std::endl;
        return 1;
    }

    return 0;
}