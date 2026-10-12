#include "render/hands_native.hpp"
#include "render/hands_controller.hpp"
#include "render/hands_depth.hpp"
#include "common/win_util.hpp"
#include "game/build_validation.hpp"
#include <d3d11.h>
#include <atomic>

namespace witness::hands {
namespace {
template <class T> bool read(std::uintptr_t address, T& value) noexcept {
    SIZE_T count{};
    return address &&
           ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(address), &value,
                             sizeof(value), &count) &&
           count == sizeof(value);
}

bool image_code(void* address, MEMORY_BASIC_INFORMATION& info) noexcept {
    return VirtualQuery(address, &info, sizeof(info)) && info.State == MEM_COMMIT && info.Type == MEM_IMAGE &&
           !(info.Protect & PAGE_GUARD) &&
           (info.Protect &
            (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY));
}

void verify_vr_method(void* address) {
    MEMORY_BASIC_INFORMATION info{};
    if (!image_code(address, info))
        throw std::runtime_error("Invalid hand tracking method");
    const auto owner = std::filesystem::path(witness::module_path(static_cast<HMODULE>(info.AllocationBase)));
    if (_wcsicmp(owner.filename().c_str(), L"vrclient_x64.dll") &&
        _wcsicmp(owner.filename().c_str(), L"openvr_api.dll"))
        throw std::runtime_error("Unexpected hand tracking method owner");
}

struct EyeMatrix34 {
    float m[3][4]{};
};

// MSVC's x64 virtual aggregate-return ABI: this, explicit return storage, args.
// Do not express these as C++ functions returning Mat4/Matrix34 under MinGW.
using Projection = Mat4* (*)(void*, Mat4*, int, float, float, int);
using EyeTransform = EyeMatrix34* (*)(void*, EyeMatrix34*, int);
using Role = std::uint32_t (*)(void*, int);
using Connected = bool (*)(void*, std::uint32_t);
using Captured = bool (*)(void*);
using LastPoses = int (*)(void*, input::TrackedPose*, std::uint32_t, input::TrackedPose*, std::uint32_t);
using GenericInterface = void* (*)(const char*, int*);

ID3D11Texture2D* query_texture(std::uintptr_t resource) noexcept {
    std::uintptr_t texture{}, table{};
    void* query{};
    MEMORY_BASIC_INFORMATION info{};
    if (!resource || !read(resource + 0x40, texture) || !read(texture, table) || !read(table, query) ||
        !image_code(query, info))
        return nullptr;
    constexpr GUID texture_iid{0x6f15aaf2, 0xd208, 0x4e89, {0x9a, 0xb4, 0x48, 0x95, 0x35, 0xd3, 0x4f, 0x9c}};
    ID3D11Texture2D* result{};
    if (FAILED(reinterpret_cast<IUnknown*>(texture)->QueryInterface(texture_iid,
                                                                    reinterpret_cast<void**>(&result))))
        return nullptr;
    return result;
}
} // namespace

struct NativeHands::State {
    std::uintptr_t base{};
    void* system{};
    void* compositor{};
    void* models{};
    void** system_table{};
    void** compositor_table{};
    void** models_table{};
    std::array<void*, 40> methods{};
    void* poses_method{};
    void* component_method{};
    std::atomic<DWORD> owner{0};
    Renderer renderer;
    std::array<Frame, 2> frames{};
    std::array<float, 2> grip{};
    std::array<std::uint32_t, 2> grip_devices{input::invalid_device, input::invalid_device};
    ULONGLONG grip_tick{};
    ULONGLONG sampled{};
    bool pair_ready{};
    bool reset_renderer{};
    bool depth_verified{};
    bool world_occlusion{};
    std::atomic<bool> depth_active{false};
    std::atomic<std::uint64_t> depth_draws{0};
    int current_eye{-1};

    bool render_thread() noexcept {
        DWORD expected = 0;
        owner.compare_exchange_strong(expected, GetCurrentThreadId());
        return owner.load() == GetCurrentThreadId();
    }

