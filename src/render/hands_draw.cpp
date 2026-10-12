#include "hands_draw.hpp"

#include "hands_skin.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <d3d11_1.h>
#include <d3dcompiler.h>
#include <utility>

namespace witness::hands {
namespace {

template <typename T> class Com {
public:
    Com() = default;

    ~Com() { reset(); }

    Com(const Com&) = delete;
    Com& operator=(const Com&) = delete;

    Com(Com&& other) noexcept : value_(std::exchange(other.value_, nullptr)) {}

    Com& operator=(Com&& other) noexcept {
        if (this != &other) {
            reset();
            value_ = std::exchange(other.value_, nullptr);
        }
        return *this;
    }

    T* get() const { return value_; }

    T* operator->() const { return value_; }

    T** put() {
        reset();
        return &value_;
    }

    void reset() {
        if (value_)
            std::exchange(value_, nullptr)->Release();
    }

private:
    T* value_{};
};

struct Compiler {
    HMODULE module{LoadLibraryExW(L"d3dcompiler_47.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32)};

    ~Compiler() {
        if (module)
            FreeLibrary(module);
    }
};

struct IsolatedState {
    ID3D11DeviceContext1* context;
    Com<ID3DDeviceContextState> previous;

    IsolatedState(ID3D11DeviceContext1* value, ID3DDeviceContextState* own) : context(value) {
        context->SwapDeviceContextState(own, previous.put());
    }

    ~IsolatedState() {
        // Drop this pass's resource bindings before returning to the game. The
        // context state restores all pipeline stages, RTV/UAVs, and predication.
        context->ClearState();
        context->SwapDeviceContextState(previous.get(), nullptr);
    }
};

constexpr char shader[] = R"(
cbuffer HandConstants : register(b0) {
    row_major float4x4 clip_from_model;
    row_major float4x4 normal_from_model;
    float4 output_options;
    float4 world_depth_params;
    float4 eye_params;
};
#ifdef WORLD_MSAA
Texture2DMS<float> world_depth : register(t0);
#else
Texture2D<float> world_depth : register(t0);
#endif
struct Input { float3 position : POSITION; float3 normal : NORMAL; };
struct Vertex { float4 position : SV_POSITION; float3 normal : NORMAL; float distance : TEXCOORD0; };
Vertex vertex_main(Input input) {
    Vertex result;
    result.position = mul(clip_from_model, float4(input.position, 1));
    result.normal = mul(normal_from_model, float4(input.normal, 0)).xyz;
    result.distance = result.position.w;
    return result;
}
float4 pixel_main(Vertex input) : SV_TARGET {
    if (output_options.y > 0.5) {
        int2 coordinates = int2(input.position.xy * eye_params.xy * world_depth_params.zw);
        coordinates = clamp(coordinates, int2(0, 0), int2(world_depth_params.zw) - 1);
        bool reverse_depth = world_depth_params.y > 0;
#ifdef WORLD_MSAA
        float scene = reverse_depth ? 0 : 1;
        [loop] for (int sample_index = 0; sample_index < int(eye_params.z); ++sample_index) {
            float value = world_depth.Load(coordinates, sample_index);
            scene = reverse_depth ? max(scene, value) : min(scene, value);
        }
#else
        float scene = world_depth.Load(int3(coordinates, 0));
#endif
        // Bias the glove five millimeters toward the viewer. This avoids
        // surface flicker while keeping the comparison in native depth units.
        float hand_depth = world_depth_params.x + world_depth_params.y / max(input.distance - 0.005, 0.001);
        if (reverse_depth ? hand_depth < scene : hand_depth > scene) discard;
    }
    float diffuse = 0.4 + 0.6 * saturate(dot(normalize(input.normal), normalize(float3(0.3, 0.7, 0.6))));
    float3 color = float3(0.11, 0.04, 0.014) * diffuse;
    // The native compositor declares gamma color. Typed sRGB RTVs encode it;
    // ordinary UNORM/FLOAT RTVs need explicit encoding here.
    if (output_options.x > 0.5) color = pow(color, 1.0 / 2.2);
    return float4(color, 1);
}
)";

struct Constants {
    std::array<float, 16> clip;
    std::array<float, 16> normal;
    float options[4];
    float world_depth_params[4];
    float eye_params[4];
};

static_assert(sizeof(Constants) == 176);
static_assert(sizeof(mesh::Vertex) == 24);
static_assert(std::size(mesh::left) == 1368 && std::size(mesh::right) == 1368);

bool usable(const HandDraw& hand) {
    const auto finite = [](float value) { return std::isfinite(value) && std::abs(value) < 1000000.0f; };
    return hand.valid && std::all_of(hand.clip_from_model.begin(), hand.clip_from_model.end(), finite) &&
           std::all_of(hand.normal_from_model.begin(), hand.normal_from_model.end(), finite);
}

DXGI_FORMAT render_format(DXGI_FORMAT source) {
    switch (source) {
    case DXGI_FORMAT_R8G8B8A8_TYPELESS:
        return DXGI_FORMAT_R8G8B8A8_UNORM;
    case DXGI_FORMAT_B8G8R8A8_TYPELESS:
        return DXGI_FORMAT_B8G8R8A8_UNORM;
    case DXGI_FORMAT_B8G8R8X8_TYPELESS:
        return DXGI_FORMAT_B8G8R8X8_UNORM;
    case DXGI_FORMAT_R16G16B16A16_TYPELESS:
        return DXGI_FORMAT_R16G16B16A16_FLOAT;
    case DXGI_FORMAT_R10G10B10A2_TYPELESS:
        return DXGI_FORMAT_R10G10B10A2_UNORM;
    case DXGI_FORMAT_R8G8B8A8_UNORM:
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
    case DXGI_FORMAT_B8G8R8A8_UNORM:
    case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
    case DXGI_FORMAT_B8G8R8X8_UNORM:
    case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
    case DXGI_FORMAT_R16G16B16A16_FLOAT:
    case DXGI_FORMAT_R10G10B10A2_UNORM:
        return source;
    default:
        return DXGI_FORMAT_UNKNOWN;
    }
}

bool is_srgb(DXGI_FORMAT format) {
    return format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB || format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB ||
           format == DXGI_FORMAT_B8G8R8X8_UNORM_SRGB;
}

bool depth_formats(DXGI_FORMAT source, DXGI_FORMAT& copy, DXGI_FORMAT& view) {
    switch (source) {
    case DXGI_FORMAT_D32_FLOAT:
    case DXGI_FORMAT_R32_TYPELESS:
    case DXGI_FORMAT_R32_FLOAT:
        copy = DXGI_FORMAT_R32_TYPELESS;
        view = DXGI_FORMAT_R32_FLOAT;
        return true;
    case DXGI_FORMAT_D24_UNORM_S8_UINT:
    case DXGI_FORMAT_R24G8_TYPELESS:
    case DXGI_FORMAT_R24_UNORM_X8_TYPELESS:
        copy = DXGI_FORMAT_R24G8_TYPELESS;
        view = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
        return true;
    case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
    case DXGI_FORMAT_R32G8X24_TYPELESS:
    case DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS:
        copy = DXGI_FORMAT_R32G8X24_TYPELESS;
        view = DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS;
        return true;
    default:
        return false;
    }
}

} // namespace

struct Renderer::Resources {
    Com<ID3D11Device> device;
    Com<ID3D11DeviceContext1> context;
    Com<ID3DDeviceContextState> state;
    Com<ID3D11VertexShader> vertex_shader;
    Com<ID3D11PixelShader> pixel_shader;
    Com<ID3D11PixelShader> pixel_shader_msaa;
    Com<ID3D11InputLayout> layout;
    Com<ID3D11Buffer> vertices[2];
    float uploaded_grip[2]{-1.f, -1.f};
    Com<ID3D11Buffer> constants;
    Com<ID3D11RasterizerState> raster;
    Com<ID3D11DepthStencilState> depth_state;
    Com<ID3D11Texture2D> depth;
    Com<ID3D11DepthStencilView> depth_view;
    D3D11_TEXTURE2D_DESC depth_desc{};
    Com<ID3D11Texture2D> world_depth;
    Com<ID3D11ShaderResourceView> world_depth_view;
    D3D11_TEXTURE2D_DESC world_depth_desc{};

