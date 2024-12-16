#include "fle.h"
#include "utils.h"
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <regex>
#include <sstream>

std::vector<std::string> splitlines(std::string_view s) {
  std::vector<std::string> lines;
  std::istringstream ss(s.data());
  std::string line;
  while (std::getline(ss, line, '\n')) {
    lines.push_back(line);
  }
  return lines;
}

json elf_to_fle(const std::string &binary, const std::string &section) {
  std::string command =
      "objcopy --dump-section " + section + "=/dev/stdout " + binary;
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
  std::regex pattern(
      R"(^([0-9a-fA-F]+)\s+(l|g)\s+(\w+)?\s+([.a-zA-Z0-9_]+)\s+([0-9a-fA-F]+)\s+(.*)$)");
  for (auto &line : splitlines(names)) {
    if (std::smatch match; std::regex_match(line, match, pattern)) {
      unsigned int offset = std::stoul(match[1].str(), nullptr, 16);
      char symb_type = match[2].str()[0];
      std::string section = match[4].str();
      std::string name = match[6].str();
      std::replace(name.begin(), name.end(), '.', '_'); // 替换点为下划线

      symbols.push_back(Symbol(symb_type, section, offset, name));
    }
  }

  std::map<int, std::pair<int, std::string>> relocations;
  bool enabled = true;
  pattern = std::regex(
      R"(^\s*([0-9a-fA-F]+)\s+([0-9a-fA-F]+)\s+(\S+)\s+([0-9a-fA-F]+)\s+(.*)$)");
  for (auto &line : splitlines(relocs)) {
    if (line.find("Relocation section") != std::string::npos) {
      enabled = line.find(".rela" + section) != std::string::npos;
    } else if (enabled) {
      if (std::smatch match; std::regex_match(line, match, pattern)) {
        int offset = std::stoi(match[1], nullptr, 16);
        std::string expr = match[5];

        std::istringstream iss(expr);
        std::vector<std::string> rs(std::istream_iterator<std::string>{iss},
                                    std::istream_iterator<std::string>());
        if (rs.empty()) {
          throw std::runtime_error("Empty relocation expression");
        }
        expr = rs[0];
        for (size_t i = 1; i < rs.size(); ++i) {
          std::stringstream ss;
          unsigned int num;
          if (std::sscanf(rs[i].c_str(), "%x", &num) > 0) {
            ss << "0x" << std::hex << num;
            expr += " " + ss.str();
          } else {
            expr += " " + rs[i];
          }
        }
        expr += " - 📍";

        if (match[3] != "R_X86_64_PC32" && match[3] != "R_X86_64_PLT32" &&
            match[3] != "R_X86_64_32") {
          throw std::runtime_error("Unsupported relocation " + match[3].str());
        }

        std::replace(expr.begin(), expr.end(), '.', '_');

        relocations[offset] = {4, "i32(" + expr + ")"};
      }
    }
  }

  std::vector<std::string> res;
  int skip = 0;
  std::vector<uint8_t> holding;

  auto do_dump = [&](std::vector<uint8_t> &holding) {
    if (!holding.empty()) {
      std::stringstream ss;
      ss << "🔢: ";
      for (auto &byte : holding) {
        ss << std::hex << std::setw(2) << std::setfill('0') << (int)byte << " ";
      }
      res.push_back(trim(ss.str()));
      holding.clear();
    }
  };

  for (size_t i = 0, len = section_data.size(); i < len; ++i) {
    for (auto &sym : symbols) {
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

void FLE_cc(const std::vector<std::string> &options) {
  std::vector<std::string> CFLAGS = {"-static", "-fPIE", "-nostdlib",
                                     "-ffreestanding",
                                     "-fno-asynchronous-unwind-tables"};
  std::vector<std::string> gcc_cmd = {"gcc", "-c"};
  gcc_cmd.insert(gcc_cmd.end(), CFLAGS.begin(), CFLAGS.end());
  gcc_cmd.insert(gcc_cmd.end(), options.begin(), options.end());

  std::string binary;
  auto it = std::find(gcc_cmd.begin(), gcc_cmd.end(), "-o");
  if (it != gcc_cmd.end() && ++it != gcc_cmd.end()) {
    binary = *it;
  } else {
    throw std::runtime_error("Output file not specified.");
  }

  std::string command = "";
  for (const auto &arg : gcc_cmd) {
    command += " " + arg;
  }
  if (std::system(command.c_str()) != 0) {
    throw std::runtime_error("gcc command failed");
  }

  command = "objdump -h " + binary;
  std::string objdump_output = exec(command.c_str());

  std::vector<std::string> lines;
  std::stringstream ss(objdump_output);
  std::string line;
  while (std::getline(ss, line, '\n')) {
    lines.push_back(line);
  }

  json res;
  res["type"] = ".obj";

  for (size_t i = 0; i < lines.size(); ++i) {
    std::regex pattern(R"(^\s*([0-9]+)\s+(\.(\w|\.)+)\s+([0-9a-fA-F]+)\s+.*$)");
    std::smatch match;
    if (std::regex_match(lines[i], match, pattern)) {
      std::string section = match[2];
      std::string flags_line = lines[i + 1];
      std::vector<std::string> flags;
      std::stringstream ss(flags_line);
      std::string flag;
      while (std::getline(ss, flag, ',')) {
        flag = trim(flag);
        flags.push_back(flag);
      }

      if (std::find(flags.begin(), flags.end(), "ALLOC") != flags.end() &&
          section.find("note.gnu.property") == std::string::npos) {
        res[section] = elf_to_fle(binary, section);
      }
    }
  }

  std::filesystem::path input_path(binary);
  std::filesystem::path output_path =
      input_path.parent_path() / (input_path.stem().string() + ".fle");
  std::ofstream outfile(output_path);
  outfile << res.dump(4) << std::endl;

  std::filesystem::remove(binary);
}