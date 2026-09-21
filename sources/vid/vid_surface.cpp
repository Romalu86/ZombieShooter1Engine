#include "vid/vid_surface.h"
#include "core/resource.h"
#include "core/application.h"
#include "graphics/gamma.h"
#include "core/log.h"
#include "graph.h"
#include "sprite.h"
#include "d3d8.h"
#include <array>
#include <new>
#include <cmath>
#include <limits>
#include <xmmintrin.h>

namespace as1
{
    namespace
    {
        __forceinline
        std::uint32_t framePointerTableBytes(std::int32_t frameCount) noexcept
        {


            const std::uint64_t product =
                static_cast<std::uint64_t>(static_cast<std::uint32_t>(frameCount)) * 4u;
            return product > 0xFFFFFFFFull
                ? 0xFFFFFFFFu
                : static_cast<std::uint32_t>(product);
        }

        __forceinline
        float interpolateSurfaceEffectCurve(const VID_SURFACE* owner,
                                            float position,
                                            int baseOffset) noexcept
        {
            int segment = 0;
            if (!std::isfinite(position) ||
                position < static_cast<float>(std::numeric_limits<std::int32_t>::min()) ||
                position >= 2147483648.0f)
            {
                segment = std::numeric_limits<std::int32_t>::min();
            }
            else
            {
                segment = static_cast<int>(std::trunc(position));
            }
            if (segment >= 7)
                return owner->weaponFloatAt(baseOffset + 7 * 4);

            const float first = owner->weaponFloatAt(baseOffset + segment * 4);
            const float second = owner->weaponFloatAt(baseOffset + (segment + 1) * 4);
            return (second - first) * (position - static_cast<float>(segment)) + first;
        }

        __forceinline
        int truncateSurfaceFloatToInt32(float value) noexcept
        {
            if (!std::isfinite(value) ||
                value < static_cast<float>(std::numeric_limits<std::int32_t>::min()) ||
                value >= 2147483648.0f)
                return std::numeric_limits<std::int32_t>::min();
            return static_cast<int>(std::trunc(value));
        }

        __forceinline
        float multiplySurfaceFloat(float lhs, float rhs) noexcept
        {
            return _mm_cvtss_f32(_mm_mul_ss(_mm_set_ss(lhs), _mm_set_ss(rhs)));
        }


        template <class T>
        __forceinline
        void releaseSurfacePointerTable(T**& table, int frameCount) noexcept
        {
            if (!table)
                return;

            if (frameCount > 0)
            {
                for (int earlier = 0; earlier < frameCount; ++earlier)
                {
                    for (int later = earlier + 1; later < frameCount; ++later)
                    {
                        if (table[earlier] == table[later])
                            table[later] = nullptr;
                    }
                }

                for (int frame = 0; frame < frameCount; ++frame)
                {
                    delete table[frame];
                }
            }

            ::operator delete(table);
            table = nullptr;
        }


    }

    VID_SURFACE::VID_SURFACE()
    {


        m_surfaceTexcoordOwners = nullptr;
        m_surfaceTextureOwners = nullptr;
    }

    VID_SURFACE::VID_SURFACE(const VID_SURFACE& other)
        : VID()
    {

        nextMirror = other.nextMirrorVid();
        const_cast<VID_SURFACE&>(other).nextMirror = this;

        layer = other.layer;
        type = other.formatFlags();
        noCadr = static_cast<short>(other.totalFrames());
        frameSpeedDefault = other.defaultFrameSpeed();
        setVidWidth(static_cast<short>(other.vidWidth()));
        setVidHeight(static_cast<short>(other.vidHeight()));

        m_surfaceTextureOwners = other.m_surfaceTextureOwners;
        m_surfaceTexcoordOwners = other.m_surfaceTexcoordOwners;
    }

    VID_SURFACE* VID_SURFACE::CreateMirror()
    {

        return new (std::nothrow) VID_SURFACE(*this);
    }


    VID_SURFACE::~VID_SURFACE()
    {


        if (!isMirrorChainOwner())
            return;

        const int frameCount = static_cast<int>(static_cast<short>(totalFrames()));
        releaseSurfacePointerTable(m_surfaceTextureOwners, frameCount);
        releaseSurfacePointerTable(m_surfaceTexcoordOwners, frameCount);
    }


    void VID_SURFACE::SetLayer()
    {
        layer = 8;
    }


