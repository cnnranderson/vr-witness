#include "render/hands_draw.hpp"
#include "render/hands_skin.hpp"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <d3d11_1.h>
#include <d3dcompiler.h>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

void require(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}

void checked(HRESULT result, const char* message) {
    if (FAILED(result))
        throw std::runtime_error(std::string(message) + " HRESULT=" + std::to_string(result));
}

template <typename T> struct Com {
    T* value{};

    ~Com() {
        if (value)
            value->Release();
    }

    Com() = default;
    Com(const Com&) = delete;
    Com& operator=(const Com&) = delete;

    T* operator->() const { return value; }

    T** put() {
        require(!value, "COM output already owned");
        return &value;
    }
};

struct Compiler {
    HMODULE module{LoadLibraryExW(L"d3dcompiler_47.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32)};
    pD3DCompile compile{};

    Compiler() {
        require(module != nullptr, "System shader compiler unavailable");
        const auto proc = GetProcAddress(module, "D3DCompile");
        std::memcpy(&compile, &proc, sizeof(compile));
        require(compile != nullptr, "D3DCompile unavailable");
    }

    ~Compiler() { FreeLibrary(module); }

    void shader(const char* code, const char* target, ID3DBlob** output) {
        Com<ID3DBlob> errors;
        checked(compile(code, std::strlen(code), nullptr, nullptr, nullptr, "main", target,
                        D3DCOMPILE_ENABLE_STRICTNESS, 0, output, errors.put()),
                "Sentinel shader compilation");
    }
};

struct Device {
    Com<ID3D11Device> device;
    Com<ID3D11DeviceContext> context;

    explicit Device(UINT flags = 0) {
        const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0};
        checked(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags, levels, 1, D3D11_SDK_VERSION,
                                  device.put(), nullptr, context.put()),
                "Create WARP device");
    }

    void color(ID3D11Texture2D** output, UINT width = 256, UINT height = 192, UINT samples = 1,
               DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM) {
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = width;
        desc.Height = height;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = format;
        desc.SampleDesc.Count = samples;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        checked(device->CreateTexture2D(&desc, nullptr, output), "Create color texture");
    }

    void clear(ID3D11Texture2D* target) {
        Com<ID3D11RenderTargetView> view;
        checked(device->CreateRenderTargetView(target, nullptr, view.put()), "Create clear view");
        const float black[4]{};
        context->ClearRenderTargetView(view.value, black);
    }

    std::vector<std::uint32_t> pixels(ID3D11Texture2D* target) {
        D3D11_TEXTURE2D_DESC desc{};
        target->GetDesc(&desc);
        Com<ID3D11Texture2D> resolved;
        if (desc.SampleDesc.Count > 1) {
            desc.SampleDesc = {1, 0};
            checked(device->CreateTexture2D(&desc, nullptr, resolved.put()), "Create resolve target");
            context->ResolveSubresource(resolved.value, 0, target, 0, desc.Format);
            target = resolved.value;
        }
        desc.Usage = D3D11_USAGE_STAGING;
        desc.BindFlags = 0;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        Com<ID3D11Texture2D> staging;
        checked(device->CreateTexture2D(&desc, nullptr, staging.put()), "Create readback texture");
        context->CopyResource(staging.value, target);
        D3D11_MAPPED_SUBRESOURCE mapped{};
        checked(context->Map(staging.value, 0, D3D11_MAP_READ, 0, &mapped), "Read hand pixels");
        std::vector<std::uint32_t> result(desc.Width * desc.Height);
        for (UINT y = 0; y != desc.Height; ++y)
            std::memcpy(result.data() + y * desc.Width,
                        static_cast<const unsigned char*>(mapped.pData) + y * mapped.RowPitch,
                        desc.Width * 4);
        context->Unmap(staging.value, 0);
        return result;
    }
};

struct WorldDepth {
    Com<ID3D11Texture2D> texture;
    Com<ID3D11DepthStencilView> view;

    WorldDepth(Device& host, DXGI_FORMAT format = DXGI_FORMAT_D32_FLOAT, UINT samples = 1, UINT width = 128,
               UINT height = 96, const float* initial = nullptr) {
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = width;
        desc.Height = height;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = format;
        desc.SampleDesc.Count = samples;
        desc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
        const D3D11_SUBRESOURCE_DATA data{initial, width * static_cast<UINT>(sizeof(float)), 0};
        checked(host.device->CreateTexture2D(&desc, initial ? &data : nullptr, texture.put()),
                "Create native world depth fixture");
        checked(host.device->CreateDepthStencilView(texture.value, nullptr, view.put()),
                "Create native world depth view");
    }

    void clear(Device& host, float value) {
        host.context->ClearDepthStencilView(view.value, D3D11_CLEAR_DEPTH, value, 0);
    }

    std::vector<unsigned char> snapshot(Device& host) {
        D3D11_TEXTURE2D_DESC desc{};
        texture->GetDesc(&desc);
        require(desc.SampleDesc.Count == 1, "Depth readback fixture requires one sample");
        desc.Usage = D3D11_USAGE_STAGING;
        desc.BindFlags = 0;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        Com<ID3D11Texture2D> copy;
        checked(host.device->CreateTexture2D(&desc, nullptr, copy.put()), "Create native depth readback");
        host.context->CopyResource(copy.value, texture.value);
        D3D11_MAPPED_SUBRESOURCE mapped{};
        checked(host.context->Map(copy.value, 0, D3D11_MAP_READ, 0, &mapped), "Read native depth");
        const UINT row_size = desc.Width * (desc.Format == DXGI_FORMAT_D32_FLOAT_S8X24_UINT ? 8 : 4);
        std::vector<unsigned char> bytes(row_size * desc.Height);
        for (UINT row = 0; row != desc.Height; ++row)
            std::memcpy(bytes.data() + row * row_size,
                        static_cast<const unsigned char*>(mapped.pData) + row * mapped.RowPitch, row_size);
        host.context->Unmap(copy.value, 0);
        return bytes;
    }

