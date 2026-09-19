#include "launcher/services/game_readiness.hpp"
#include "launcher/services/platform.hpp"
#include "game/build_validation.hpp"
#include <algorithm>
#include <array>

namespace witness::launcher {
namespace {
void read_memory(HANDLE process, std::uintptr_t address, void* output, SIZE_T size) {
    SIZE_T got{};
    if (!ReadProcessMemory(process, reinterpret_cast<void*>(address), output, size, &got) || got != size)
        throw win_error("Read game readiness");
}

std::size_t file_offset(const std::vector<unsigned char>& image, DWORD rva) {
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(image.data());
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(image.data() + dos->e_lfanew);
    const auto* section = IMAGE_FIRST_SECTION(nt);
    for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i)
        if (rva >= section[i].VirtualAddress && rva - section[i].VirtualAddress < section[i].SizeOfRawData)
            return section[i].PointerToRawData + rva - section[i].VirtualAddress;
    throw std::runtime_error("Readiness reference is outside the verified image.");
}
} // namespace

GameModules GameReadiness::inspect(DWORD pid, const fs::path& game_dir, const fs::path& package_root) {
    Handle game(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ, FALSE, pid));
    if (!game)
        throw win_error("Inspect game readiness");
    const auto list = modules(pid);
    std::uintptr_t base{};
    GameModules result;
    for (const auto& module : list) {
        if (!_wcsicmp(module.name.c_str(), L"witness64_d3d11.exe")) {
            if (module.size != 74457088)
                throw std::runtime_error("This game build is not supported.");
            base = module.base;
        }
        for (bool input : {false, true}) {
            const auto dll = input ? L"witness-input-probe.dll" : L"witness-render-probe.dll";
            if (!_wcsicmp(module.name.c_str(), dll)) {
                if (!same_path(module.path, package_root / L"runtime" / dll))
                    throw std::runtime_error(
                        "A different mod build is loaded. Close the game normally before using this package.");
                (input ? result.input_loaded : result.render_loaded) = true;
            }
        }
    }
    if (!base)
        return result;
    if (verified_pid_ != pid) {
        const auto exe = game_dir / L"witness64_d3d11.exe";
        if (validation::sha256(exe) != "8d672d444df6a6df7130f25a517bdab1af92731fe2138dc5b324d519561d55a5")
            throw std::runtime_error(
                "Unsupported Witness executable. This package supports the tested Steam Windows x64 build only.");
        const auto image = read_file(exe);
        verified_image_.assign(image.begin(), image.end());
        verified_pid_ = pid;
    }
    for (const auto& site :
         std::array<std::pair<DWORD, SIZE_T>, 3>{{{0x37A436, 7}, {0x37A4F9, 7}, {0x37A3F6, 6}}}) {
        std::array<unsigned char, 7> live{};
        read_memory(game.get(), base + site.first, live.data(), site.second);
        const auto offset = file_offset(verified_image_, site.first);
        if (offset + site.second > verified_image_.size() ||
            !std::equal(live.begin(), live.begin() + site.second, verified_image_.begin() + offset))
            throw std::runtime_error("Game readiness code differs from the supported build.");
    }
    std::array<std::uintptr_t, 2> pointers{};
    read_memory(game.get(), base + 0x469AB38, pointers.data(), sizeof(pointers));
    result.vr_ready = pointers[0] && pointers[1];
    return result;
}

} // namespace witness::launcher
