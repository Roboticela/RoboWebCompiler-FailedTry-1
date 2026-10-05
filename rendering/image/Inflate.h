#pragma once

#include <vector>

namespace Runtime {

bool InflateZlib(const unsigned char* data, int size, std::vector<unsigned char>& output);
bool InflateRaw(const unsigned char* data, int size, std::vector<unsigned char>& output);

}  // namespace Runtime
