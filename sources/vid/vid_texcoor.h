#pragma once

#include "core/types.h"
#include <cstddef>
#include <cstdint>
#include "d3d8.h"

namespace as1
{
    class SPRITE;

    struct VID_TEXCOOR_VERTEX
    {

        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        float u = 0.0f;
        float v = 0.0f;
    };


    struct VID_TEXCOOR_SURFACE_VERTEX_WORDS
    {
        WORD screenX = 0;
        WORD screenY = 0;
        WORD depthCode = 0;
        WORD texU = 0;
        WORD texV = 0;
    };


    class VID_TEXCOOR_CORE
    {
    public:
        VID_TEXCOOR_CORE() = default;
        virtual ~VID_TEXCOOR_CORE();

        virtual VID_TEXCOOR_VERTEX* lockVertexBuffer();
        virtual int unlockVertexBuffer();
        virtual WORD* lockIndexBuffer();
        virtual int unlockIndexBuffer();
        virtual int drawTexcoorMesh(const SPRITE& sprite) const = 0;
    };

    class VID_TEXCOOR final : public VID_TEXCOOR_CORE
    {
    public:
        VID_TEXCOOR(int vertexCount, int indexCount);
        ~VID_TEXCOOR() override;

        VID_TEXCOOR(const VID_TEXCOOR&) = delete;
        VID_TEXCOOR& operator=(const VID_TEXCOOR&) = delete;

        VID_TEXCOOR_VERTEX* lockVertexBuffer() override;
        int unlockVertexBuffer() override;
        WORD* lockIndexBuffer() override;
        int unlockIndexBuffer() override;

        int drawTexcoorMesh(const SPRITE& sprite) const override;

        int vertexCount() const { return m_vertexCount; }
        int indexCount() const { return m_indexCount; }

        static constexpr std::size_t ObjectSize = 0x14u;
        IDirect3DIndexBuffer8* indexBuffer() const { return m_nativeIndexBuffer; }
        IDirect3DVertexBuffer8* vertexBuffer() const { return m_nativeVertexBuffer; }
        bool hasVertexBuffer() const { return m_nativeVertexBuffer != nullptr; }
        bool hasIndexBuffer() const { return m_nativeIndexBuffer != nullptr; }

    private:
        int m_vertexCount;
        int m_indexCount;
        IDirect3DIndexBuffer8* m_nativeIndexBuffer;
        IDirect3DVertexBuffer8* m_nativeVertexBuffer;
    };

}