    bool live() const noexcept {
        void* object{};
        void** table{};
        void* method{};
        if (!read(base + 0x469AB38, object) || object != system ||
            !read(reinterpret_cast<std::uintptr_t>(object), table) || table != system_table ||
            !read(base + 0x469AB40, object) || object != compositor ||
            !read(reinterpret_cast<std::uintptr_t>(object), table) || table != compositor_table ||
            !read(reinterpret_cast<std::uintptr_t>(table) + 3 * sizeof(void*), method) ||
            method != poses_method || !read(reinterpret_cast<std::uintptr_t>(models), table) ||
            table != models_table ||
            !read(reinterpret_cast<std::uintptr_t>(table) + 13 * sizeof(void*), method) ||
            method != component_method)
            return false;
        for (const auto slot :
             {1u, 4u, 26u, input::role_slot, input::connected_slot, input::state_slot, input::focus_slot}) {
            if (!read(reinterpret_cast<std::uintptr_t>(system_table) + slot * sizeof(void*), method) ||
                method != methods[slot])
                return false;
        }
        return true;
    }

    bool visible() const noexcept {
        DWORD foreground{};
        GetWindowThreadProcessId(GetForegroundWindow(), &foreground);
        float fade{};
        return foreground == GetCurrentProcessId() && read(base + 0x61E994, fade) && fade == 0.f &&
               !reinterpret_cast<Captured>(methods[input::focus_slot])(system) &&
               reinterpret_cast<Connected>(methods[input::connected_slot])(system, 0);
    }

    void capture() noexcept {
        frames = {};
        pair_ready = false;
        if (!live() || !visible()) {
            grip_tick = 0;
            return;
        }
        std::array<input::TrackedPose, 16> poses{};
        if (reinterpret_cast<LastPoses>(poses_method)(compositor, poses.data(), 16, nullptr, 0) != 0 ||
            !input::valid_pose(poses[0])) {
            grip_tick = 0;
            return;
        }
        std::array<std::uint32_t, 2> devices{};
        for (int hand = 0; hand < 2; ++hand)
            devices[hand] = reinterpret_cast<Role>(methods[input::role_slot])(system, hand + 1);
        if (devices[0] == devices[1]) {
            grip_tick = 0;
            return;
        }
        std::array<ControllerHand, 2> hands{};
        const auto now = GetTickCount64();
        const float step = grip_tick && now >= grip_tick && now - grip_tick <= 100
                               ? static_cast<float>(now - grip_tick) / 120.f
                               : 1.f;
        for (int hand = 0; hand < 2; ++hand) {
            const auto device = devices[hand];
            if (device > 0 && device < poses.size() && input::valid_pose(poses[device]) &&
                reinterpret_cast<Connected>(methods[input::connected_slot])(system, device))
                hands[hand] = sample_hand(system, models, device, hand == 0,
                                          reinterpret_cast<StringProperty>(methods[26]),
                                          reinterpret_cast<ControllerStateFn>(methods[input::state_slot]),
                                          reinterpret_cast<ComponentStateFn>(component_method));
            if (!hands[hand].valid) {
                grip[hand] = 0;
                grip_devices[hand] = input::invalid_device;
                continue;
            }
            const float target = hands[hand].grip_pressed ? 1.f : 0.f;
            if (grip_devices[hand] != device)
                grip[hand] = target;
            else
                grip[hand] += std::clamp(target - grip[hand], -step, step);
            grip_devices[hand] = device;
        }
        grip_tick = now;
        for (int eye = 0; eye < 2; ++eye) {
            Mat4 projection{};
            EyeMatrix34 eye_to_head{};
            if (reinterpret_cast<Projection>(methods[1])(system, &projection, eye, .02f, 1000.f, 0) !=
                    &projection ||
                reinterpret_cast<EyeTransform>(methods[4])(system, &eye_to_head, eye) != &eye_to_head)
                return;
            for (int hand = 0; hand < 2; ++hand) {
                const auto device = devices[hand];
                if (hands[hand].valid) {
                    frames[eye].hands[hand] = make_draw(poses[0], poses[device], eye_to_head.m, projection,
                                                        hands[hand].device_from_glove);
                    frames[eye].grip[hand] = grip[hand];
                }
            }
        }
        sampled = GetTickCount64();
        pair_ready = true;
    }