    struct Target {
        ID3D11Texture2D* color{}; // Owned indirectly by its view.
        Com<ID3D11RenderTargetView> view;
    };

    Target targets[2];
    unsigned replacement{};
    DrawResult status{DrawResult::failed};

    DrawResult initialize() {
        Com<ID3D11Device1> device1;
        Com<ID3D11DeviceContext> immediate;
        if (FAILED(device->QueryInterface(__uuidof(ID3D11Device1), reinterpret_cast<void**>(device1.put()))))
            return DrawResult::unsupported;
        device->GetImmediateContext(immediate.put());
        if (!immediate.get() || FAILED(immediate->QueryInterface(__uuidof(ID3D11DeviceContext1),
                                                                 reinterpret_cast<void**>(context.put()))))
            return DrawResult::unsupported;
        const auto level = device->GetFeatureLevel();
        if (level < D3D_FEATURE_LEVEL_10_0)
            return DrawResult::unsupported;
        const UINT flags = (device->GetCreationFlags() & D3D11_CREATE_DEVICE_SINGLETHREADED)
                               ? D3D11_1_CREATE_DEVICE_CONTEXT_STATE_SINGLETHREADED
                               : 0;
        if (FAILED(device1->CreateDeviceContextState(flags, &level, 1, D3D11_SDK_VERSION,
                                                     __uuidof(ID3D11Device), nullptr, state.put())))
            return DrawResult::unsupported;

        Compiler compiler;
        if (!compiler.module)
            return DrawResult::unsupported;
        const auto address = GetProcAddress(compiler.module, "D3DCompile");
        pD3DCompile compile{};
        static_assert(sizeof(compile) == sizeof(address));
        std::memcpy(&compile, &address, sizeof(compile));
        if (!compile)
            return DrawResult::unsupported;
        Com<ID3DBlob> vs, ps, errors;
        const UINT shader_flags = D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3;
        if (FAILED(compile(shader, sizeof(shader) - 1, nullptr, nullptr, nullptr, "vertex_main", "vs_4_0",
                           shader_flags, 0, vs.put(), errors.put())) ||
            FAILED(compile(shader, sizeof(shader) - 1, nullptr, nullptr, nullptr, "pixel_main", "ps_4_0",
                           shader_flags, 0, ps.put(), errors.put())))
            return DrawResult::failed;
        if (FAILED(device->CreateVertexShader(vs->GetBufferPointer(), vs->GetBufferSize(), nullptr,
                                              vertex_shader.put())) ||
            FAILED(device->CreatePixelShader(ps->GetBufferPointer(), ps->GetBufferSize(), nullptr,
                                             pixel_shader.put())))
            return DrawResult::failed;
        if (level >= D3D_FEATURE_LEVEL_10_1) {
            const D3D_SHADER_MACRO defines[] = {{"WORLD_MSAA", "1"}, {nullptr, nullptr}};
            Com<ID3DBlob> multisample;
            // Failure of this optional shader never prevents ordinary hands.
            if (SUCCEEDED(compile(shader, sizeof(shader) - 1, nullptr, defines, nullptr, "pixel_main",
                                  "ps_4_1", shader_flags, 0, multisample.put(), errors.put())))
                device->CreatePixelShader(multisample->GetBufferPointer(), multisample->GetBufferSize(),
                                          nullptr, pixel_shader_msaa.put());
        }
        const D3D11_INPUT_ELEMENT_DESC elements[] = {
            {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0}};
        if (FAILED(device->CreateInputLayout(elements, 2, vs->GetBufferPointer(), vs->GetBufferSize(),
                                             layout.put())))
            return DrawResult::failed;
        D3D11_BUFFER_DESC buffer{};
        buffer.ByteWidth = sizeof(mesh::Vertex) * skin::vertex_count;
        buffer.Usage = D3D11_USAGE_DYNAMIC;
        buffer.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        buffer.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        if (FAILED(device->CreateBuffer(&buffer, nullptr, vertices[0].put())) ||
            FAILED(device->CreateBuffer(&buffer, nullptr, vertices[1].put())))
            return DrawResult::failed;
        buffer = {};
        buffer.ByteWidth = sizeof(Constants);
        buffer.Usage = D3D11_USAGE_DYNAMIC;
        buffer.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        buffer.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        if (FAILED(device->CreateBuffer(&buffer, nullptr, constants.put())))
            return DrawResult::failed;
        D3D11_RASTERIZER_DESC raster_desc{};
        raster_desc.FillMode = D3D11_FILL_SOLID;
        // Both GLBs have verified outward CCW winding. No culling keeps the
        // wrist opening visible from either side; private depth resolves faces.
        raster_desc.CullMode = D3D11_CULL_NONE;
        raster_desc.DepthClipEnable = TRUE;
        raster_desc.MultisampleEnable = TRUE;
        if (FAILED(device->CreateRasterizerState(&raster_desc, raster.put())))
            return DrawResult::failed;
        D3D11_DEPTH_STENCIL_DESC depth_settings{};
        depth_settings.DepthEnable = TRUE;
        depth_settings.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
        depth_settings.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
        if (FAILED(device->CreateDepthStencilState(&depth_settings, depth_state.put())))
            return DrawResult::failed;
        return DrawResult::drawn;
    }