    void VID_SURFACE::Load(RESOURCE* globalRes)
    {

        globalRes->read(&m_surfaceSourceFormat, sizeof(m_surfaceSourceFormat));

        const int frameCount = static_cast<int>(static_cast<std::int16_t>(totalFrames()));
        const std::uint32_t tableBytes32 = framePointerTableBytes(frameCount);
        const std::size_t tableBytes = static_cast<std::size_t>(tableBytes32);
        m_surfaceTextureOwners = static_cast<BASE_TEXTURE**>(::operator new(tableBytes));
        m_surfaceTexcoordOwners = static_cast<VID_TEXCOOR**>(::operator new(tableBytes));

        std::array<DWORD, 256> paletteDwords{};
        if ((formatFlags() & VID_TYPE_PALETTE) != 0)
        {
            if (globalRes->GoNext(RESOURCE::ResTypes::PALETTE) != 0)
                logVidResourceError(5, "PAL ", 0);
            else
            {
                globalRes->read(paletteDwords.data(), static_cast<unsigned>(sizeof(paletteDwords)));
            }
        }

        if (globalRes->GoNext(RESOURCE::ResTypes::DATA) != 0)
            logVidResourceError(5, "DATA", 0);

        if (frameCount <= 0)
            return;

        for (int frame = 0; frame < frameCount; ++frame)
        {
            WORD textureWidth = 0;
            WORD textureHeight = 0;
            globalRes->read(&textureWidth, sizeof(textureWidth));
            globalRes->read(&textureHeight, sizeof(textureHeight));

            const DWORD textureFlags =
                static_cast<DWORD>(((formatFlags() & 2u) | 4u) << 2u);


            BASE_TEXTURE* texture = new (std::nothrow) BASE_TEXTURE(
                static_cast<int>(textureWidth),
                static_cast<int>(textureHeight),
                m_surfaceSourceFormat,
                textureFlags,
                paletteDwords.data(),
                globalRes);
            m_surfaceTextureOwners[frame] = texture;

            DWORD vertexCount = 0;
            DWORD indexCount = 0;
            globalRes->read(&vertexCount, sizeof(vertexCount));
            globalRes->read(&indexCount, sizeof(indexCount));

            VID_TEXCOOR* texcoor = new (std::nothrow) VID_TEXCOOR(
                static_cast<int>(vertexCount),
                static_cast<int>(indexCount));
            m_surfaceTexcoordOwners[frame] = texcoor;


            VID_TEXCOOR_VERTEX* dstVertex = texcoor->lockVertexBuffer();
            for (int vertex = 0; vertex < static_cast<int>(vertexCount); ++vertex)
            {
                std::int16_t screenX = 0;
                std::int16_t screenY = 0;
                std::int16_t depthCode = 0;
                std::int16_t texU = 0;
                std::int16_t texV = 0;
                globalRes->read(&screenX, sizeof(screenX));
                globalRes->read(&screenY, sizeof(screenY));
                globalRes->read(&depthCode, sizeof(depthCode));
                globalRes->read(&texU, sizeof(texU));
                globalRes->read(&texV, sizeof(texV));

                dstVertex[vertex].x = static_cast<float>(screenX);


                dstVertex[vertex].y = multiplySurfaceFloat(
                    static_cast<float>(screenY), 1.414306640625f);
                dstVertex[vertex].z =
                    static_cast<float>(static_cast<int>(depthCode) - 1024) * 0.125f;


                dstVertex[vertex].u =
                    (static_cast<float>(texU) + 0.5f) / static_cast<float>(texture->width());
                dstVertex[vertex].v =
                    (static_cast<float>(texV) + 0.5f) / static_cast<float>(texture->height());
            }
            texcoor->unlockVertexBuffer();

            if (indexCount != 0u)
            {
                WORD* dstIndex = texcoor->lockIndexBuffer();
                globalRes->read(dstIndex, static_cast<unsigned>(indexCount * sizeof(WORD)));
                texcoor->unlockIndexBuffer();
            }

            globalRes->GoNextSub(RESOURCE::ResTypes::DATA);
        }
    }