    // This is a candidate world-depth route established from static native
    // ordering. Unsupported resources/projections leave ordinary hands visible.
    SceneDepth scene_depth(const void* view) const noexcept {
        SceneDepth result{};
        float projection[16]{};
        std::uint8_t reversed{}, half_size{};
        float scale{};
        const auto address = reinterpret_cast<std::uintptr_t>(view);
        std::uintptr_t wrapper{}, resource{};
        if (!world_occlusion || !depth_verified || !view || !read(address + 0xAC, projection) ||
            !read(address + 0x16C, reversed) || reversed > 1 ||
            !depth_projection(projection, reversed != 0, result.a, result.b) ||
            !read(base + 0x630C20, wrapper) || !wrapper || !read(wrapper + 8, resource) ||
            !read(base + 0x620478, scale) || !std::isfinite(scale) || scale <= 0.f ||
            !read(base + 0x621E6C, half_size))
            return {};
        result.texture = query_texture(resource);
        if (!result.texture)
            return {};
        D3D11_TEXTURE2D_DESC desc{};
        result.texture->GetDesc(&desc);
        result.width = desc.Width;
        result.height = desc.Height;
        // The native binder sets the full texture viewport; the render loop
        // overrides it only for reduced-resolution rendering.
        if (scale < 1.f) {
            if (!read(base + 0x630C50, result.width) || !read(base + 0x630C54, result.height)) {
                result.texture->Release();
                return {};
            }
            if (half_size) {
                result.width /= 2;
                result.height /= 2;
            }
        }
        if (!result.width || !result.height || result.width > desc.Width || result.height > desc.Height) {
            result.texture->Release();
            return {};
        }
        return result;
    }

    DrawResult draw(int eye, const void* view) noexcept {
        depth_active.store(false, std::memory_order_relaxed);
        if (!render_thread() || eye < 0 || eye > 1 || current_eye != eye || !pair_ready ||
            GetTickCount64() - sampled > 100 || !live() || !visible())
            return DrawResult::skipped;
        auto frame = frames[eye];
        if (!frame.hands[0].valid && !frame.hands[1].valid)
            return DrawResult::skipped;
        // Submit resolves/copies wrapper[0] into wrapper[1]. Draw into its source
        // before native submit, otherwise the resolve would erase our geometry.
        std::uintptr_t target{}, source{};
        if (!read(base + 0x469AB58 + static_cast<unsigned>(eye) * 24, target) || !read(target, source))
            return DrawResult::failed;
        // QueryInterface verifies the live native resource's actual COM type and
        // holds it across this draw. Native ownership remains unchanged.
        auto* color = query_texture(source);
        if (!color)
            return DrawResult::unsupported;
        frame.depth = scene_depth(view);
        const auto drawn = renderer.draw(color, frame);
        if (frame.depth.texture)
            frame.depth.texture->Release();
        color->Release();
        const bool used = drawn == DrawResult::drawn && renderer.used_world_depth();
        depth_active.store(used, std::memory_order_relaxed);
        if (used)
            depth_draws.fetch_add(1, std::memory_order_relaxed);
        return drawn;
    }
};