    // Give only sample zero a near wall. Selecting an arbitrary sample or an
    // average would leave gloves visible; the renderer must choose the closest.
    void split_samples(Device& host, bool reverse) {
        Compiler compiler;
        Com<ID3DBlob> vertex_code, pixel_code;
        compiler.shader("float4 main(uint id:SV_VertexID):SV_POSITION {"
                        "float2 p = id == 0 ? float2(-1,-1) : id == 1 ? float2(-1,3) : float2(3,-1);"
                        "return float4(p,0,1);}",
                        "vs_5_0", vertex_code.put());
        compiler.shader(
            reverse ? "float main(uint sample:SV_SampleIndex):SV_Depth{return sample==0 ? 0.2 : 0.05;}"
                    : "float main(uint sample:SV_SampleIndex):SV_Depth{return sample==0 ? 0.8 : 0.95;}",
            "ps_5_0", pixel_code.put());
        Com<ID3D11VertexShader> vertex;
        Com<ID3D11PixelShader> pixel;
        Com<ID3D11DepthStencilState> depth_state;
        Com<ID3D11RasterizerState> raster;
        checked(host.device->CreateVertexShader(vertex_code->GetBufferPointer(), vertex_code->GetBufferSize(),
                                                nullptr, vertex.put()),
                "World depth fixture vertex shader");
        checked(host.device->CreatePixelShader(pixel_code->GetBufferPointer(), pixel_code->GetBufferSize(),
                                               nullptr, pixel.put()),
                "World depth fixture pixel shader");
        D3D11_DEPTH_STENCIL_DESC depth_desc{};
        depth_desc.DepthEnable = TRUE;
        depth_desc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
        depth_desc.DepthFunc = D3D11_COMPARISON_ALWAYS;
        checked(host.device->CreateDepthStencilState(&depth_desc, depth_state.put()),
                "World fixture depth state");
        D3D11_RASTERIZER_DESC raster_desc{};
        raster_desc.FillMode = D3D11_FILL_SOLID;
        raster_desc.CullMode = D3D11_CULL_NONE;
        raster_desc.DepthClipEnable = TRUE;
        raster_desc.MultisampleEnable = TRUE;
        checked(host.device->CreateRasterizerState(&raster_desc, raster.put()), "World fixture raster state");
        D3D11_TEXTURE2D_DESC desc{};
        texture->GetDesc(&desc);
        const D3D11_VIEWPORT viewport{0, 0, static_cast<float>(desc.Width), static_cast<float>(desc.Height),
                                      0, 1};
        host.context->ClearState();
        host.context->RSSetViewports(1, &viewport);
        host.context->RSSetState(raster.value);
        host.context->OMSetRenderTargets(0, nullptr, view.value);
        host.context->OMSetDepthStencilState(depth_state.value, 0);
        host.context->VSSetShader(vertex.value, nullptr, 0);
        host.context->PSSetShader(pixel.value, nullptr, 0);
        host.context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        host.context->Draw(3, 0);
        host.context->ClearState();
    }
};

// Snapshot actual API state, including slots outside the hand pass. Querying a
// binding returns an owned reference; retain only its identity and release it.
struct State {
    std::vector<std::uint64_t> values;

    template <typename T> void object(T* value) {
        values.push_back(reinterpret_cast<std::uintptr_t>(value));
        if (value)
            value->Release();
    }

    void number(UINT value) { values.push_back(value); }

    void scalar(float value) {
        UINT bits;
        std::memcpy(&bits, &value, sizeof(bits));
        number(bits);
    }

