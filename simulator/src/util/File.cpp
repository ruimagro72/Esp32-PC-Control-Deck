#include "File.hpp"

#include <fstream>
#include <sstream>

namespace sim {

std::optional<std::string> readFile(const std::string &path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return std::nullopt;
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

}  // namespace sim
