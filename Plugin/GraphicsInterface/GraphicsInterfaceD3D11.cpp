﻿#include "pch.h"
#include "giInternal.h"

#ifdef giSupportD3D11
#include <d3d11.h>

using Microsoft::WRL::ComPtr;

namespace gi {

const int D3D11MaxStagingTextures = 32;

class GraphicsInterfaceD3D11 : public GraphicsInterface
{
public:
    GraphicsInterfaceD3D11(void *device);
    ~GraphicsInterfaceD3D11() override;
    void release() override;

    void* getDevicePtr() override;
    DeviceType getDeviceType() override;
    void sync() override;

    Result createTexture2D(void **dst_tex, int width, int height, TextureFormat format, const void *data, ResourceFlags flags) override;
    void releaseTexture2D(void *tex) override;
    Result readTexture2D(void *o_buf, size_t read_size, void *tex, int width, int height, TextureFormat format) override;
    Result writeTexture2D(void *o_tex, int width, int height, TextureFormat format, const void *buf, size_t write_size) override;

    Result createBuffer(void **dst_buf, size_t size, BufferType type, const void *data, ResourceFlags flags) override;
    void releaseBuffer(void *buf) override;
    Result readBuffer(void *dst, const void *src_buf, size_t read_size, BufferType type) override;
    Result writeBuffer(void *dst_buf, const void *src, size_t write_size, BufferType type) override;

private:
    void clearStagingTextures();
    void clearStagingBuffers();
    ID3D11Texture2D* getStagingTexture(int width, int height, TextureFormat format);
    ID3D11Buffer* getStagingBuffer(BufferType type, size_t size);

private:
    ComPtr<ID3D11Device> m_device = nullptr;
    ComPtr<ID3D11DeviceContext> m_context = nullptr;
    ComPtr<ID3D11Query> m_query_event = nullptr;
    std::map<uint64_t, ComPtr<ID3D11Texture2D>> m_staging_textures;
    std::array<ComPtr<ID3D11Buffer>, (int)BufferType::End> m_staging_buffers;
};


GraphicsInterface* CreateGraphicsInterfaceD3D11(void *device)
{
    if (!device) { return nullptr; }
    return new GraphicsInterfaceD3D11(device);
}

GraphicsInterfaceD3D11::GraphicsInterfaceD3D11(void *device)
    : m_device((ID3D11Device*)device)
{
    std::fill(m_staging_buffers.begin(), m_staging_buffers.end(), nullptr);
    if (m_device != nullptr)
    {
        m_device->GetImmediateContext(&m_context);

        D3D11_QUERY_DESC qdesc = {D3D11_QUERY_EVENT , 0};
        m_device->CreateQuery(&qdesc, &m_query_event);
    }
}

GraphicsInterfaceD3D11::~GraphicsInterfaceD3D11()
{
    clearStagingTextures();
    clearStagingBuffers();
}

void GraphicsInterfaceD3D11::release()
{
    delete this;
}

void GraphicsInterfaceD3D11::clearStagingTextures()
{
    m_staging_textures.clear();
}

void GraphicsInterfaceD3D11::clearStagingBuffers()
{
    for (auto& buf : m_staging_buffers) {
        buf.Reset();
    }
}

void* GraphicsInterfaceD3D11::getDevicePtr() { return m_device.Get(); }
DeviceType GraphicsInterfaceD3D11::getDeviceType() { return DeviceType::D3D11; }

void GraphicsInterfaceD3D11::sync()
{
    m_context->End(m_query_event.Get());
    while (m_context->GetData(m_query_event.Get(), nullptr, 0, 0) == S_FALSE) {
        std::this_thread::sleep_for(std::chrono::microseconds(100));
    }
}


static DXGI_FORMAT GetInternalFormatD3D11(TextureFormat fmt)
{
    switch (fmt)
    {
    case TextureFormat::RGBAu8:  return DXGI_FORMAT_R8G8B8A8_TYPELESS;

    case TextureFormat::RGBAf16: return DXGI_FORMAT_R16G16B16A16_FLOAT;
    case TextureFormat::RGf16:   return DXGI_FORMAT_R16G16_FLOAT;
    case TextureFormat::Rf16:    return DXGI_FORMAT_R16_FLOAT;

    case TextureFormat::RGBAf32: return DXGI_FORMAT_R32G32B32A32_FLOAT;
    case TextureFormat::RGf32:   return DXGI_FORMAT_R32G32_FLOAT;
    case TextureFormat::Rf32:    return DXGI_FORMAT_R32_FLOAT;

    case TextureFormat::RGBAi32: return DXGI_FORMAT_R32G32B32A32_SINT;
    case TextureFormat::RGi32:   return DXGI_FORMAT_R32G32_SINT;
    case TextureFormat::Ri32:    return DXGI_FORMAT_R32_SINT;
    }
    return DXGI_FORMAT_UNKNOWN;
}

ID3D11Texture2D* GraphicsInterfaceD3D11::getStagingTexture(int width, int height, TextureFormat format)
{
    if (m_staging_textures.size() >= D3D11MaxStagingTextures) {
        clearStagingTextures();
    }

    DXGI_FORMAT internal_format = GetInternalFormatD3D11(format);
    uint64_t hash = width + (height << 16) + ((uint64_t)internal_format << 32);
    {
        auto it = m_staging_textures.find(hash);
        if (it != m_staging_textures.end())
        {
            return it->second.Get();
        }
    }

    D3D11_TEXTURE2D_DESC desc = {
        (UINT)width, (UINT)height, 1, 1, internal_format, { 1, 0 },
        D3D11_USAGE_STAGING, 0, D3D11_CPU_ACCESS_READ | D3D11_CPU_ACCESS_WRITE, 0
    };
    ID3D11Texture2D *ret = nullptr;
    HRESULT hr = m_device->CreateTexture2D(&desc, nullptr, &ret);
    if (SUCCEEDED(hr))
    {
        m_staging_textures.insert(std::make_pair(hash, ret));
    }
    return ret;
}

static Result TranslateReturnCode(HRESULT hr)
{
    switch (hr) {
    case S_OK: return Result::OK;
    case E_OUTOFMEMORY: return Result::OutOfMemory;
    case E_INVALIDARG: return Result::InvalidParameter;
    }
    return Result::Unknown;
}

Result GraphicsInterfaceD3D11::createTexture2D(void **dst_tex, int width, int height, TextureFormat format, const void *data, ResourceFlags flags)
{
    size_t texel_size = GetTexelSize(format);
    DXGI_FORMAT internal_format = GetInternalFormatD3D11(format);

    D3D11_TEXTURE2D_DESC desc = {
        (UINT)width, (UINT)height, 1, 1, internal_format, { 1, 0 },
        D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE, 0, 0
    };
    if (flags & ResourceFlags::CPU_Write) {
        desc.Usage = D3D11_USAGE_DYNAMIC;
        desc.CPUAccessFlags |= D3D11_CPU_ACCESS_WRITE;
    }
    if (flags & ResourceFlags::CPU_Read) {
        desc.Usage = D3D11_USAGE_STAGING;
        desc.CPUAccessFlags |= D3D11_CPU_ACCESS_READ;
        desc.BindFlags = 0;
    }

    D3D11_SUBRESOURCE_DATA subr = {
        data,
        width * (UINT)texel_size,
        width * height * (UINT)texel_size,
    };

    ID3D11Texture2D *ret = nullptr;
    HRESULT hr = m_device->CreateTexture2D(&desc, data ? &subr : nullptr, &ret);
    if (FAILED(hr))
    {
        return TranslateReturnCode(hr);
    }
    *dst_tex = ret;
    return Result::OK;
}

void GraphicsInterfaceD3D11::releaseTexture2D(void *tex)
{
    if (tex) {
        ((ID3D11Texture2D*)tex)->Release();
    }
}

Result GraphicsInterfaceD3D11::readTexture2D(void *dst, size_t read_size, void *src_tex_, int width, int height, TextureFormat format)
{
    if (read_size == 0) { return Result::OK; }
    if (m_context == nullptr || src_tex_ == nullptr) { return Result::InvalidParameter; }

    int psize = GetTexelSize(format);

    auto *src_tex = (ID3D11Texture2D*)src_tex_;
    bool mappable = false;
    {
        D3D11_TEXTURE2D_DESC desc;
        src_tex->GetDesc(&desc);
        mappable = (desc.CPUAccessFlags & D3D11_CPU_ACCESS_READ) != 0;
    }

    Result ret = Result::OK;
    auto proc_read = [this, &ret](void *dst, size_t dst_size, ID3D11Texture2D *tex, int width, int height, TextureFormat format) {
        D3D11_MAPPED_SUBRESOURCE mapped = { 0 };
        auto hr = m_context->Map(tex, 0, D3D11_MAP_READ, 0, &mapped);
        if (FAILED(hr)) {
            giLogError("GraphicsInterfaceD3D11::readTexture2D(): Map() failed.\n");
            ret = TranslateReturnCode(hr);
        }
        else {
            auto *dst_pixels = (char*)dst;
            auto *src_pixels = (const char*)mapped.pData;
            int dst_pitch = width * GetTexelSize(format);
            int src_pitch = mapped.RowPitch;

            // pitch may not be same with (width * size_of_texel)
            if (dst_pitch == src_pitch)
            {
                memcpy(dst_pixels, src_pixels, dst_size);
            }
            else
            {
                for (int i = 0; i < height; ++i)
                {
                    memcpy(dst_pixels, src_pixels, dst_pitch);
                    dst_pixels += dst_pitch;
                    src_pixels += src_pitch;
                }
            }

            m_context->Unmap(tex, 0);
        }
    };

    if (mappable) {
        // read buffer data directly
        proc_read(dst, read_size, src_tex, width, height, format);
    }
    else {
        // copy texture data to staging texture and read from it
        auto *staging = getStagingTexture(width, height, format);
        m_context->CopyResource(staging, src_tex);
        // Map() doesn't wait completion of above CopyResource(). manual synchronization is required.
        sync();
        proc_read(dst, read_size, staging, width, height, format);
    }

    return ret;
}

Result GraphicsInterfaceD3D11::writeTexture2D(void *dst_tex_, int width, int height, TextureFormat format, const void *src, size_t write_size)
{
    if (write_size == 0) { return Result::OK; }
    if (!dst_tex_ || !src) { return Result::InvalidParameter; }

    auto *dst_tex = (ID3D11Texture2D*)dst_tex_;
    bool mappable = false;
    {
        D3D11_TEXTURE2D_DESC desc;
        dst_tex->GetDesc(&desc);
        mappable = (desc.CPUAccessFlags & D3D11_CPU_ACCESS_WRITE) != 0;
    }

    int psize = GetTexelSize(format);
    int pitch = psize * width;
    const size_t num_pixels = write_size / psize;

    Result ret = Result::OK;
    if (mappable) {
        D3D11_MAPPED_SUBRESOURCE mapped = { 0 };
        auto hr = m_context->Map(dst_tex, 0, D3D11_MAP_WRITE, 0, &mapped);
        if (FAILED(hr)) {
            hr = m_context->Map(dst_tex, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
        }
        if (FAILED(hr)) {
            ret = TranslateReturnCode(hr);
        }
        else {
            auto *dst_pixels = (char*)mapped.pData;
            auto *src_pixels = (const char*)src;
            int dst_pitch = mapped.RowPitch;
            int src_pitch = width * GetTexelSize(format);
            CopyRegion(dst_pixels, dst_pitch, src_pixels, src_pitch, height);

            m_context->Unmap(dst_tex, 0);
        }
    }
    else {
        D3D11_BOX box;
        box.left = 0;
        box.right = width;
        box.top = 0;
        box.bottom = ceildiv((UINT)num_pixels, (UINT)width);
        box.front = 0;
        box.back = 1;
        m_context->UpdateSubresource(dst_tex, 0, &box, src, pitch, 0);
    }
    return ret;
}



ID3D11Buffer* GraphicsInterfaceD3D11::getStagingBuffer(BufferType type, size_t size)
{
    auto& staging = m_staging_buffers[(int)type];
    size_t current_size = 0;
    if (staging) {
        D3D11_BUFFER_DESC desc;
        staging->GetDesc(&desc);
        current_size = desc.ByteWidth;
    }


    if (current_size != size) {
        staging.Reset();

        D3D11_BUFFER_DESC desc = { 0 };
        desc.ByteWidth = (UINT)size;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE | D3D11_CPU_ACCESS_READ;
        desc.Usage = D3D11_USAGE_STAGING;
        desc.BindFlags = 0;

        HRESULT hr = m_device->CreateBuffer(&desc, nullptr, &staging);
        if (FAILED(hr)) {
            giLogError("GraphicsInterfaceD3D11::findOrCreateStagingBuffer(): CreateBuffer() failed!\n");
            return nullptr;
        }
    }
    return staging.Get();
}

Result GraphicsInterfaceD3D11::createBuffer(void **dst_buf, size_t size, BufferType type, const void *data, ResourceFlags flags)
{
    D3D11_BUFFER_DESC desc = { 0 };
    desc.ByteWidth = (UINT)size;
    desc.Usage = D3D11_USAGE_DEFAULT;

    switch (type) {
    case BufferType::Index:
        desc.BindFlags = D3D11_BIND_INDEX_BUFFER;
        break;
    case BufferType::Vertex:
        desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        break;
    case BufferType::Constant:
        desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        break;
    case BufferType::Compute:
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
        break;
    }

    desc.CPUAccessFlags = 0;
    if (flags & ResourceFlags::CPU_Write) {
        desc.Usage = D3D11_USAGE_DYNAMIC;
        desc.CPUAccessFlags |= D3D11_CPU_ACCESS_WRITE;
    }
    if (flags & ResourceFlags::CPU_Read) {
        desc.Usage = D3D11_USAGE_STAGING;
        desc.CPUAccessFlags |= D3D11_CPU_ACCESS_READ;
        desc.BindFlags = 0;
    }

    ID3D11Buffer *ret = nullptr;
    HRESULT hr = m_device->CreateBuffer(&desc, nullptr, &ret);
    if (FAILED(hr)) {
        return TranslateReturnCode(hr);
    }
    *dst_buf = ret;
    return Result::OK;
}

void GraphicsInterfaceD3D11::releaseBuffer(void *buf)
{
    if (buf) {
        ((ID3D11Buffer*)buf)->Release();
    }
}

Result GraphicsInterfaceD3D11::readBuffer(void *dst, const void *src_buf_, size_t read_size, BufferType type)
{
    if (read_size == 0) { return Result::OK; }
    if (!dst || !src_buf_) { return Result::InvalidParameter; }

    auto *src_buf = (ID3D11Buffer*)src_buf_;
    bool mappable = false;
    {
        D3D11_BUFFER_DESC desc;
        src_buf->GetDesc(&desc);
        mappable = (desc.CPUAccessFlags & D3D11_CPU_ACCESS_READ) != 0;
    }


    Result ret = Result::OK;
    auto proc_read = [this, &ret](void *dst, ID3D11Buffer *buf, size_t size) {
        D3D11_MAPPED_SUBRESOURCE mapped;
        HRESULT hr = m_context->Map(buf, 0, D3D11_MAP_READ, 0, &mapped);
        if (FAILED(hr)) {
            giLogError("GraphicsInterfaceD3D11::readBuffer(): Map() failed.\n");
            ret = Result::Unknown;
        }
        else {
            memcpy(dst, mapped.pData, size);
            m_context->Unmap(buf, 0);
        }
    };

    if (mappable) {
        // read buffer data directly
        proc_read(dst, src_buf, read_size);
    }
    else {
        // copy buffer data to staging buffer and read from it
        auto *staging_buf = getStagingBuffer(type, read_size);
        m_context->CopyResource(staging_buf, src_buf);
        sync();
        proc_read(dst, staging_buf, read_size);
    }

    return ret;
}

Result GraphicsInterfaceD3D11::writeBuffer(void *dst_buf_, const void *src, size_t write_size, BufferType type)
{
    if (write_size == 0) { return Result::OK; }
    if (!dst_buf_ || !src) { return Result::InvalidParameter; }

    auto *dst_buf = (ID3D11Buffer*)dst_buf_;
    bool mappable = false;

    D3D11_BUFFER_DESC desc = { 0 };
    dst_buf->GetDesc(&desc);
    mappable = (desc.CPUAccessFlags & D3D11_CPU_ACCESS_WRITE) != 0;

    Result ret = Result::OK;
    if (mappable) {
        D3D11_MAPPED_SUBRESOURCE mapped;
        auto hr = m_context->Map(dst_buf, 0, D3D11_MAP_WRITE, 0, &mapped);
        if (FAILED(hr)) {
            hr = m_context->Map(dst_buf, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
        }
        if (FAILED(hr)) {
            giLogError("GraphicsInterfaceD3D11::writeBuffer(): Map() failed.\n");
            ret = Result::Unknown;
        }
        else {
            memcpy(mapped.pData, src, write_size);
            m_context->Unmap(dst_buf, 0);
        }
    }
    else {
        D3D11_BOX box;
        box.left = 0;
        box.right = (UINT)write_size;
        box.top = 0;
        box.bottom = 1;
        box.front = 0;
        box.back = 1;
        m_context->UpdateSubresource(dst_buf, 0, &box, src, (UINT)write_size, 0);
    }

    return ret;
}

} // namespace gi
#endif // giSupportD3D11