    explicit State(ID3D11DeviceContext* context) {
        ID3D11RenderTargetView* rtvs[8]{};
        ID3D11DepthStencilView* depth{};
        ID3D11UnorderedAccessView* uavs[8]{};
        context->OMGetRenderTargetsAndUnorderedAccessViews(8, rtvs, &depth, 0, 8, uavs);
        for (auto* view : rtvs)
            object(view);
        object(depth);
        for (auto* view : uavs)
            object(view);
        context->CSGetUnorderedAccessViews(0, 8, uavs);
        for (auto* view : uavs)
            object(view);
        ID3D11DepthStencilState* depth_state{};
        UINT stencil{};
        context->OMGetDepthStencilState(&depth_state, &stencil);
        object(depth_state);
        number(stencil);
        ID3D11BlendState* blend{};
        float factor[4]{};
        UINT mask{};
        context->OMGetBlendState(&blend, factor, &mask);
        object(blend);
        for (auto value : factor)
            scalar(value);
        number(mask);
        ID3D11RasterizerState* raster{};
        context->RSGetState(&raster);
        object(raster);
        UINT count = 16;
        D3D11_VIEWPORT viewports[16]{};
        context->RSGetViewports(&count, viewports);
        number(count);
        for (UINT i = 0; i != count; ++i) {
            scalar(viewports[i].TopLeftX);
            scalar(viewports[i].TopLeftY);
            scalar(viewports[i].Width);
            scalar(viewports[i].Height);
            scalar(viewports[i].MinDepth);
            scalar(viewports[i].MaxDepth);
        }
        count = 16;
        D3D11_RECT rectangles[16]{};
        context->RSGetScissorRects(&count, rectangles);
        number(count);
        for (UINT i = 0; i != count; ++i) {
            number(rectangles[i].left);
            number(rectangles[i].right);
            number(rectangles[i].top);
            number(rectangles[i].bottom);
        }
        ID3D11InputLayout* layout{};
        context->IAGetInputLayout(&layout);
        object(layout);
        D3D11_PRIMITIVE_TOPOLOGY topology{};
        context->IAGetPrimitiveTopology(&topology);
        number(topology);
        ID3D11Buffer* buffers[16]{};
        UINT strides[16]{}, offsets[16]{};
        context->IAGetVertexBuffers(0, 16, buffers, strides, offsets);
        for (unsigned i = 0; i != 16; ++i) {
            object(buffers[i]);
            number(strides[i]);
            number(offsets[i]);
        }
        ID3D11Buffer* index{};
        DXGI_FORMAT index_format{};
        UINT index_offset{};
        context->IAGetIndexBuffer(&index, &index_format, &index_offset);
        object(index);
        number(index_format);
        number(index_offset);
#define CAPTURE_STAGE(prefix, type)                                                                          \
    {                                                                                                        \
        type* shader{};                                                                                      \
        context->prefix##GetShader(&shader, nullptr, nullptr);                                               \
        object(shader);                                                                                      \
        ID3D11Buffer* constants[14]{};                                                                       \
        context->prefix##GetConstantBuffers(0, 14, constants);                                               \
        for (auto* buffer : constants)                                                                       \
            object(buffer);                                                                                  \
        ID3D11ShaderResourceView* resources[16]{};                                                           \
        context->prefix##GetShaderResources(0, 16, resources);                                               \
        for (auto* resource : resources)                                                                     \
            object(resource);                                                                                \
        ID3D11SamplerState* samplers[16]{};                                                                  \
        context->prefix##GetSamplers(0, 16, samplers);                                                       \
        for (auto* sampler : samplers)                                                                       \
            object(sampler);                                                                                 \
    }
        CAPTURE_STAGE(VS, ID3D11VertexShader)
        CAPTURE_STAGE(PS, ID3D11PixelShader)
        CAPTURE_STAGE(GS, ID3D11GeometryShader)
        CAPTURE_STAGE(HS, ID3D11HullShader)
        CAPTURE_STAGE(DS, ID3D11DomainShader)
        CAPTURE_STAGE(CS, ID3D11ComputeShader)
#undef CAPTURE_STAGE
        ID3D11Predicate* predicate{};
        BOOL predicate_value{};
        context->GetPredication(&predicate, &predicate_value);
        object(predicate);
        number(predicate_value);
        ID3D11Buffer* stream[4]{};
        context->SOGetTargets(4, stream);
        for (auto* buffer : stream)
            object(buffer);
    }

    bool operator==(const State& other) const { return values == other.values; }
};

struct Sentinels {
    Com<ID3D11Texture2D> color, depth;
    Com<ID3D11RenderTargetView> target;
    Com<ID3D11DepthStencilView> depth_view;
    Com<ID3D11ShaderResourceView> sampled_eye;
    Com<ID3D11Buffer> vertex, index, constant, stream, unordered, compute_unordered;
    Com<ID3D11UnorderedAccessView> uav, compute_uav;
    Com<ID3D11Predicate> predicate;
    Com<ID3D11BlendState> blend;
    Com<ID3D11RasterizerState> raster;
    Com<ID3D11DepthStencilState> depth_state;
    Com<ID3D11SamplerState> sampler;
    Com<ID3D11VertexShader> vs;
    Com<ID3D11PixelShader> ps;
    Com<ID3D11GeometryShader> gs;
    Com<ID3D11InputLayout> layout;