NativeHands::NativeHands(std::uintptr_t image_base) : state_(std::make_unique<State>()) {
    // The caller has already checked the complete executable hash and image size.
    using witness::validation::match;
    match(image_base, 0x37A7CA, "488b0d6703320433db488b01488d55d70f28d9895c2428498bf9f30f11542420ff5008");
    match(image_base, 0x37A8B0, "448bc1488b0d7e023204488bda488d55e0440f2948b8488b01ff5020");
    match(
        image_base, 0x37A26D,
        "488d05e4083204488d147f488b0cd0488d1cd033d2e8b9cefdff488b03488b0daf083204488b50084c8d4424304533c9488b4240");
    match(image_base, 0x357140, "40564883ec50488bf1488b49084885c90f847d010000448b461048895c2468488b1e");
    match(image_base, 0x37A436, "48893dfb06320485db7438");
    match(image_base, 0x37A4F9, "48890540063204");
    auto& s = *state_;
    s.base = image_base;
    try {
        match(image_base, 0x1C8E9D, "488b157c7d4600488b0d6d7d46008805106d4600e85ae81800");
        match(
            image_base, 0x357710,
            "48895c241048896c2418488974242057415641574883ec2033db488bea4c8bf18bfb8bf348895c2440448bfb4885c97414488b014885c0740c488bf8488b405048894424404885d27410488b42084885c074074c8b7850488bf0");
        match(
            image_base, 0x2C88D5,
            "488d93ac0000004c8d432c488d8c24800000000f10100f1058100f1040200f1048300f11120f115a100f1142200f114a30");
        match(image_base, 0x2C894C,
              "4088bb6c0100004084ff488bbc24f0000000c783700100000000803f740ac7837001000000000000");
        match(
            image_base, 0x1C8EC9,
            "f30f1005a77545000f2fc6733d33c9380d8e8f4500741e8b056e7d4600992bc2d1f8448bc88b055c7d4600992bc2d1f8448bc0eb0e448b0d4f7d4600448b05447d460033d2");
        s.depth_verified = true;
    } catch (const std::exception&) {
        // A depth-route mismatch disables only the optional experiment.
    }
    if (!read(image_base + 0x469AB38, s.system) || !s.system ||
        !read(reinterpret_cast<std::uintptr_t>(s.system), s.system_table) ||
        !read(reinterpret_cast<std::uintptr_t>(s.system_table), s.methods) ||
        !read(image_base + 0x469AB40, s.compositor) || !s.compositor ||
        !read(reinterpret_cast<std::uintptr_t>(s.compositor), s.compositor_table) ||
        !read(reinterpret_cast<std::uintptr_t>(s.compositor_table) + 3 * sizeof(void*), s.poses_method))
        throw std::runtime_error("No readable native hand tracking interfaces");
    for (const auto slot :
         {1u, 4u, 26u, input::role_slot, input::connected_slot, input::state_slot, input::focus_slot})
        verify_vr_method(s.methods[slot]);
    verify_vr_method(s.poses_method);
    // Borrow the already initialized runtime through the shipped API. No new
    // VR session, action manifest, controller binding, or runtime DLL is loaded.
    const auto api = GetModuleHandleW(L"openvr_api.dll");
    const auto factory =
        api ? reinterpret_cast<void*>(GetProcAddress(api, "VR_GetGenericInterface")) : nullptr;
    MEMORY_BASIC_INFORMATION info{};
    if (!factory || !image_code(factory, info) || info.AllocationBase != api)
        throw std::runtime_error("No native render model interface factory");
    int error = -1;
    s.models = reinterpret_cast<GenericInterface>(factory)("IVRRenderModels_005", &error);
    if (error || !s.models || !read(reinterpret_cast<std::uintptr_t>(s.models), s.models_table) ||
        !read(reinterpret_cast<std::uintptr_t>(s.models_table) + 13 * sizeof(void*), s.component_method))
        throw std::runtime_error("No readable native controller palm interface");
    verify_vr_method(s.component_method);
}

NativeHands::~NativeHands() = default;

void NativeHands::new_session(bool world_occlusion) noexcept {
    state_->pair_ready = false;
    state_->current_eye = -1;
    state_->frames = {};
    state_->grip = {};
    state_->grip_devices = {input::invalid_device, input::invalid_device};
    state_->grip_tick = 0;
    state_->reset_renderer = true;
    state_->world_occlusion = world_occlusion;
    state_->depth_active.store(false);
    state_->depth_draws.store(0);
}

void NativeHands::begin_eye(int eye) noexcept {
    auto& s = *state_;
    if (!s.render_thread())
        return;
    s.depth_active.store(false, std::memory_order_relaxed);
    if (s.reset_renderer) {
        s.renderer.reset();
        s.reset_renderer = false;
    }
    const int previous = s.current_eye;
    s.current_eye = eye;
    if (eye == 0)
        s.capture();
    else if (eye != 1 || previous != 0)
        s.pair_ready = false;
}

DrawResult NativeHands::draw(int eye, const void* view) noexcept {
    return state_->draw(eye, view);
}

bool NativeHands::occlusion_active() const noexcept {
    return state_->depth_active.load(std::memory_order_relaxed);
}

std::uint64_t NativeHands::occlusion_draws() const noexcept {
    return state_->depth_draws.load(std::memory_order_relaxed);
}
} // namespace witness::hands
