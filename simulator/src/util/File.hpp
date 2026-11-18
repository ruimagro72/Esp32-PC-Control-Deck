// Whole-file helpers shared by every simulator object (one implementation, no copies).
#pragma once
#include <optional>
#include <string>

namespace sim {

// Whole file as a string, or nullopt when it cannot be opened.
std::optional<std::string> readFile(const std::string &path);

}  // namespace sim