    explicit Sentinels(Device& host, ID3D11Texture2D* eye) {
        auto* device = host.device.value;
        auto* context = host.context.value;
        host.color(color.put());
        checked(device->CreateRenderTargetView(color.value, nullptr, target.put()), "Sentinel RTV");
        checked(device->CreateShaderResourceView(eye, nullptr, sampled_eye.put()), "Sentinel eye SRV");
        D3D11_TEXTURE2D_DESC depth_desc{};
        color->GetDesc(&depth_desc);
        depth_desc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
        depth_desc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
        checked(device->CreateTexture2D(&depth_desc, nullptr, depth.put()), "Sentinel depth texture");
        checked(device->CreateDepthStencilView(depth.value, nullptr, depth_view.put()), "Sentinel DSV");
        D3D11_BUFFER_DESC buffer{};
        buffer.ByteWidth = 256;
        buffer.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        checked(device->CreateBuffer(&buffer, nullptr, vertex.put()), "Sentinel vertex");
        buffer.BindFlags = D3D11_BIND_INDEX_BUFFER;
        checked(device->CreateBuffer(&buffer, nullptr, index.put()), "Sentinel index");
        buffer.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        checked(device->CreateBuffer(&buffer, nullptr, constant.put()), "Sentinel constant");
        buffer.BindFlags = D3D11_BIND_STREAM_OUTPUT;
        checked(device->CreateBuffer(&buffer, nullptr, stream.put()), "Sentinel stream");
        buffer.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
        buffer.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        buffer.StructureByteStride = 4;
        checked(device->CreateBuffer(&buffer, nullptr, unordered.put()), "Sentinel unordered");
        checked(device->CreateUnorderedAccessView(unordered.value, nullptr, uav.put()), "Sentinel UAV");
        checked(device->CreateBuffer(&buffer, nullptr, compute_unordered.put()),
                "Sentinel compute unordered");
        checked(device->CreateUnorderedAccessView(compute_unordered.value, nullptr, compute_uav.put()),
                "Sentinel compute UAV");
        const D3D11_QUERY_DESC query{D3D11_QUERY_OCCLUSION_PREDICATE, 0};
        checked(device->CreatePredicate(&query, predicate.put()), "Sentinel predicate");
        D3D11_BLEND_DESC blend_desc{};
        blend_desc.RenderTarget[0].RenderTargetWriteMask = 0;
        checked(device->CreateBlendState(&blend_desc, blend.put()), "Sentinel blend");
        D3D11_RASTERIZER_DESC raster_desc{};
        raster_desc.FillMode = D3D11_FILL_WIREFRAME;
        raster_desc.CullMode = D3D11_CULL_FRONT;
        raster_desc.DepthClipEnable = TRUE;
        raster_desc.ScissorEnable = TRUE;
        checked(device->CreateRasterizerState(&raster_desc, raster.put()), "Sentinel raster");
        D3D11_DEPTH_STENCIL_DESC depth_settings{};
        depth_settings.DepthEnable = TRUE;
        depth_settings.DepthFunc = D3D11_COMPARISON_NEVER;
        checked(device->CreateDepthStencilState(&depth_settings, depth_state.put()), "Sentinel depth state");
        D3D11_SAMPLER_DESC sampler_desc{};
        sampler_desc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
        sampler_desc.AddressU = sampler_desc.AddressV = sampler_desc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
        sampler_desc.MaxLOD = D3D11_FLOAT32_MAX;
        sampler_desc.ComparisonFunc = D3D11_COMPARISON_ALWAYS;
        checked(device->CreateSamplerState(&sampler_desc, sampler.put()), "Sentinel sampler");
        Compiler compiler;
        Com<ID3DBlob> vertex_code, pixel_code, geometry_code;
        compiler.shader("float4 main(float3 p:POSITION):SV_POSITION{return float4(p,1);}", "vs_4_0",
                        vertex_code.put());
        compiler.shader("float4 main():SV_TARGET{return float4(1,0,1,1);}", "ps_4_0", pixel_code.put());
        compiler.shader("[maxvertexcount(1)]void main(point float4 p[1]:SV_POSITION,"
                        "inout PointStream<float4> output){}",
                        "gs_4_0", geometry_code.put());
        checked(device->CreateVertexShader(vertex_code->GetBufferPointer(), vertex_code->GetBufferSize(),
                                           nullptr, vs.put()),
                "Sentinel VS");
        checked(device->CreatePixelShader(pixel_code->GetBufferPointer(), pixel_code->GetBufferSize(),
                                          nullptr, ps.put()),
                "Sentinel PS");
        checked(device->CreateGeometryShader(geometry_code->GetBufferPointer(),
                                             geometry_code->GetBufferSize(), nullptr, gs.put()),
                "Sentinel GS");
        const D3D11_INPUT_ELEMENT_DESC element{
            "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0};
        checked(device->CreateInputLayout(&element, 1, vertex_code->GetBufferPointer(),
                                          vertex_code->GetBufferSize(), layout.put()),
                "Sentinel layout");
        context->OMSetRenderTargetsAndUnorderedAccessViews(1, &target.value, depth_view.value, 1, 1,
                                                           &uav.value, nullptr);
        const float factors[] = {0.1f, 0.2f, 0.3f, 0.4f};
        context->OMSetBlendState(blend.value, factors, 0x369C);
        context->OMSetDepthStencilState(depth_state.value, 41);
        context->RSSetState(raster.value);
        const D3D11_VIEWPORT viewport{3, 7, 113, 109, 0.2f, 0.8f};
        const D3D11_RECT rectangle{2, 4, 55, 66};
        context->RSSetViewports(1, &viewport);
        context->RSSetScissorRects(1, &rectangle);
        context->IASetInputLayout(layout.value);
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_POINTLIST);
        const UINT stride = 16, offset = 32;
        context->IASetVertexBuffers(0, 1, &vertex.value, &stride, &offset);
        context->IASetVertexBuffers(5, 1, &vertex.value, &stride, &offset);
        context->IASetIndexBuffer(index.value, DXGI_FORMAT_R16_UINT, 16);
        context->VSSetShader(vs.value, nullptr, 0);
        context->PSSetShader(ps.value, nullptr, 0);
        context->GSSetShader(gs.value, nullptr, 0);
#define SET_STAGE(prefix)                                                                                    \
    context->prefix##SetConstantBuffers(0, 1, &constant.value);                                              \
    context->prefix##SetConstantBuffers(13, 1, &constant.value);                                             \
    context->prefix##SetShaderResources(0, 1, &sampled_eye.value);                                           \
    context->prefix##SetSamplers(0, 1, &sampler.value)
        SET_STAGE(VS);
        SET_STAGE(PS);
        SET_STAGE(GS);
        SET_STAGE(HS);
        SET_STAGE(DS);
        SET_STAGE(CS);
#undef SET_STAGE
        const UINT stream_offset = 0;
        context->SOSetTargets(1, &stream.value, &stream_offset);
        context->CSSetUnorderedAccessViews(2, 1, &compute_uav.value, nullptr);
        context->Begin(predicate.value);
        context->End(predicate.value);
        context->SetPredication(predicate.value, TRUE);
    }
};

