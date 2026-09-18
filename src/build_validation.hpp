#pragma once

#include <windows.h>
#include <bcrypt.h>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace witness::validation {
// Return the supported-size executable's SHA-256 digest; throw on read/hash failure.
inline std::string sha256(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) throw std::runtime_error("Cannot read executable for hash verification");
    std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(file)), {});
    if (bytes.size() != 6864896) throw std::runtime_error("Unsupported executable size");
    BCRYPT_ALG_HANDLE algorithm{};
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0)
        throw std::runtime_error("Cannot open SHA-256 provider");
    unsigned char digest[32]{};
    const auto status = BCryptHash(algorithm, nullptr, 0, bytes.data(), static_cast<ULONG>(bytes.size()), digest, sizeof(digest));
    BCryptCloseAlgorithmProvider(algorithm, 0);
    if (status < 0) throw std::runtime_error("Cannot hash executable");
    std::ostringstream out;
    for (const auto byte : digest) out << std::hex << std::setw(2) << std::setfill('0') << static_cast<unsigned>(byte);
    return out.str();
}

// Require exact live bytes at an image-relative address before installing hooks.
inline void match(std::uintptr_t base, std::uintptr_t rva, const char* hex) {
    std::vector<unsigned char> expected;
    for (std::size_t i = 0; hex[i]; i += 2)
        expected.push_back(static_cast<unsigned char>(std::stoul(std::string(hex + i, 2), nullptr, 16)));
    std::vector<unsigned char> actual(expected.size());
    SIZE_T received{};
    if (!ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(base + rva), actual.data(), actual.size(), &received) ||
        received != actual.size() || actual != expected)
        throw std::runtime_error("Live code differs from the supported build; no new hooks enabled");
}
} // namespace witness::validation