    void VID_SURFACE::Draw(const SPRITE* sprite)
    {
        const DWORD auxFlags = runtimeAuxFlags();
        if ((auxFlags & 0x40u) != 0u)
            return;

        GRAPH* const graph = Graph;
        const WORD flags = formatFlags();
        const DWORD propertyFlags = properties();

        if ((propertyFlags & P_ALWAYSTOP) == 0u &&
            (flags & VID_TYPE_ZBUFFER) == 0u)
        {
            const auto* const appOwner = static_cast<const std::uint8_t*>(
                core::ApplicationOwner());
            const float cameraShiftX = *reinterpret_cast<const float*>(
                appOwner + core::application_layout::CameraShiftX);
            const float cameraShiftY = *reinterpret_cast<const float*>(
                appOwner + core::application_layout::CameraShiftY);
            const float projectedX = sprite->X() - cameraShiftX;
            const float projectedY = (sprite->Y() - sprite->Z()) - cameraShiftY;

            if (!(projectedX >= graph->viewportLeft() && projectedX < graph->viewportRight() &&
                  projectedY >= graph->viewportTop() && projectedY < graph->viewportBottom()))
            {
                return;
            }

            const int screenX = truncateSurfaceFloatToInt32(projectedX);
            const int screenY = truncateSurfaceFloatToInt32(projectedY);
            const int zInt = truncateSurfaceFloatToInt32(sprite->Z());
            const WORD* const depth = graph->softwareDepthBuffer();
            const int pitch = graph->softwareDepthPitch();
            const int depthLimit = static_cast<int>(
                static_cast<std::uint32_t>(zInt) * 8u + 0x400u);


            if (static_cast<int>(depth[screenX + screenY * pitch]) > depthLimit)
                return;
        }

        float scaleX = scaleXYZ.x;
        float scaleY = scaleXYZ.y;
        float scaleZ = scaleXYZ.z;
        float worldX = sprite->X();
        float worldY = sprite->Y();
        float worldZ = sprite->Z();

        if ((auxFlags & 0x02u) != 0u)
        {
            const float position = sprite->exDataEffectCurvePosition();
            scaleX *= interpolateSurfaceEffectCurve(this, position, 0x0E4);
            scaleY *= interpolateSurfaceEffectCurve(this, position, 0x104);
            scaleZ *= interpolateSurfaceEffectCurve(this, position, 0x124);
        }
        if ((auxFlags & 0x04u) != 0u)
        {
            const float position = sprite->exDataEffectCurvePosition();
            worldX += interpolateSurfaceEffectCurve(this, position, 0x144);
            worldY += interpolateSurfaceEffectCurve(this, position, 0x164);
            worldZ += interpolateSurfaceEffectCurve(this, position, 0x184);
        }



        D3DMATRIX world{};
        world.m[0][0] = scaleX * SPRITE::rawDirectionCos(0);
        world.m[0][1] = SPRITE::rawDirectionSinAux(0);
        world.m[1][0] = -SPRITE::rawDirectionSin(0);
        world.m[1][1] = scaleY * SPRITE::rawDirectionCosAux(0);
        world.m[2][2] = scaleZ;
        world.m[3][0] = worldX;
        world.m[3][1] = worldY;
        world.m[3][2] = worldZ;
        world.m[3][3] = 1.0f;

        IDirect3DDevice8* const transformDevice =
            static_cast<IDirect3DDevice8*>(graph->deviceHandle());
        const HRESULT transformResult =
            transformDevice->SetTransform(D3DTS_WORLD, &world);
        if (FAILED(transformResult))
        {
            LOG::ResourceError(
                "VID [%i-%s]", 8, "Transform world",
                static_cast<int>(transformResult), nvid(), name.c_str());
        }

        Gamma drawGamma{};
        drawGamma = sprite->GetGamma();
        if ((propertyFlags & P_GAMMA) == 0u)
        {
            drawGamma.setSaturatingAdd(drawGamma, graph->rawGammaPair());
        }

        const bool useTextureFactor = drawGamma.first != 0u || drawGamma.second != 0u;
        IDirect3DDevice8* const gammaDevice =
            static_cast<IDirect3DDevice8*>(graph->deviceHandle());
        if (useTextureFactor)
        {
            gammaDevice->SetTextureStageState(0u, D3DTSS_COLORARG2, D3DTA_TFACTOR);
            gammaDevice->SetTextureStageState(0u, D3DTSS_ALPHAARG2, D3DTA_TFACTOR);
            graph->setRenderStateCached(D3DRS_TEXTUREFACTOR, ~drawGamma.first);
        }

        if ((flags & VID_TYPE_ALPHA) != 0u)
            graph->SetAlphaBlend(5u, 6u);
        else
            graph->setRenderStateCached(0x1Bu, 0u);

        const int frame = sprite->currentFrame();
        BASE_TEXTURE* const texture = m_surfaceTextureOwners[frame];


        IDirect3DDevice8* const textureDevice =
            static_cast<IDirect3DDevice8*>(graph->deviceHandle());
        textureDevice->SetTexture(
            0u, static_cast<IDirect3DBaseTexture8*>(texture->nativeHandle()));
        m_surfaceTexcoordOwners[frame]->drawTexcoorMesh(*sprite);

        if (useTextureFactor)
        {
            IDirect3DDevice8* const resetDevice =
                static_cast<IDirect3DDevice8*>(graph->deviceHandle());
            resetDevice->SetTextureStageState(0u, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
            resetDevice->SetTextureStageState(0u, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
        }
    }

}