witness::hands::Frame frame() {
    witness::hands::Frame result;
    for (unsigned hand = 0; hand != 2; ++hand) {
        auto& value = result.hands[hand];
        value.valid = true;
        value.clip_from_model = {5, 0, 0, hand ? 0.45f : -0.45f, 0, 5, 0, -0.15f, 0, 0, -1, 0.5f, 0, 0, 0, 1};
        value.normal_from_model = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    }
    return result;
}

unsigned colored(const std::vector<std::uint32_t>& pixels, UINT width, bool right) {
    unsigned count = 0;
    for (std::size_t i = 0; i != pixels.size(); ++i) {
        if ((i % width >= width / 2) != right)
            continue;
        const auto value = pixels[i];
        if (!value)
            continue;
        const auto red = value & 255, green = (value >> 8) & 255, blue = (value >> 16) & 255;
        require(red > green && green > blue && (value >> 24) != 0, "Expected shaded brown glove pixel");
        ++count;
    }
    return count;
}

bool half_equal(const std::vector<std::uint32_t>& a, const std::vector<std::uint32_t>& b, bool right) {
    for (std::size_t i = 0; i != a.size(); ++i)
        if ((i % 256 >= 128) == right && a[i] != b[i])
            return false;
    return true;
}

float distance(const witness::hands::Mat4& a, const witness::hands::Mat4& b) {
    const float x = a[3] - b[3], y = a[7] - b[7], z = a[11] - b[11];
    return std::sqrt(x * x + y * y + z * z);
}

void check_skinning() {
    using namespace witness::hands;
    std::array<mesh::Vertex, skin::vertex_count> open{}, middle{}, closed{}, clamped{};
    skin::Pose rest, half, grip;
    for (bool left : {true, false}) {
        const auto* source = left ? mesh::left : mesh::right;
        const auto* bones = left ? mesh::left_bones : mesh::right_bones;
        require(skin::deform(left, 0, open.data()) && skin::deform(left, 0.5f, middle.data()) &&
                    skin::deform(left, 1, closed.data()),
                "Generate open, intermediate and closed skin");
        unsigned moved = 0, fixed = 0, nonlinear = 0;
        for (unsigned i = 0; i != skin::vertex_count; ++i) {
            require(std::memcmp(&open[i], &source[i].rest, sizeof(mesh::Vertex)) == 0,
                    "Open glove must preserve the prepared rest vertices exactly");
            float change = 0, normal_length = 0, lerp_error = 0;
            for (unsigned axis = 0; axis != 3; ++axis) {
                require(std::isfinite(closed[i].position[axis]) && std::abs(closed[i].position[axis]) < 0.25f,
                        "Skinned position escaped hand bounds");
                require(std::isfinite(closed[i].normal[axis]), "Skinned normal is not finite");
                change += std::abs(closed[i].position[axis] - open[i].position[axis]);
                normal_length += closed[i].normal[axis] * closed[i].normal[axis];
                lerp_error += std::abs(middle[i].position[axis] -
                                       (open[i].position[axis] + closed[i].position[axis]) * 0.5f);
            }
            require(std::abs(normal_length - 1) < 0.00001f, "Weighted normals must be normalized");
            moved += change > 0.01f;
            nonlinear += lerp_error > 0.001f;
            float anchored_weight = 0;
            for (unsigned influence = 0; influence != 4; ++influence)
                if (source[i].joints[influence] == 0 || source[i].joints[influence] == 25)
                    anchored_weight += source[i].weights[influence];
            if (anchored_weight == 1) {
                require(change < 0.000001f, "Palm and wrist vertices must stay anchored");
                ++fixed;
            }
        }
        require(moved > 200 && fixed > 100 && nonlinear > 200,
                "Grip must articulate fingers while retaining the palm");
        require(skin::make_pose(left, 0, rest) && skin::make_pose(left, 0.5f, half) &&
                    skin::make_pose(left, 1, grip),
                "Generate joint poses");
        for (unsigned i = 0; i != skin::bone_count; ++i) {
            const int parent = bones[i].parent;
            if (parent >= 0) {
                const auto& local = bones[i].local;
                const float length =
                    std::sqrt(local[3] * local[3] + local[7] * local[7] + local[11] * local[11]);
                require(std::abs(distance(half.global[i], half.global[parent]) - length) < 0.000001f &&
                            std::abs(distance(grip.global[i], grip.global[parent]) - length) < 0.000001f,
                        "Animated bone length changed or detached");
            }
        }
        for (unsigned index : {0u, 6u, 11u, 16u, 21u, 25u})
            require(distance(rest.global[index], grip.global[index]) < 0.000001f,
                    "Wrist, palm and proximal anchors must not move");
        for (unsigned tip : {9u, 14u, 19u, 24u})
            require(grip.global[tip][7] < rest.global[tip][7] - 0.04f &&
                        grip.global[tip][11] > rest.global[tip][11] + 0.01f,
                    "Fingertip must curl down toward the positive-Z palm");
        require(std::abs(grip.global[4][3]) < std::abs(rest.global[4][3]) - 0.02f &&
                    grip.global[4][11] > rest.global[4][11] + 0.02f,
                "Thumb must oppose inward toward the palm");
        require(skin::deform(left, -5, clamped.data()) &&
                    std::memcmp(open.data(), clamped.data(), sizeof(open)) == 0,
                "Negative grip must clamp to open");
        require(skin::deform(left, 5, clamped.data()) &&
                    std::memcmp(closed.data(), clamped.data(), sizeof(closed)) == 0,
                "Large grip must clamp to closed");
        require(!skin::deform(left, std::numeric_limits<float>::quiet_NaN(), clamped.data()) &&
                    std::memcmp(closed.data(), clamped.data(), sizeof(closed)) == 0,
                "Invalid grip must leave destination untouched");
    }
    skin::Pose left, right;
    require(skin::make_pose(true, 1, left) && skin::make_pose(false, 1, right),
            "Generate mirrored grip poses");
    for (unsigned i = 0; i != skin::bone_count; ++i)
        require(std::abs(left.global[i][3] + right.global[i][3]) < 0.000002f &&
                    std::abs(left.global[i][7] - right.global[i][7]) < 0.000002f &&
                    std::abs(left.global[i][11] - right.global[i][11]) < 0.000002f,
                "Closed skeletons must remain mirror images");
}

