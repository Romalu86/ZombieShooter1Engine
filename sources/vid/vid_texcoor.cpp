#include "vid/vid_texcoor.h"
#include "sprite.h"
#include "vid/vid.h"
#include "graph.h"
#include "core/log.h"
#include "core/file_logger.h"
#include <cstdint>
#include <algorithm>
#include <cmath>
#include <cstring>
#include "d3d8.h"

namespace as1
{

    VID_TEXCOOR_VERTEX* VID_TEXCOOR_CORE::lockVertexBuffer() { return nullptr; }
    int VID_TEXCOOR_CORE::unlockVertexBuffer() { return 0; }
    WORD* VID_TEXCOOR_CORE::lockIndexBuffer() { return nullptr; }
    int VID_TEXCOOR_CORE::unlockIndexBuffer() { return 0; }

    namespace
    {

        IDirect3DDevice8* currentVidTexcoorDevice()
        {
            return static_cast<IDirect3DDevice8*>(Graph ? Graph->deviceHandle() : nullptr);
        }

        int normalizedCount(int value)
        {
            return value > 0 ? value : 0;
        }
    }

    VID_TEXCOOR_CORE::~VID_TEXCOOR_CORE() = default;

    VID_TEXCOOR::VID_TEXCOOR(int vertexCount, int indexCount)
    {

        m_vertexCount = vertexCount;
        m_indexCount = indexCount;
        m_nativeIndexBuffer = nullptr;
        m_nativeVertexBuffer = nullptr;

        IDirect3DDevice8* device = currentVidTexcoorDevice();
        HRESULT result = device->CreateVertexBuffer(
            static_cast<UINT>(vertexCount) * 0x14u,
            8u,
            0x102u,
            static_cast<D3DPOOL>(1u),
            &m_nativeVertexBuffer);
        if (result != D3D_OK)
            LOG::ResourceError("MESH", 3, "VertexBuffer", static_cast<int>(result));

        if (indexCount != 0)
        {
            result = device->CreateIndexBuffer(
                static_cast<UINT>(indexCount) * 2u,
                8u,
                static_cast<D3DFORMAT>(0x65u),
                static_cast<D3DPOOL>(1u),
                &m_nativeIndexBuffer);
            if (result != D3D_OK)
                LOG::ResourceError("MESH", 3, "IndexBuffer", static_cast<int>(result));
        }
    }

    VID_TEXCOOR::~VID_TEXCOOR()
    {

        IDirect3DDevice8* device = currentVidTexcoorDevice();
        device->SetIndices(nullptr, 0u);
        device->SetStreamSource(0, nullptr, 0x14u);

        if (m_nativeIndexBuffer)
        {
            const ULONG releaseCount = m_nativeIndexBuffer->Release();
            if (releaseCount != 0u)
                LOG::ResourceError("MESH", 10, "Index release count !=0", static_cast<int>(releaseCount));
        }
        if (m_nativeVertexBuffer)
        {
            const ULONG releaseCount = m_nativeVertexBuffer->Release();
            if (releaseCount != 0u)
                LOG::ResourceError("MESH", 10, "Vertex release count !=0", static_cast<int>(releaseCount));
        }
    }

    VID_TEXCOOR_VERTEX* VID_TEXCOOR::lockVertexBuffer()
    {


        void* locked = nullptr;
        m_nativeVertexBuffer->Lock(0, 0, &locked, 0x2000u);
        return static_cast<VID_TEXCOOR_VERTEX*>(locked);
    }

    int VID_TEXCOOR::unlockVertexBuffer()
    {

        return static_cast<int>(m_nativeVertexBuffer->Unlock());
    }

    WORD* VID_TEXCOOR::lockIndexBuffer()
    {


        void* locked = nullptr;
        m_nativeIndexBuffer->Lock(0, 0, &locked, 0x2000u);
        return static_cast<WORD*>(locked);
    }

    int VID_TEXCOOR::unlockIndexBuffer()
    {

        return static_cast<int>(m_nativeIndexBuffer->Unlock());
    }

    int VID_TEXCOOR::drawTexcoorMesh(const SPRITE& sprite) const
    {
        (void)sprite;

        GRAPH* const graph = Graph;
        graph->setRenderStateCached(7u, 1u);
        graph->setRenderStateCached(0x17u, 5u);
        graph->setRenderStateCached(0x0Eu, 1u);
        graph->setRenderStateCached(0x16u, 1u);
        graph->setRenderStateCached(0x0Fu, 1u);
        graph->setRenderStateCached(0x18u, 0x20u);
        graph->setRenderStateCached(0x19u, 5u);

        IDirect3DDevice8* const device = static_cast<IDirect3DDevice8*>(graph->deviceHandle());
        HRESULT hr = D3D_OK;
        if (indexCount() != 0)
        {
            hr = device->SetIndices(m_nativeIndexBuffer, 0u);
            if (FAILED(hr))
                LOG::ResourceError("%s", 8, "Indices", static_cast<int>(hr), "MESH");
        }

        hr = device->SetStreamSource(0, m_nativeVertexBuffer, 0x14u);
        if (FAILED(hr))
            LOG::ResourceError("%s", 8, "Vertex", static_cast<int>(hr), "MESH");

        hr = device->SetVertexShader(0x102u);
        if (FAILED(hr))
            LOG::ResourceError("%s", 8, "VertexShader", static_cast<int>(hr), "MESH");

        if (indexCount() != 0)
        {
            hr = device->DrawIndexedPrimitive(
                D3DPT_TRIANGLELIST,
                0,
                static_cast<UINT>(vertexCount()),
                0,
                static_cast<UINT>(indexCount() / 3));
        }
        else
        {
            hr = device->DrawPrimitive(D3DPT_TRIANGLESTRIP, 0u, 2u);
        }

        if (FAILED(hr))
            if (g_fileLogger)
                logFileLoggerResourceError(g_fileLogger, "%s", 10, "Draw", static_cast<int>(hr), "MESH");

        graph->setRenderStateCached(0x0Fu, 0u);
        return static_cast<int>(hr);
    }

}