    bool upload_grip(unsigned hand, float grip) {
        if (grip == uploaded_grip[hand])
            return true;
        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (FAILED(context->Map(vertices[hand].get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
            return false;
        const bool result = skin::deform(hand == 0, grip, static_cast<mesh::Vertex*>(mapped.pData));
        context->Unmap(vertices[hand].get(), 0);
        if (result)
            uploaded_grip[hand] = grip;
        return result;
    }

    ID3D11RenderTargetView* target(ID3D11Texture2D* color, const D3D11_TEXTURE2D_DESC& desc) {
        for (auto& entry : targets)
            if (entry.color == color && entry.view.get())
                return entry.view.get();
        D3D11_RENDER_TARGET_VIEW_DESC view_desc{};
        view_desc.Format = render_format(desc.Format);
        view_desc.ViewDimension =
            desc.SampleDesc.Count > 1 ? D3D11_RTV_DIMENSION_TEXTURE2DMS : D3D11_RTV_DIMENSION_TEXTURE2D;
        Com<ID3D11RenderTargetView> view;
        if (FAILED(device->CreateRenderTargetView(color, &view_desc, view.put())))
            return nullptr;
        auto& entry = targets[(replacement++) % 2];
        entry.color = color;
        entry.view = std::move(view);
        return entry.view.get();
    }

    bool prepare_depth(const D3D11_TEXTURE2D_DESC& desc) {
        if (depth_view.get() && desc.Width == depth_desc.Width && desc.Height == depth_desc.Height &&
            desc.SampleDesc.Count == depth_desc.SampleDesc.Count &&
            desc.SampleDesc.Quality == depth_desc.SampleDesc.Quality)
            return true;
        depth_view.reset();
        depth.reset();
        depth_desc = {};
        depth_desc.Width = desc.Width;
        depth_desc.Height = desc.Height;
        depth_desc.MipLevels = 1;
        depth_desc.ArraySize = 1;
        depth_desc.Format = DXGI_FORMAT_D32_FLOAT;
        depth_desc.SampleDesc = desc.SampleDesc;
        depth_desc.Usage = D3D11_USAGE_DEFAULT;
        depth_desc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
        return SUCCEEDED(device->CreateTexture2D(&depth_desc, nullptr, depth.put())) &&
               SUCCEEDED(device->CreateDepthStencilView(depth.get(), nullptr, depth_view.put()));
    }

    bool prepare_world_depth(const SceneDepth& scene) {
        if (!scene.texture || !scene.width || !scene.height || !std::isfinite(scene.a) ||
            !std::isfinite(scene.b) || std::abs(scene.a) > 1000000.f || std::abs(scene.b) > 1000000.f ||
            std::abs(scene.b) < 1e-10f)
            return false;
        Com<ID3D11Device> source_device;
        scene.texture->GetDevice(source_device.put());
        if (source_device.get() != device.get())
            return false;
        D3D11_TEXTURE2D_DESC desc{};
        scene.texture->GetDesc(&desc);
        DXGI_FORMAT copy_format{}, view_format{};
        if (desc.ArraySize != 1 || scene.width > desc.Width || scene.height > desc.Height ||
            !desc.SampleDesc.Count || desc.SampleDesc.Count > 32 ||
            (desc.SampleDesc.Count > 1 && !pixel_shader_msaa.get()) ||
            !depth_formats(desc.Format, copy_format, view_format))
            return false;
        desc.Format = copy_format;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        desc.CPUAccessFlags = 0;
        desc.MiscFlags = 0;
        if (world_depth_view.get() && desc.Width == world_depth_desc.Width &&
            desc.Height == world_depth_desc.Height && desc.MipLevels == world_depth_desc.MipLevels &&
            desc.Format == world_depth_desc.Format &&
            desc.SampleDesc.Count == world_depth_desc.SampleDesc.Count &&
            desc.SampleDesc.Quality == world_depth_desc.SampleDesc.Quality)
            return true;
        world_depth_view.reset();
        world_depth.reset();
        if (FAILED(device->CreateTexture2D(&desc, nullptr, world_depth.put())))
            return false;
        D3D11_SHADER_RESOURCE_VIEW_DESC view_desc{};
        view_desc.Format = view_format;
        view_desc.ViewDimension =
            desc.SampleDesc.Count > 1 ? D3D11_SRV_DIMENSION_TEXTURE2DMS : D3D11_SRV_DIMENSION_TEXTURE2D;
        if (desc.SampleDesc.Count == 1)
            view_desc.Texture2D.MipLevels = 1;
        if (FAILED(device->CreateShaderResourceView(world_depth.get(), &view_desc, world_depth_view.put())))
            return false;
        world_depth_desc = desc;
        return true;
    }
};

Renderer::Renderer() noexcept = default;
Renderer::~Renderer() = default;

void Renderer::reset() noexcept {
    resources_.reset();
    thread_ = 0;
    world_depth_used_ = false;
}

DrawResult Renderer::draw(ID3D11Texture2D* color, const Frame& frame) noexcept {
    world_depth_used_ = false;
    float grip[2]{};
    const bool valid[2]{usable(frame.hands[0]) && skin::normalize_grip(frame.grip[0], grip[0]),
                        usable(frame.hands[1]) && skin::normalize_grip(frame.grip[1], grip[1])};
    if (!color || (!valid[0] && !valid[1]))
        return DrawResult::skipped;
    const auto current_thread = GetCurrentThreadId();
    if (thread_ && thread_ != current_thread)
        return DrawResult::skipped;
    thread_ = current_thread;
    try {
        D3D11_TEXTURE2D_DESC desc{};
        color->GetDesc(&desc);
        if (!desc.Width || !desc.Height || desc.ArraySize != 1 || !desc.SampleDesc.Count ||
            !(desc.BindFlags & D3D11_BIND_RENDER_TARGET) || render_format(desc.Format) == DXGI_FORMAT_UNKNOWN)
            return DrawResult::unsupported;
        Com<ID3D11Device> device;
        color->GetDevice(device.put());
        if (!device.get())
            return DrawResult::failed;
        if (!resources_ || resources_->device.get() != device.get()) {
            resources_ = std::make_unique<Resources>();
            resources_->device = std::move(device);
            resources_->status = resources_->initialize();
        }
        auto& own = *resources_;
        if (own.status != DrawResult::drawn)
            return own.status;
        ID3D11RenderTargetView* target = own.target(color, desc);
        if (!target || !own.prepare_depth(desc))
            return DrawResult::failed;
        const bool world_depth = own.prepare_world_depth(frame.depth);
        IsolatedState isolate(own.context.get(), own.state.get());
        if (world_depth) {
            own.context->CopyResource(own.world_depth.get(), frame.depth.texture);
            ID3D11ShaderResourceView* view = own.world_depth_view.get();
            own.context->PSSetShaderResources(0, 1, &view);
        }
        const D3D11_VIEWPORT viewport{0, 0, static_cast<float>(desc.Width), static_cast<float>(desc.Height),
                                      0, 1};
        own.context->RSSetViewports(1, &viewport);
        own.context->RSSetState(own.raster.get());
        own.context->OMSetRenderTargets(1, &target, own.depth_view.get());
        own.context->OMSetDepthStencilState(own.depth_state.get(), 0);
        own.context->ClearDepthStencilView(own.depth_view.get(), D3D11_CLEAR_DEPTH, 1.0f, 0);
        own.context->IASetInputLayout(own.layout.get());
        own.context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        own.context->VSSetShader(own.vertex_shader.get(), nullptr, 0);
        own.context->PSSetShader(world_depth && own.world_depth_desc.SampleDesc.Count > 1
                                     ? own.pixel_shader_msaa.get()
                                     : own.pixel_shader.get(),
                                 nullptr, 0);
        ID3D11Buffer* constant = own.constants.get();
        own.context->VSSetConstantBuffers(0, 1, &constant);
        own.context->PSSetConstantBuffers(0, 1, &constant);
        for (unsigned hand = 0; hand != 2; ++hand) {
            if (!valid[hand])
                continue;
            if (!own.upload_grip(hand, grip[hand]))
                return DrawResult::failed;
            D3D11_MAPPED_SUBRESOURCE mapped{};
            if (FAILED(own.context->Map(constant, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
                return DrawResult::failed;
            const Constants data{
                frame.hands[hand].clip_from_model,
                frame.hands[hand].normal_from_model,
                {is_srgb(render_format(desc.Format)) ? 0.0f : 1.0f, world_depth ? 1.f : 0.f, 0, 0},
                {frame.depth.a, frame.depth.b, static_cast<float>(frame.depth.width),
                 static_cast<float>(frame.depth.height)},
                {1.f / static_cast<float>(desc.Width), 1.f / static_cast<float>(desc.Height),
                 static_cast<float>(own.world_depth_desc.SampleDesc.Count), 0}};
            std::memcpy(mapped.pData, &data, sizeof(data));
            own.context->Unmap(constant, 0);
            ID3D11Buffer* vertex = own.vertices[hand].get();
            constexpr UINT stride = sizeof(mesh::Vertex), offset = 0;
            own.context->IASetVertexBuffers(0, 1, &vertex, &stride, &offset);
            own.context->Draw(static_cast<UINT>(std::size(mesh::left)), 0);
        }
        world_depth_used_ = world_depth;
        return DrawResult::drawn;
    } catch (...) {
        return DrawResult::failed;
    }
}

} // namespace witness::hands