void save_bmp(const char* path, const std::vector<std::uint32_t>& pixels) {
    BITMAPFILEHEADER file{};
    file.bfType = 0x4D42;
    file.bfOffBits = sizeof(file) + sizeof(BITMAPINFOHEADER);
    file.bfSize = file.bfOffBits + static_cast<DWORD>(pixels.size() * 4);
    BITMAPINFOHEADER info{};
    info.biSize = sizeof(info);
    info.biWidth = 256;
    info.biHeight = -192;
    info.biPlanes = 1;
    info.biBitCount = 32;
    std::ofstream output(path, std::ios::binary);
    output.write(reinterpret_cast<const char*>(&file), sizeof(file));
    output.write(reinterpret_cast<const char*>(&info), sizeof(info));
    for (auto value : pixels) {
        value = (value & 0xFF00FF00) | ((value & 255) << 16) | ((value >> 16) & 255);
        output.write(reinterpret_cast<const char*>(&value), sizeof(value));
    }
    require(static_cast<bool>(output), "Write renderer capture");
}

void check_world_depth(Device& host, ID3D11Texture2D* eye, witness::hands::Renderer& renderer) {
    using namespace witness::hands;
    host.context->ClearState();
    auto hands = frame();
    hands.grip = {1, 0.5f};
    host.clear(eye);
    require(renderer.draw(eye, hands) == DrawResult::drawn && !renderer.used_world_depth(),
            "Missing world depth must draw ordinary gloves");
    const auto ordinary = host.pixels(eye);
    for (DXGI_FORMAT format :
         {DXGI_FORMAT_D32_FLOAT, DXGI_FORMAT_D24_UNORM_S8_UINT, DXGI_FORMAT_D32_FLOAT_S8X24_UINT}) {
        WorldDepth world(host, format);
        for (bool reverse : {false, true}) {
            hands.depth = {world.texture.value, reverse ? 0.f : 1.f, reverse ? 0.1f : -0.1f, 128, 96};
            world.clear(host, reverse ? 0.2f : 0.8f);
            const auto native_before = world.snapshot(host);
            host.context->OMSetRenderTargets(0, nullptr, world.view.value);
            const State pipeline_before(host.context.value);
            host.clear(eye);
            require(renderer.draw(eye, hands) == DrawResult::drawn && renderer.used_world_depth(),
                    "Compatible native world depth was not used");
            require(pipeline_before == State(host.context.value),
                    "World depth draw leaked native DSV or state");
            const auto hidden = host.pixels(eye);
            require(colored(hidden, 256, false) == 0 && colored(hidden, 256, true) == 0,
                    "Near wall must occlude both gloves");
            require(native_before == world.snapshot(host), "Renderer modified native depth contents");
            world.clear(host, reverse ? 0.05f : 0.95f);
            host.clear(eye);
            require(renderer.draw(eye, hands) == DrawResult::drawn && renderer.used_world_depth() &&
                        host.pixels(eye) == ordinary,
                    "Far wall must preserve all glove pixels");
            // A surface at the exact nominal hand distance remains visible with
            // the five-millimeter bias, including reversed depth conventions.
            world.clear(host, reverse ? 0.1f : 0.9f);
            host.clear(eye);
            require(renderer.draw(eye, hands) == DrawResult::drawn && host.pixels(eye) == ordinary,
                    "Depth bias must retain a glove on the surface");
            host.context->ClearState();
        }
    }
    // Equal screen placement with different clip W values exercises the eye
    // distance varying, independently of screen-space SV_Position depth.
    WorldDepth meter_wall(host);
    meter_wall.clear(host, 0.9f); // A=1, B=-0.1, wall at one meter.
    auto distances = hands;
    distances.depth = {meter_wall.texture.value, 1, -0.1f, 128, 96};
    for (unsigned hand = 0; hand != 2; ++hand)
        for (float& value : distances.hands[hand].clip_from_model)
            value *= hand ? 2.f : 0.5f;
    host.clear(eye);
    require(renderer.draw(eye, distances) == DrawResult::drawn && renderer.used_world_depth(),
            "Per-hand eye distance draw failed");
    const auto distance_pixels = host.pixels(eye);
    require(half_equal(distance_pixels, ordinary, false) && colored(distance_pixels, 256, true) == 0,
            "World depth comparison ignored clip W eye distance");
    // The active world viewport occupies the top-left quarter of a larger
    // texture. Only its right half is far enough away to show a glove.
    std::vector<float> pattern(256 * 192, 0.8f);
    for (unsigned row = 0; row != 96; ++row)
        for (unsigned column = 64; column != 128; ++column)
            pattern[row * 256 + column] = 0.95f;
    WorldDepth partial(host, DXGI_FORMAT_D32_FLOAT, 1, 256, 192, pattern.data());
    hands.depth = {partial.texture.value, 1, -0.1f, 128, 96};
    host.clear(eye);
    require(renderer.draw(eye, hands) == DrawResult::drawn && renderer.used_world_depth(),
            "Active viewport depth draw failed");
    const auto partial_pixels = host.pixels(eye);
    require(colored(partial_pixels, 256, false) == 0 && half_equal(partial_pixels, ordinary, true),
            "World depth sampling ignored active viewport dimensions");
    hands.depth.width = 257;
    host.clear(eye);
    require(renderer.draw(eye, hands) == DrawResult::drawn && !renderer.used_world_depth() &&
                host.pixels(eye) == ordinary,
            "Out-of-range depth viewport must leave gloves visible");
    hands.depth = {eye, 1, -0.1f, 256, 192};
    host.clear(eye);
    require(renderer.draw(eye, hands) == DrawResult::drawn && !renderer.used_world_depth() &&
                host.pixels(eye) == ordinary,
            "Unsupported world depth format must leave gloves visible");
    hands.depth = {partial.texture.value, 1, std::numeric_limits<float>::quiet_NaN(), 128, 96};
    host.clear(eye);
    require(renderer.draw(eye, hands) == DrawResult::drawn && !renderer.used_world_depth() &&
                host.pixels(eye) == ordinary,
            "Invalid world projection must leave gloves visible");
    WorldDepth multisample(host, DXGI_FORMAT_D32_FLOAT, 4);
    for (bool reverse : {false, true}) {
        hands.depth = {multisample.texture.value, reverse ? 0.f : 1.f, reverse ? 0.1f : -0.1f, 128, 96};
        multisample.split_samples(host, reverse);
        host.clear(eye);
        require(renderer.draw(eye, hands) == DrawResult::drawn && renderer.used_world_depth(),
                "Multisampled world depth unavailable");
        const auto hidden = host.pixels(eye);
        require(colored(hidden, 256, false) == 0 && colored(hidden, 256, true) == 0,
                "MSAA world depth must select the closest sample");
        multisample.clear(host, reverse ? 0.05f : 0.95f);
        host.clear(eye);
        require(renderer.draw(eye, hands) == DrawResult::drawn && renderer.used_world_depth() &&
                    host.pixels(eye) == ordinary,
                "Far MSAA world surface hid gloves");
    }
    hands.depth = {};
    host.clear(eye);
    require(renderer.draw(eye, hands) == DrawResult::drawn && !renderer.used_world_depth() &&
                host.pixels(eye) == ordinary,
            "Removing world depth retained a stale occlusion texture");
}

} // namespace

int main(int argc, char** argv) {
    try {
        using namespace witness::hands;
        check_skinning();
        Device host;
        Com<ID3D11Texture2D> eye;
        host.color(eye.put());
        host.clear(eye.value);
        Sentinels sentinel(host, eye.value);
        const State before(host.context.value);
        Renderer renderer;
        auto hands = frame();
        require(renderer.draw(eye.value, hands) == DrawResult::drawn, "First draw failed");
        require(before == State(host.context.value), "First draw leaked pipeline state");
        auto pixels = host.pixels(eye.value);
        require(colored(pixels, 256, false) > 200 && colored(pixels, 256, true) > 200,
                "Both gloves must render independently");
        if (argc == 2)
            save_bmp(argv[1], pixels);
        const auto open_pixels = pixels;
        hands.grip[0] = 1;
        host.clear(eye.value);
        require(renderer.draw(eye.value, hands) == DrawResult::drawn, "Left grip draw failed");
        pixels = host.pixels(eye.value);
        require(!half_equal(open_pixels, pixels, false) && half_equal(open_pixels, pixels, true),
                "Left grip must change only the left glove");
        hands.grip[1] = 1;
        host.clear(eye.value);
        require(renderer.draw(eye.value, hands) == DrawResult::drawn, "Both closed gloves failed");
        const auto closed_pixels = host.pixels(eye.value);
        require(colored(closed_pixels, 256, false) > 200 && colored(closed_pixels, 256, true) > 200 &&
                    !half_equal(open_pixels, closed_pixels, true),
                "Both closed gloves must remain visible and distinct from rest");
        if (argc == 2) {
            save_bmp((std::string(argv[1]) + ".closed.bmp").c_str(), closed_pixels);
            auto angled = hands;
            for (unsigned hand = 0; hand != 2; ++hand) {
                const float s = std::sin(hand ? -0.65f : 0.65f), c = std::cos(0.65f);
                const Mat4 rotation{c, 0, s, 0, 0, 1, 0, 0, -s, 0, c, 0, 0, 0, 0, 1};
                angled.hands[hand].clip_from_model = multiply(angled.hands[hand].clip_from_model, rotation);
                angled.hands[hand].normal_from_model = rotation;
            }
            host.clear(eye.value);
            require(renderer.draw(eye.value, angled) == DrawResult::drawn, "Angled grip capture failed");
            save_bmp((std::string(argv[1]) + ".angled-closed.bmp").c_str(), host.pixels(eye.value));
        }
        hands.grip = {0.5f, 0.5f};
        host.clear(eye.value);
        require(renderer.draw(eye.value, hands) == DrawResult::drawn, "Intermediate grip draw failed");
        pixels = host.pixels(eye.value);
        require(pixels != open_pixels && pixels != closed_pixels,
                "Grip interpolation must produce an intermediate pose");
        hands.grip = {-1, -1};
        host.clear(eye.value);
        require(renderer.draw(eye.value, hands) == DrawResult::drawn && host.pixels(eye.value) == open_pixels,
                "Reopening or low grip clamp did not restore exact rest pixels");
        hands.grip = {2, 2};
        host.clear(eye.value);
        require(renderer.draw(eye.value, hands) == DrawResult::drawn &&
                    host.pixels(eye.value) == closed_pixels,
                "High grip must clamp to closed");
        hands.grip = {0, std::numeric_limits<float>::infinity()};
        host.clear(eye.value);
        require(renderer.draw(eye.value, hands) == DrawResult::drawn, "Independent invalid grip draw failed");
        pixels = host.pixels(eye.value);
        require(half_equal(open_pixels, pixels, false) && colored(pixels, 256, true) == 0,
                "Nonfinite grip must hide only its hand");
        require(before == State(host.context.value), "Grip animation leaked pipeline state");
        hands = frame();
        host.clear(eye.value);
        hands.hands[0].valid = false;
        require(renderer.draw(eye.value, hands) == DrawResult::drawn, "Right-only draw failed");
        pixels = host.pixels(eye.value);
        require(colored(pixels, 256, false) == 0 && colored(pixels, 256, true) > 200,
                "Invalid left hand remained visible");
        hands.hands[1].clip_from_model[0] = std::numeric_limits<float>::quiet_NaN();
        require(renderer.draw(eye.value, hands) == DrawResult::skipped, "Nonfinite transform accepted");
        require(pixels == host.pixels(eye.value), "Skipped draw changed color");
        require(before == State(host.context.value), "Repeated draw leaked pipeline state");
        DrawResult other_thread = DrawResult::failed;
        std::thread worker([&] { other_thread = renderer.draw(eye.value, frame()); });
        worker.join();
        require(other_thread == DrawResult::skipped, "Cross-thread draw must skip");

        // Near left hand and mirrored right hand overlap. Their lighting differs;
        // a later, farther draw must not replace the nearer glove's pixels.
        host.context->ClearState();
        hands = frame();
        for (auto& hand : hands.hands) {
            hand.clip_from_model[3] = 0;
            hand.clip_from_model[11] = 0.3f;
        }
        hands.hands[1].clip_from_model[0] = -5;
        hands.hands[1].clip_from_model[11] = 0.7f;
        hands.hands[1].normal_from_model[5] = -1;
        hands.hands[1].valid = false;
        host.clear(eye.value);
        require(renderer.draw(eye.value, hands) == DrawResult::drawn, "Near depth reference failed");
        const auto near_pixels = host.pixels(eye.value);
        hands.hands[1].valid = true;
        host.clear(eye.value);
        require(renderer.draw(eye.value, hands) == DrawResult::drawn, "Self-depth draw failed");
        require(near_pixels == host.pixels(eye.value), "Far glove overwrote near glove");
        hands.hands[0].valid = false;
        host.clear(eye.value);
        require(renderer.draw(eye.value, hands) == DrawResult::drawn, "Far depth reference failed");
        require(near_pixels != host.pixels(eye.value), "Depth reference needs distinguishable colors");

        Com<ID3D11Texture2D> resized;
        host.color(resized.put(), 384, 256, 4);
        host.clear(resized.value);
        auto msaa_frame = frame();
        msaa_frame.grip = {1, 0.5f};
        require(renderer.draw(resized.value, msaa_frame) == DrawResult::drawn,
                "MSAA resized grip target failed");
        pixels = host.pixels(resized.value);
        require(colored(pixels, 384, false) > 400 && colored(pixels, 384, true) > 400,
                "MSAA glove pixels missing");
        Com<ID3D11Texture2D> srgb;
        host.color(srgb.put(), 256, 192, 1, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB);
        host.clear(srgb.value);
        require(renderer.draw(srgb.value, frame()) == DrawResult::drawn, "sRGB target failed");
        require(colored(host.pixels(srgb.value), 256, false) > 200, "sRGB glove pixels missing");
        Com<ID3D11Texture2D> unsupported;
        host.color(unsupported.put(), 64, 64, 1, DXGI_FORMAT_R32_FLOAT);
        require(renderer.draw(unsupported.value, frame()) == DrawResult::unsupported,
                "Unsupported target must skip safely");

        check_world_depth(host, eye.value, renderer);

        Device replacement(D3D11_CREATE_DEVICE_SINGLETHREADED);
        Com<ID3D11Texture2D> replacement_eye;
        replacement.color(replacement_eye.put());
        replacement.clear(replacement_eye.value);
        require(renderer.draw(replacement_eye.value, frame()) == DrawResult::drawn,
                "Single-threaded replacement device failed");
        require(colored(replacement.pixels(replacement_eye.value), 256, true) > 200,
                "Replacement device glove pixels missing");
        renderer.reset();
        require(renderer.draw(eye.value, frame()) == DrawResult::drawn, "Draw after reset failed");
        renderer.reset();
        std::cout
            << "Skinned gloves preserve rest, joint lengths and anchors; independent grip articulation, "
               "normalized normals, full state restoration, invalid input/thread hiding, private depth, "
               "native world depth copies/occlusion/reversed depth/MSAA/fallback, sRGB, target/device "
               "replacement, and reset passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
