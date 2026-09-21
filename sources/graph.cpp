#include "graph.h"

#include "core/application.h"
#include "core/configuration.h"
#include "core/resource.h"
#include "map.h"
#include "mouse.h"
#include "sprite.h"
#include "sprite_collector.h"
#include "vid/vid.h"
#include "vid/vid_software.h"
#include "vid/vid_software16.h"
#include "vid/vid_surface.h"
#include "vid/vid_texcoor.h"
#include "graphics/base_texture.h"
#include "graphics/gamma.h"
#include "graphics/color.h"
#include "images/picture.h"
#include "core/log.h"
#include "core/file_logger.h"
#include "zs1/zUserMngr.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <vector>
#include <memory>
#include <new>
#include <utility>
#include <array>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <xmmintrin.h>
#include <sstream>

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dshow.h>
#include "win/application_win.h"
#include "d3d8.h"
#include "win/dialog_item.h"


namespace as1
{
    int graphConvertFloatToInt32(float value) noexcept;

#define m_d3d8PresentParameters (*reinterpret_cast<D3DPRESENT_PARAMETERS8*>(m_presentParameters))
    DWORD g_color16RedMask = 0xF800u;
    DWORD g_color16GreenMask = 0x07E0u;
    DWORD g_color16RedShift = 8u;
    DWORD g_color16GreenShift = 3u;
    DWORD g_packedSoftwareDepth = 0u;
    WORD g_softwareDepthWordPrimary = 0u;
    WORD g_softwareDepthWordSecondary = 0u;
    const DWORD* g_softwarePaletteLookup = nullptr;
    int g_softwareClipLeft = 0;
    int g_softwareClipRight = 0;
    int g_softwareClipTop = 0;
    int g_softwareClipBottom = 0;


    GRAPH* Graph = nullptr;

    namespace
    {

        std::uint32_t start_squall = 0u;
        float old_w_speed = -1.0f;

        std::uint32_t g_groundSnowStart = 0u;
        std::uint32_t g_groundSnowAmount = 0u;


        std::uint32_t g_lightBufferToggle = 0u;

        DWORD graphFloatBits(float value)
        {
            DWORD bits = 0u;

            std::memcpy(&bits, &value, sizeof(bits));
            return bits;
        }


        struct GraphWeatherVertex44
        {
            float x;
            float y;
            float z;
            float rhw;
            DWORD color;
        };


        struct GraphWeatherLineParticle
        {
            GraphWeatherVertex44 vertex[2];
        };


        struct GraphWeatherCrossParticle
        {
            GraphWeatherVertex44 vertex[6];
        };


        std::array<GraphWeatherLineParticle, 250> g_lineParticles{};
        std::array<GraphWeatherCrossParticle, 1000> g_crossParticles{};
        int g_lineParticleCount = 0;
        int g_crossParticleCount = 0;
        DWORD g_lineParticleWindDirection = 999u;
        float g_lineParticleWindSpeed = 999.0f;
        DWORD g_crossParticleWindDirection = 999u;
        float g_crossParticleWindSpeed = 999.0f;
        float g_lineParticleWindX = 0.0f;
        float g_crossParticleWindX = 0.0f;
        float g_crossParticlePreviousNegatedCameraX = 0.0f;
        float g_crossParticlePreviousNegatedCameraY = 0.0f;

        constexpr float kGraphRand32767 = 0.000030518509f;
        constexpr float kGraphRand268435456 = 0.000000003725404f;
        constexpr float kGraphOneOver8192 = 0.00012207031f;
        constexpr float kGraphOneOver819_2 = 0.0012207031f;
        constexpr float kGraphOneOver200 = 0.0049999999f;

        float graphWeatherWindX(DWORD direction, float speed)
        {

            return SPRITE::rawDirectionSinUnchecked(direction) * speed * 1000.0f;
        }

        void graphTransformCrossParticle(GraphWeatherCrossParticle& particle)
        {
            const float middleX = (particle.vertex[1].x + particle.vertex[0].x) * 0.5f;
            const float middleY = (particle.vertex[1].y + particle.vertex[0].y) * 0.5f;
            for (GraphWeatherVertex44& vertex : particle.vertex)
            {
                const float oldX = vertex.x;
                const float oldY = vertex.y;
                vertex.x = middleX + oldY - middleY;
                vertex.y = oldX + middleY - middleX;
            }
        }

        int clampGraphByte(int value)
        {
            if (value < 0)
                return 0;
            if (value > 255)
                return 255;
            return value;
        }

        std::uint16_t packGraphIntensityWord(int high, int green, int low, bool r5g6b5)
        {
            high = clampGraphByte(high);
            green = clampGraphByte(green);
            low = clampGraphByte(low);
            const int highShift = r5g6b5 ? 8 : 7;
            const int greenShift = r5g6b5 ? 3 : 2;
            const int greenMask = r5g6b5 ? 0x07E0 : 0x03E0;
            return static_cast<std::uint16_t>(
                ((high & 0xF8) << highShift) |
                ((green << greenShift) & greenMask) |
                ((low >> 3) & 0x1F));
        }

        void buildGraphIntensityPalette(std::array<std::uint16_t, 256>& table, bool r5g6b5)
        {

            for (int v = 8, base = 0; base < 256; v += 8, base += 8)
            {
                table[base + 0] = packGraphIntensityWord(v - 8, v - 8, v - 8, r5g6b5);
                table[base + 1] = packGraphIntensityWord(v - 7, v - 7, v, r5g6b5);
                table[base + 2] = packGraphIntensityWord(v, v - 6, v - 6, r5g6b5);
                table[base + 3] = packGraphIntensityWord(v, v - 5, v, r5g6b5);

                table[base + 4] = packGraphIntensityWord(v - 4, v, v - 4, r5g6b5);
                table[base + 5] = packGraphIntensityWord(v - 3, v, v, r5g6b5);
                table[base + 6] = packGraphIntensityWord(v, v, v - 2, r5g6b5);
                table[base + 7] = packGraphIntensityWord(v, v, v, r5g6b5);
            }
        }

        bool graphSnowEdgeIntensity(std::uint16_t center, std::uint16_t neighbor, std::uint32_t& intensity)
        {
            const int delta = std::abs(static_cast<int>(center) - static_cast<int>(neighbor));
            if (delta > 6)
                return false;
            intensity = (g_groundSnowAmount * static_cast<std::uint32_t>(6 - delta)) >> 3u;
            return true;
        }

        int signedHalfTowardZero(int value)
        {
            const int sign = value < 0 ? -1 : 0;
            return (value - sign) >> 1;
        }

        int signedHalfFromFloatTowardZero(float value)
        {
            return signedHalfTowardZero(graphConvertFloatToInt32(value));
        }

        DWORD floatRaw(float value)
        {
            DWORD raw = 0;
            std::memcpy(&raw, &value, sizeof(raw));
            return raw;
        }

        float rawFloat(DWORD raw)
        {
            float value = 0.0f;
            std::memcpy(&value, &raw, sizeof(value));
            return value;
        }

        DWORD graphGrayRgb(DWORD value)
        {


            return value | ((value | (value << 8u)) << 8u);
        }

        DWORD graphScaleRgb(DWORD color, DWORD scale256)
        {

            const DWORD redProductRaw = static_cast<DWORD>(
                static_cast<std::uint64_t>(color) * static_cast<std::uint64_t>(scale256));
            std::int32_t redProductSigned = 0;
            std::memcpy(&redProductSigned, &redProductRaw, sizeof(redProductSigned));
            const DWORD red = static_cast<DWORD>(redProductSigned >> 8) & 0xFF0000u;
            const DWORD green = (((color & 0xFF00u) * scale256) >> 8u) & 0xFF00u;
            const DWORD blue = ((color & 0xFFu) * scale256) >> 8u;
            return red + green + blue;
        }

        void logMovieError(int errorCode, const char* detailText, int detailValue)
        {
            LOG::ResourceError("%s", errorCode, detailText, detailValue, "MOVIE");
        }

        std::string hresultText(const char* op, HRESULT hr)
        {
            std::ostringstream ss;
            ss << op << " failed: HRESULT=0x" << std::hex << static_cast<unsigned long>(hr);
            return ss.str();
        }

        IDirect3D8* graphD3D(void* p) { return static_cast<IDirect3D8*>(p); }
        IDirect3DDevice8* graphDevice(void* p) { return static_cast<IDirect3DDevice8*>(p); }

        void appendTextureArgumentName(char* destination, DWORD value)
        {
            const unsigned char low = static_cast<unsigned char>(value);
            if ((low & 0x20u) != 0u)
                std::strcat(destination, "alp-");
            if ((low & 0x10u) != 0u)
                std::strcat(destination, "inv-");

            switch (low & 0x0Fu)
            {
            case 2u: std::strcat(destination, "tex"); break;
            case 0u: std::strcat(destination, "dif"); break;
            case 4u: std::strcat(destination, "spec"); break;
            case 1u: std::strcat(destination, "cur"); break;
            case 3u: std::strcat(destination, "tfac"); break;
            default: break;
            }
        }

        const char* textureOperationName(DWORD value) noexcept
        {
            switch (value)
            {
            case 1u: return "dis";
            case 2u: return "sel1";
            case 3u: return "sel2";
            case 4u: return "mod";
            case 13u: return "tex_alpha";
            default: return "unknown";
            }
        }

        const char* buildTextureStageDebugText()
        {
            static char text[0x40C];
            IDirect3DDevice8* const device = graphDevice(Graph ? Graph->deviceHandle() : nullptr);
            DWORD value;

            std::strcpy(text, "Op=");
            device->GetTextureStageState(0u, static_cast<D3DTEXTURESTAGESTATETYPE>(1u), &value);
            std::strcat(text, textureOperationName(value));

            std::strcat(text, " Arg1=");
            device->GetTextureStageState(0u, static_cast<D3DTEXTURESTAGESTATETYPE>(2u), &value);
            appendTextureArgumentName(text, value);

            std::strcat(text, " Arg2=");
            device->GetTextureStageState(0u, static_cast<D3DTEXTURESTAGESTATETYPE>(3u), &value);
            appendTextureArgumentName(text, value);

            std::strcat(text, " AOp=");
            device->GetTextureStageState(0u, static_cast<D3DTEXTURESTAGESTATETYPE>(4u), &value);
            std::strcat(text, textureOperationName(value));

            std::strcat(text, " Arg1=");
            device->GetTextureStageState(0u, static_cast<D3DTEXTURESTAGESTATETYPE>(5u), &value);
            appendTextureArgumentName(text, value);

            std::strcat(text, " Arg2=");
            device->GetTextureStageState(0u, static_cast<D3DTEXTURESTAGESTATETYPE>(6u), &value);
            appendTextureArgumentName(text, value);
            return text;
        }

        void publishBaseTextureCaps(void* deviceRaw)
        {


            IDirect3DDevice8* const device = graphDevice(deviceRaw);
            D3DCAPS8 caps{};
            const HRESULT capsResult = device->GetDeviceCaps(&caps);
            if (capsResult < 0)
                LOG::ResourceError("TEXTURE", 9, "Caps", 0);

            BaseTextureCaps textureCaps;
            textureCaps.maxWidth = static_cast<int>(caps.MaxTextureWidth);
            textureCaps.maxHeight = static_cast<int>(caps.MaxTextureHeight);
            textureCaps.requireSquare = (caps.TextureCaps & 0x20u) != 0u;
            textureCaps.requirePowerOfTwo = (caps.TextureCaps & 0x2u) != 0u;
            textureCaps.conditionalNonPowerOfTwo = (caps.TextureCaps & 0x100u) != 0u;
            textureCaps.dynamicTextures = false;
            textureCaps.paletteTextures = false;
            textureCaps.alphaPaletteTextures = (caps.TextureCaps & 0x80u) != 0u;
            textureCaps.compressedFormatMask = BASE_TEXTURE::CompressedFormatMask();

            IDirect3DSurface8* backBuffer = nullptr;
            if (device->GetBackBuffer(0u, D3DBACKBUFFER_TYPE_MONO, &backBuffer) == D3D_OK && backBuffer)
            {
                D3DSURFACE_DESC8 backBufferDesc{};
                (void)backBuffer->GetDesc(&backBufferDesc);

                IDirect3D8* d3d = nullptr;
                if (device->GetDirect3D(&d3d) == D3D_OK && d3d)
                {
                    const auto supportsTextureFormat = [&](D3DFORMAT format) noexcept
                    {
                        return d3d->CheckDeviceFormat(
                            caps.AdapterOrdinal,
                            caps.DeviceType,
                            backBufferDesc.Format,
                            0u,
                            D3DRTYPE_TEXTURE,
                            format) == D3D_OK;
                    };

                    if (supportsTextureFormat(D3DFMT_P8))
                        textureCaps.paletteTextures = true;
                    else
                        textureCaps.alphaPaletteTextures = false;

                    if (supportsTextureFormat(static_cast<D3DFORMAT>(0x31545844u)))
                        textureCaps.compressedFormatMask |= 0x01u;
                    if (supportsTextureFormat(static_cast<D3DFORMAT>(0x33545844u)))
                        textureCaps.compressedFormatMask |= 0x04u;
                    if (supportsTextureFormat(static_cast<D3DFORMAT>(0x35545844u)))
                        textureCaps.compressedFormatMask |= 0x10u;

                    d3d->Release();
                }
                backBuffer->Release();
            }


            textureCaps.paletteTextures = false;
            textureCaps.alphaPaletteTextures = false;

            BASE_TEXTURE::ConfigureCaps(textureCaps);
        }

        void invokeBaseTextureVirtualDestructor(BASE_TEXTURE* texture) noexcept
        {
            if (!texture)
                return;
            void** const vtable = *reinterpret_cast<void***>(texture);
            using ScalarDeletingOwner = void* (__thiscall*)(void*, unsigned char);
            reinterpret_cast<ScalarDeletingOwner>(vtable[0])(texture, 1u);
        }

        void deleteBaseTextureThroughVirtualDestructor(BASE_TEXTURE*& texture) noexcept
        {
            if (!texture)
                return;
            invokeBaseTextureVirtualDestructor(texture);
            texture = nullptr;
        }
        IDirect3DSurface8* graphSurface(void* p) { return static_cast<IDirect3DSurface8*>(p); }
        IDirect3DTexture8* graphTexture(void* p) { return static_cast<IDirect3DTexture8*>(p); }

        int nextPowerOfTwo(int v)
        {
            int out = 1;
            while (out < v && out < 4096)
                out <<= 1;
            return out;
        }

        struct TextureVertex
        {
            float x, y, z, rhw;
            DWORD color;
            float u, v;
        };

        constexpr DWORD kTextureVertexFVF = D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1;


        bool isAllowedDisplayMode(const as1::core::StartupSettingsBlock& startupSettings,
                         DWORD width, DWORD height, DWORD colorBits)
        {

            const std::uint32_t* colorTable = startupSettings.allowedColorBits;
            int index = 0;
            DWORD value = colorTable[0];
            while (value != 0u)
            {
                if (colorBits == value)
                    break;
                ++index;
                value = colorTable[index];
            }

            if (startupSettings.allowedColorBits[index] == 0u || startupSettings.allowedWidths[0] == 0u)
                return false;

            index = 0;
            value = startupSettings.allowedWidths[0];
            while (value != 0u)
            {
                if (width == value && height == startupSettings.allowedHeights[index])
                    return true;
                ++index;
                value = startupSettings.allowedWidths[index];
            }
            return false;
        }


        DWORD chooseDepthStencilFormat(IDirect3D8* d3d, UINT adapter, D3DFORMAT format)
        {
            const D3DFORMAT depthFormats[] = {
                D3DFMT_D16,
                D3DFMT_D32,
                D3DFMT_D24X8,
                D3DFMT_D24S8
            };
            for (D3DFORMAT depth : depthFormats)
            {
                if (d3d->CheckDepthStencilMatch(adapter, D3DDEVTYPE_HAL, format, format, depth) == D3D_OK)
                    return static_cast<DWORD>(depth);
            }
            return 0;
        }

        float effectiveRenderZForStaticDraw(DWORD property, float spriteZ, float groundZ)
        {
            (void)property;
            (void)groundZ;
            return spriteZ;
        }
    }


    int graphConvertFloatToInt32(float value) noexcept
    {


        return _mm_cvtt_ss2si(_mm_set_ss(value));
    }

    bool graphFcompC3Equal(float lhs, float rhs) noexcept
    {

        return lhs == rhs || std::isnan(lhs) || std::isnan(rhs);
    }

    bool graphFcompC0(float lhs, float rhs) noexcept
    {

        return lhs < rhs || std::isnan(lhs) || std::isnan(rhs);
    }

    struct GraphTextGlyph
    {
        float left;
        float top;
        float right;
        float bottom;
    };

    namespace
    {
        constexpr DWORD kTextVertexFVF = D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1;
        constexpr float kTextVertexZ = 0.899999976f;
        constexpr float kTextVertexRhw = 1.0f;
        constexpr UINT kTextVertexBufferBytes = 0x20D0u;
        constexpr UINT kTextVertexStride = 0x1Cu;
        constexpr UINT kTextVertexBufferUsage = 0x208u;
        constexpr DWORD kTextVertexLockFlags = 0x2000u;

        struct GraphTextVertex
        {
            float x = 0.0f;
            float y = 0.0f;
            float z = kTextVertexZ;
            float rhw = kTextVertexRhw;
            DWORD color = 0xFFFFFFFFu;
            float u = 0.0f;
            float v = 0.0f;
        };
    }


    class CD3DFont
    {
    public:

        CD3DFont(const STRING& face, int sizeX, int sizeY, DWORD flags)
        {
            (void)initializeFontDescriptor(face.c_str(), sizeX, sizeY, flags);
        }

        CD3DFont* initializeFontDescriptor(const char* face, int sizeX, int sizeY, DWORD flags)
        {

            std::strcpy(m_face, face);
            m_sizeY = sizeY;
            m_sizeX = sizeX;
            m_flags = flags;
            m_device = nullptr;
            m_texture = nullptr;
            m_vertexBuffer = nullptr;
            m_savedStateBlock = 0u;
            m_textStateBlock = 0u;
            return this;
        }


        ~CD3DFont()
        {
            (void)InvalidateDeviceObjects();
            (void)DeleteDeviceObjects();
        }

        const char* face() const { return m_face; }
        int sizeX() const { return m_sizeX; }
        int sizeY() const { return m_sizeY; }
        DWORD flags() const { return m_flags; }
        bool isReady() const
        {
            return m_vertexBuffer != nullptr;
        }


        int InvalidateDeviceObjects()
        {
            if (m_vertexBuffer)
            {
                m_vertexBuffer->Release();
                m_vertexBuffer = nullptr;
            }
            if (m_device)
            {
                if (m_savedStateBlock)
                    m_device->DeleteStateBlock(m_savedStateBlock);
                if (m_textStateBlock)
                    m_device->DeleteStateBlock(m_textStateBlock);
            }
            m_savedStateBlock = 0u;
            m_textStateBlock = 0u;
            return 0;
        }


        int DeleteDeviceObjects()
        {
            if (m_texture)
            {
                m_texture->Release();
                m_texture = nullptr;
            }
            m_device = nullptr;
            return 0;
        }

        int releaseFontResources()
        {
            InvalidateDeviceObjects();
            return DeleteDeviceObjects();
        }

        void release() { (void)releaseFontResources(); }


        int InitDeviceObjects(IDirect3DDevice8* device, std::string* status)
        {
            m_device = device;

            m_textureWidth = atlasSizeForHeight(m_sizeY);
            m_textureHeight = m_textureWidth;
            m_scale = 1.0f;

            D3DCAPS8 caps;
            device->GetDeviceCaps(&caps);
            if (static_cast<DWORD>(m_textureWidth) > caps.MaxTextureWidth)
            {
                m_scale = static_cast<float>(caps.MaxTextureWidth) / static_cast<float>(m_textureWidth);
                m_textureWidth = static_cast<int>(caps.MaxTextureWidth);
                m_textureHeight = static_cast<int>(caps.MaxTextureWidth);
            }

            HRESULT hr = device->CreateTexture(
                static_cast<UINT>(m_textureWidth),
                static_cast<UINT>(m_textureHeight),
                1,
                0,
                D3DFMT_A4R4G4B4,
                D3DPOOL_MANAGED,
                &m_texture);
            if (FAILED(hr) || !m_texture)
            {
                if (status)
                    *status = hresultText("font texture", hr);
                return static_cast<int>(hr);
            }

            if (!buildAtlas(status))
                return static_cast<int>(E_FAIL);

            return 0;
        }


        int RestoreDeviceObjects()
        {
            if (!m_device)
                return static_cast<int>(E_FAIL);

            HRESULT hr = m_device->CreateVertexBuffer(
                kTextVertexBufferBytes,
                kTextVertexBufferUsage,
                0,
                D3DPOOL_DEFAULT,
                &m_vertexBuffer);
            if (FAILED(hr) || !m_vertexBuffer)
                return static_cast<int>(hr);

            if (!createStateBlocks(nullptr))
                return static_cast<int>(E_FAIL);

            return 0;
        }

        bool draw(float x, float y, DWORD color, const char* text, DWORD flags, std::string* status)
        {
            if (!m_device)
                return false;
            if (m_savedStateBlock)
                m_device->CaptureStateBlock(m_savedStateBlock);
            if (m_textStateBlock)
                m_device->ApplyStateBlock(m_textStateBlock);

            m_device->SetVertexShader(kTextVertexFVF);
            m_device->SetPixelShader(0u);
            m_device->SetStreamSource(0, m_vertexBuffer, kTextVertexStride);
            m_device->SetTexture(0, m_texture);
            if ((flags & 4u) != 0)
            {
                D3D8SetSamplerState(m_device, 0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
                D3D8SetSamplerState(m_device, 0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
            }

            void* raw = nullptr;
            HRESULT hr = m_vertexBuffer->Lock(0, 0, &raw, kTextVertexLockFlags);
            if (FAILED(hr) || !raw)
            {
                if (status)
                    *status = hresultText("font vertex lock", hr);
                return false;
            }

            GraphTextVertex* vertices = reinterpret_cast<GraphTextVertex*>(raw);
            UINT primitiveCount = 0;
            UINT vertexCount = 0;
            bool ok = true;
            float cursorX = x;
            float cursorY = y;
            const float lineStep = (m_glyphs[0].bottom - m_glyphs[0].top) * static_cast<float>(m_textureHeight);

            for (const unsigned char* p = reinterpret_cast<const unsigned char*>(text); *p; ++p)
            {
                const unsigned char ch = *p;
                if (ch == '\n')
                {
                    cursorX = x;
                    cursorY += lineStep;
                }
                const GraphTextGlyph& g = m_glyphs[ch];
                const float glyphW = (g.right - g.left) * static_cast<float>(m_textureWidth) / m_scale;
                const float glyphH = (g.bottom - g.top) * static_cast<float>(m_textureHeight) / m_scale;
                if (ch == ' ')
                {
                    cursorX += glyphW;
                    continue;
                }

                const float x0 = cursorX - 0.5f;
                const float y0 = cursorY - 0.5f;
                const float x1 = cursorX + glyphW - 0.5f;
                const float y1 = cursorY + glyphH - 0.5f;
                const float u0 = g.left;
                const float v0 = g.top;
                const float u1 = g.right;
                const float v1 = g.bottom;
                GraphTextVertex quad[6] = {
                    {x0, y0, kTextVertexZ, kTextVertexRhw, color, u0, v0},
                    {x1, y0, kTextVertexZ, kTextVertexRhw, color, u1, v0},
                    {x1, y1, kTextVertexZ, kTextVertexRhw, color, u1, v1},
                    {x0, y0, kTextVertexZ, kTextVertexRhw, color, u0, v0},
                    {x1, y1, kTextVertexZ, kTextVertexRhw, color, u1, v1},
                    {x0, y1, kTextVertexZ, kTextVertexRhw, color, u0, v1},
                };
                std::copy(quad, quad + 6, vertices + vertexCount);
                vertexCount += 6u;
                primitiveCount += 2u;
                if (primitiveCount > 98u)
                {
                    hr = flush(primitiveCount, status);
                    if (FAILED(hr))
                    {
                        ok = false;
                        break;
                    }
                    raw = nullptr;
                    hr = m_vertexBuffer->Lock(0, 0, &raw, kTextVertexLockFlags);
                    if (FAILED(hr) || !raw)
                    {
                        if (status)
                            *status = hresultText("font vertex lock", hr);
                        ok = false;
                        break;
                    }
                    vertices = reinterpret_cast<GraphTextVertex*>(raw);
                    primitiveCount = 0;
                    vertexCount = 0;
                }
                cursorX += glyphW;
            }

            HRESULT unlockHr = m_vertexBuffer->Unlock();
            if (FAILED(unlockHr) && ok)
            {
                if (status)
                    *status = hresultText("font vertex unlock", unlockHr);
                ok = false;
            }
            if (ok && primitiveCount > 0)
            {
                hr = m_device->DrawPrimitive(D3DPT_TRIANGLELIST, 0, primitiveCount);
                if (FAILED(hr))
                {
                    if (status)
                        *status = hresultText("font draw", hr);
                    ok = false;
                }
            }

            if (m_savedStateBlock)
                m_device->ApplyStateBlock(m_savedStateBlock);
            return ok;
        }


        int DrawText(float x, float y, DWORD color, const char* text, DWORD flags)
        {
            return draw(x, y, color, text, flags, nullptr)
                ? 0
                : static_cast<int>(E_FAIL);
        }

    private:
        static int atlasSizeForHeight(int sizeY)
        {
            if (sizeY > 40)
                return 1024;
            if (sizeY > 20)
                return 512;
            return 256;
        }

        bool buildAtlas(std::string* status)
        {
            BITMAPINFO bmi{};
            bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            bmi.bmiHeader.biWidth = m_textureWidth;
            bmi.bmiHeader.biHeight = -m_textureHeight;
            bmi.bmiHeader.biPlanes = 1;
            bmi.bmiHeader.biBitCount = 32;
            bmi.bmiHeader.biCompression = BI_RGB;

            void* bits = nullptr;
            HDC dc = CreateCompatibleDC(nullptr);
            HBITMAP bitmap = CreateDIBSection(dc, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);

            SelectObject(dc, bitmap);
            SetMapMode(dc, MM_TEXT);
            const int dpiY = GetDeviceCaps(dc, LOGPIXELSY);
            const int dpiX = GetDeviceCaps(dc, LOGPIXELSX);
            const int fontHeight = -MulDiv(m_sizeY, static_cast<int>(static_cast<float>(dpiY) * m_scale), 72);
            const int fontWidth = -MulDiv(m_sizeX, static_cast<int>(static_cast<float>(dpiX) * m_scale), 72);
            const int weight = (m_flags & 1u) ? FW_BOLD : FW_NORMAL;
            const BOOL italic = (m_flags & 2u) ? TRUE : FALSE;
            const DWORD pitchAndFamily = 2u - ((m_flags & 8u) != 0u ? 1u : 0u);
            HFONT font = CreateFontA(fontHeight, fontWidth, 0, 0, weight, italic, FALSE, FALSE,
                                     DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                     ANTIALIASED_QUALITY, pitchAndFamily, m_face);
            if (!font)
                return false;
            SelectObject(dc, font);
            SetTextColor(dc, RGB(255, 255, 255));
            SetBkColor(dc, RGB(0, 0, 0));
            SetTextAlign(dc, TA_TOP | TA_LEFT);

            int penX = 0;
            int penY = 0;
            for (int ch = 0; ch < 256; ++ch)
            {
                char c = static_cast<char>(ch);
                SIZE extent{};
                GetTextExtentPoint32A(dc, &c, 1, &extent);
                if (penX + extent.cx + 1 > m_textureWidth)
                {
                    penX = 0;
                    penY += extent.cy + 1;
                }
                ExtTextOutA(dc, penX, penY, ETO_OPAQUE, nullptr, &c, 1, nullptr);
                GraphTextGlyph& glyph = m_glyphs[static_cast<unsigned char>(ch)];
                glyph.left = static_cast<float>(penX) / static_cast<float>(m_textureWidth);
                glyph.top = static_cast<float>(penY) / static_cast<float>(m_textureHeight);
                glyph.right = static_cast<float>(penX + extent.cx) / static_cast<float>(m_textureWidth);
                glyph.bottom = static_cast<float>(penY + extent.cy) / static_cast<float>(m_textureHeight);
                penX += extent.cx + 1;
            }

            D3DLOCKED_RECT locked;
            m_texture->LockRect(0, &locked, nullptr, 0);
            const DWORD* src = static_cast<const DWORD*>(bits);
            for (int y = 0; y < m_textureHeight; ++y)
            {
                std::uint16_t* dst = reinterpret_cast<std::uint16_t*>(static_cast<BYTE*>(locked.pBits) + y * locked.Pitch);
                for (int x = 0; x < m_textureWidth; ++x)
                {
                    const DWORD pixel = src[static_cast<std::size_t>(y) * static_cast<std::size_t>(m_textureWidth) + static_cast<std::size_t>(x)];
                    const BYTE alpha = static_cast<BYTE>((pixel >> 4) & 0x0Fu);
                    dst[x] = alpha ? static_cast<std::uint16_t>((alpha << 12) | 0x0FFFu) : 0;
                }
            }
            m_texture->UnlockRect(0);

            DeleteObject(bitmap);
            DeleteDC(dc);
            DeleteObject(font);
            (void)status;
            return true;
        }

        bool createStateBlocks(std::string* status)
        {
            HRESULT hr = m_device->BeginStateBlock();
            if (FAILED(hr))
            {
                if (status)
                    *status = hresultText("font state begin", hr);
                return false;
            }
            applyTextRenderState();
            hr = m_device->EndStateBlock(&m_savedStateBlock);
            if (FAILED(hr))
            {
                if (status)
                    *status = hresultText("font state end", hr);
                return false;
            }

            hr = m_device->BeginStateBlock();
            if (FAILED(hr))
            {
                if (status)
                    *status = hresultText("font state begin", hr);
                return false;
            }
            applyTextRenderState();
            hr = m_device->EndStateBlock(&m_textStateBlock);
            if (FAILED(hr))
            {
                if (status)
                    *status = hresultText("font state end", hr);
                return false;
            }
            return true;
        }

        void applyTextRenderState()
        {
            m_device->SetTexture(0, m_texture);
            m_device->SetRenderState(static_cast<D3DRENDERSTATETYPE>(7), (m_flags & 4u) != 0u ? 1u : 0u);
            m_device->SetRenderState(static_cast<D3DRENDERSTATETYPE>(27), 1u);
            m_device->SetRenderState(static_cast<D3DRENDERSTATETYPE>(19), 5u);
            m_device->SetRenderState(static_cast<D3DRENDERSTATETYPE>(20), 6u);
            m_device->SetRenderState(static_cast<D3DRENDERSTATETYPE>(15), 1u);
            m_device->SetRenderState(static_cast<D3DRENDERSTATETYPE>(24), 8u);
            m_device->SetRenderState(static_cast<D3DRENDERSTATETYPE>(25), 7u);
            m_device->SetRenderState(static_cast<D3DRENDERSTATETYPE>(8), 3u);
            m_device->SetRenderState(static_cast<D3DRENDERSTATETYPE>(22), 3u);
            m_device->SetRenderState(static_cast<D3DRENDERSTATETYPE>(52), 0u);
            m_device->SetRenderState(static_cast<D3DRENDERSTATETYPE>(136), 1u);
            m_device->SetRenderState(static_cast<D3DRENDERSTATETYPE>(40), 0u);
            m_device->SetRenderState(static_cast<D3DRENDERSTATETYPE>(152), 0u);
            m_device->SetRenderState(static_cast<D3DRENDERSTATETYPE>(151), 0u);
            m_device->SetRenderState(static_cast<D3DRENDERSTATETYPE>(167), 0u);
            m_device->SetRenderState(static_cast<D3DRENDERSTATETYPE>(28), 0u);

            m_device->SetTextureStageState(0, static_cast<D3DTEXTURESTAGESTATETYPE>(1), 4u);
            m_device->SetTextureStageState(0, static_cast<D3DTEXTURESTAGESTATETYPE>(2), 2u);
            m_device->SetTextureStageState(0, static_cast<D3DTEXTURESTAGESTATETYPE>(3), 0u);
            m_device->SetTextureStageState(0, static_cast<D3DTEXTURESTAGESTATETYPE>(4), 4u);
            m_device->SetTextureStageState(0, static_cast<D3DTEXTURESTAGESTATETYPE>(5), 2u);
            m_device->SetTextureStageState(0, static_cast<D3DTEXTURESTAGESTATETYPE>(6), 0u);
            D3D8SetSamplerState(m_device, 0, D3DSAMP_MINFILTER, D3DTEXF_POINT);
            D3D8SetSamplerState(m_device, 0, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
            D3D8SetSamplerState(m_device, 0, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
            m_device->SetTextureStageState(0, D3DTSS_TEXCOORDINDEX, 0u);
            m_device->SetTextureStageState(0, static_cast<D3DTEXTURESTAGESTATETYPE>(24), 0u);
            m_device->SetTextureStageState(1, static_cast<D3DTEXTURESTAGESTATETYPE>(1), 1u);
            m_device->SetTextureStageState(1, static_cast<D3DTEXTURESTAGESTATETYPE>(4), 1u);
        }

        HRESULT flush(UINT primitiveCount, std::string* status)
        {
            HRESULT unlockHr = m_vertexBuffer->Unlock();
            if (FAILED(unlockHr))
            {
                if (status)
                    *status = hresultText("font vertex unlock", unlockHr);
                return unlockHr;
            }
            HRESULT drawHr = S_OK;
            if (primitiveCount > 0)
                drawHr = m_device->DrawPrimitive(D3DPT_TRIANGLELIST, 0, primitiveCount);
            if (status && FAILED(drawHr))
                *status = hresultText("font draw", drawHr);
            return drawHr;
        }

        char m_face[80];
        int m_sizeY;
        int m_sizeX;
        DWORD m_flags;
        IDirect3DDevice8* m_device;
        IDirect3DTexture8* m_texture;
        IDirect3DVertexBuffer8* m_vertexBuffer;
        int m_textureWidth;
        int m_textureHeight;
        float m_scale;
        GraphTextGlyph m_glyphs[256];
        DWORD m_savedStateBlock;
        DWORD m_textStateBlock;
    };


    namespace
    {
        void destroyCD3DFont(CD3DFont*& font)
        {
            if (!font)
                return;
            font->~CD3DFont();
            ::operator delete(font);
            font = nullptr;
        }

        int displayFormatBits(DWORD format) noexcept;
    }

    CD3DFont* createVidFontOwner(const STRING& face, int sizeX, int sizeY)
    {
        void* const storage = ::operator new(0x107Cu);
        CD3DFont* const owner = storage ? new (storage) CD3DFont(face, sizeX, sizeY, 8u) : nullptr;
        if (!owner)
            return nullptr;
        IDirect3DDevice8* const device = graphDevice(Graph ? Graph->deviceHandle() : nullptr);
        if (device)
        {
            (void)owner->InitDeviceObjects(device, nullptr);
            (void)owner->RestoreDeviceObjects();
        }
        return owner;
    }

    void destroyVidFontOwner(CD3DFont*& owner) noexcept
    {
        destroyCD3DFont(owner);
    }

    int invalidateVidFontOwner(CD3DFont* owner) noexcept
    {
        return owner ? owner->InvalidateDeviceObjects() : 0;
    }

    int restoreVidFontOwner(CD3DFont* owner) noexcept
    {
        return owner ? owner->RestoreDeviceObjects() : 0;
    }

    bool drawVidFontOwner(CD3DFont* owner, float x, float y, DWORD color,
                                const char* text, DWORD flags) noexcept
    {
        return owner ? owner->draw(x, y, color, text ? text : "", flags, nullptr) : false;
    }


    GRAPH* GRAPH::initializeGraphState(const as1::core::StartupSettingsBlock& startupSettings)
    {


        for (DD_DRIVER& record : m_adapterRecords)
        {
            record.displayModeCount = 0u;
            record.description[0] = '\0';
            record.capabilityFlags &= 0xFFFFFFF0u;
        }

        m_effectGammaPair.first = 0u;
        m_effectGammaPair.second = 0u;
        m_gammaPair.first = 0u;
        m_gammaPair.second = 0u;

        DWORD flags = m_graphFlags;
        flags = (flags & ~0x00000400u) |
                ((startupSettings.flags & 0x1u) != 0u ? 0x00000400u : 0u);
        flags = (flags & ~0x00000101u) |
                ((startupSettings.flags & 0x2u) != 0u ? 0x00000100u : 0u);
        flags &= ~0x00004000u;
        m_graphFlags = flags;

        m_direct3D = nullptr;
        m_device = nullptr;
        m_textFont = nullptr;
        m_lightBuffer = nullptr;
        m_hiBuffer = nullptr;
        m_alphaBuffer = nullptr;
        m_tempBuffer = nullptr;
        m_backBuffer = nullptr;
        m_softwareDepthBuffer = nullptr;
        m_lockedBackBufferPixels = nullptr;
        m_renderFlags = 0u;
        m_deviceLifecycleState = 0u;
        m_windDirection = 0xDCu;
        m_windSpeed = 20.0f;


        Effect(0, 0, 0, 0);

        m_direct3D = Direct3DCreate8(D3D8_SDK_VERSION);
        if (!m_direct3D)
        {
            logAndShowError(g_fileLogger, "Can't create Direct3D8");
            return this;
        }

        IDirect3D8* const d3d = graphD3D(m_direct3D);
        m_adapterCount = 0u;
        const UINT adapterCount = d3d->GetAdapterCount();
        for (UINT adapter = 0; adapter < adapterCount; ++adapter)
        {


            buildAdapterRecord(m_adapterRecords[adapter], d3d,
                               static_cast<int>(adapter), startupSettings);
            ++m_adapterCount;
        }

        m_selectedAdapterIndex = 0;
        m_sizeX = static_cast<float>(startupSettings.screenWidth);
        m_sizeY = static_cast<float>(startupSettings.screenHeight);

        flags = m_graphFlags;
        flags = (flags & ~0x00000002u) |
                (startupSettings.colorDepth == 32 ? 0x00000002u : 0u);
        flags = (flags & ~0x00000080u) |
                (startupSettings.fullscreen != 0 ? 0x00000080u : 0u);
        m_graphFlags = flags;

        if (m_adapterCount == 0u)
            m_selectedAdapterIndex = 0;
        return this;
    }


    int GRAPH::rebuildTextFont(const STRING& face, int sizeX, int sizeY)
    {

        CD3DFont* const oldFont = m_textFont;
        if (oldFont)
        {
            (void)oldFont->releaseFontResources();
            oldFont->~CD3DFont();
            ::operator delete(oldFont);
        }

        void* const storage = ::operator new(0x107Cu);
        CD3DFont* const replacement = storage ? new (storage) CD3DFont(face, sizeX, sizeY, 8u) : nullptr;
        m_textFont = replacement;
        (void)m_textFont->InitDeviceObjects(graphDevice(m_device), nullptr);
        return m_textFont->RestoreDeviceObjects();
    }


    STRING GRAPH::D3DFormatToString(DWORD format)
    {


        switch (format)
        {
        case 20: return STRING("R8G8B8");
        case 21: return STRING("A8R8G8B8");
        case 22: return STRING("X8R8G8B8");
        case 23: return STRING("R5G6B5");
        case 24: return STRING("X1R5G5B5");
        case 25: return STRING("A1R5G5B5");
        case 26: return STRING("A4R4G4B4");
        case 28: return STRING("A8");
        case 40: return STRING("A8P8");
        case 41: return STRING("P8");
        case 50: return STRING("L8");
        case 51: return STRING("A8L8");
        case 52: return STRING("A4L4");
        case 60: return STRING("V8U8");
        case 70: return STRING("D16_LOCKABLE");
        case 71: return STRING("D32");
        case 73: return STRING("D15S1");
        case 75: return STRING("D24S8");
        case 77: return STRING("D24X8");
        case 80: return STRING("D16");
        case 100: return STRING("VERTEXDATA");
        case 101: return STRING("INDEX16");
        case 102: return STRING("INDEX32");
        case 0x31545844u: return STRING("DXT1");
        case 0x32545844u: return STRING("DXT2");
        case 0x33545844u: return STRING("DXT3");
        case 0x34545844u: return STRING("DXT4");
        case 0x35545844u: return STRING("DXT5");
        default: return STRING("Unknown");
        }
    }

    namespace
    {
        int displayFormatBits(DWORD format) noexcept
        {

            switch (format)
            {
            case 0x15u:
            case 0x16u:
                return 32;
            case 0x17u:
            case 0x18u:
            case 0x19u:
            case 0x1Au:
                return 16;
            case 0x14u:
                return 24;
            case 0x29u:
                return 8;
            case 0x31545844u:
                return 4;
            case 0x33545844u:
            case 0x35545844u:
                return 8;
            default:
                return 0;
            }
        }
    }


    int DD_DRIVER::GetMode(int width, int height, int bitsPerPixel) const noexcept
    {

        for (DWORD index = 0; index < displayModeCount; ++index)
        {
            if (static_cast<int>(displayModeWidths[index]) == width &&
                static_cast<int>(displayModeHeights[index]) == height &&
                displayFormatBits(displayModeFormats[index]) == bitsPerPixel)
            {
                return static_cast<int>(index);
            }
        }
        return -1;
    }


    STRING DD_DRIVER::GetModeDesctription(int modeIndex) const
    {
        bool multipleFormats = false;
        if (displayModeCount > 1u)
        {
            const DWORD firstFormat = displayModeFormats[0];
            for (DWORD index = displayModeCount - 1u; index >= 1u; --index)
            {
                if (displayModeFormats[index] != firstFormat)
                {
                    multipleFormats = true;
                    break;
                }
                if (index == 1u)
                    break;
            }
        }

        const DWORD index = static_cast<DWORD>(modeIndex);
        if (!multipleFormats)
            return STRING::Format("%i x %i", static_cast<int>(displayModeWidths[index]), static_cast<int>(displayModeHeights[index]));
        return STRING::Format("%i x %i x %ibpp",
                      static_cast<int>(displayModeWidths[index]),
                      static_cast<int>(displayModeHeights[index]),
                      displayFormatBits(displayModeFormats[index]));
    }

    const DD_DRIVER& GRAPH::selectedAdapterRecord() const noexcept
    {
        return m_adapterRecords[static_cast<std::size_t>(m_selectedAdapterIndex)];
    }

    DD_DRIVER& GRAPH::selectedAdapterRecord() noexcept
    {
        return m_adapterRecords[static_cast<std::size_t>(m_selectedAdapterIndex)];
    }


    int GRAPH::syncDisplayModeDialog(const win::DialogItemRef& deviceRef,
                          const win::DialogItemRef& modeRef,
                          const win::DialogItemRef* fullscreenRef)
    {
        if (deviceRef.sendControlMessage(CB_GETCOUNT, 0, 0) != 0)
        {
            const LRESULT selection = deviceRef.currentSelection();
            m_selectedAdapterIndex = static_cast<int>(deviceRef.sendControlMessage(CB_GETITEMDATA, static_cast<WPARAM>(selection), 0));
        }
        else
        {
            if (fullscreenRef)
                fullscreenRef->sendControlMessage(BM_SETCHECK, (m_graphFlags >> 7u) & 1u, 0);

            deviceRef.sendControlMessage(CB_RESETCONTENT, 0, 0);
            for (DWORD adapter = 0; adapter < m_adapterCount; ++adapter)
            {
                const DD_DRIVER& record = m_adapterRecords[adapter];
                const LRESULT item = deviceRef.sendControlMessage(CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(record.description));
                if (item != CB_ERR)
                    deviceRef.sendControlMessage(CB_SETITEMDATA, static_cast<WPARAM>(item), static_cast<LPARAM>(adapter));
                if (static_cast<int>(adapter) == m_selectedAdapterIndex)
                    deviceRef.sendControlMessage(CB_SETCURSEL, static_cast<WPARAM>(item), 0);
            }
        }

        DD_DRIVER& record = selectedAdapterRecord();

        if (modeRef.sendControlMessage(CB_GETCOUNT, 0, 0) != 0)
        {
            const LRESULT selection = modeRef.currentSelection();
            const DWORD packed = static_cast<DWORD>(modeRef.sendControlMessage(CB_GETITEMDATA, static_cast<WPARAM>(selection), 0));
            m_sizeX = static_cast<float>(packed & 0x7FFFu);
            m_sizeY = static_cast<float>(static_cast<std::int32_t>(packed) >> 16u);
            m_graphFlags = (m_graphFlags & ~0x2u) | ((packed >> 14u) & 0x2u);
        }

        const int currentWidth = static_cast<int>(m_sizeX);
        const int currentHeight = static_cast<int>(m_sizeY);
        int currentMode = record.GetMode(currentWidth, currentHeight, (m_graphFlags & 0x2u) != 0u ? 32 : 16);
        if (currentMode < 0)
        {
            m_sizeX = static_cast<float>(record.displayModeWidths[0]);
            m_sizeY = static_cast<float>(record.displayModeHeights[0]);
            m_graphFlags = (m_graphFlags & ~0x2u) |
                           (displayFormatBits(record.displayModeFormats[0]) == 32 ? 0x2u : 0u);
        }

        if (fullscreenRef)
        {
            bool fullscreenAvailable = false;
            if ((record.capabilityFlags & 0x1u) != 0u &&
                ((m_graphFlags >> 1u) & 1u) == (displayFormatBits(record.desktopDisplayFormat) == 32))
            {
                fullscreenAvailable = static_cast<float>(GetSystemMetrics(SM_CXSCREEN)) >= m_sizeX &&
                                      static_cast<float>(GetSystemMetrics(SM_CYSCREEN)) >= m_sizeY;
            }

            if (fullscreenAvailable)
            {
                EnableWindow(GetDlgItem(fullscreenRef->dialog, fullscreenRef->controlId), TRUE);
            }
            else
            {
                fullscreenRef->sendControlMessage(BM_SETCHECK, BST_CHECKED, 0);
                EnableWindow(GetDlgItem(fullscreenRef->dialog, fullscreenRef->controlId), FALSE);
            }

            setFullscreenRequested(fullscreenRef->isChecked());
        }

        modeRef.sendControlMessage(CB_RESETCONTENT, 0, 0);
        for (DWORD index = 0; index < record.displayModeCount; ++index)
        {
            STRING text = record.GetModeDesctription(static_cast<int>(index));
            const int bits = displayFormatBits(record.displayModeFormats[index]);
            const DWORD packed = (bits == 32 ? 0x8000u : 0u) |
                                 (record.displayModeWidths[index] & 0x7FFFu) |
                                 ((record.displayModeHeights[index] & 0xFFFFu) << 16u);
            const LRESULT item = modeRef.sendControlMessage(CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text.c_str()));
            if (item != CB_ERR)
                modeRef.sendControlMessage(CB_SETITEMDATA, static_cast<WPARAM>(item), static_cast<LPARAM>(packed));
            if (static_cast<float>(record.displayModeWidths[index]) == m_sizeX &&
                static_cast<float>(record.displayModeHeights[index]) == m_sizeY &&
                ((m_graphFlags >> 1u) & 1u) == (bits == 32))
            {
                modeRef.sendControlMessage(CB_SETCURSEL, static_cast<WPARAM>(item), 0);
            }
        }

        return m_selectedAdapterIndex;
    }


    void GRAPH::buildAdapterRecord(DD_DRIVER& record, void* direct3D, int adapter,
                                   const as1::core::StartupSettingsBlock& startupSettings)
    {
        IDirect3D8* d3d = graphD3D(direct3D);

        D3DADAPTER_IDENTIFIER8 identifier;
        d3d->GetAdapterIdentifier(static_cast<UINT>(adapter), 0u, &identifier);
        D3DDISPLAYMODE desktop;
        d3d->GetAdapterDisplayMode(static_cast<UINT>(adapter), &desktop);
        std::memcpy(record.description, identifier.Description, 0x28u);
        record.desktopDisplayFormat = static_cast<DWORD>(desktop.Format);

        record.displayModeCount = 0u;
        const UINT count = d3d->GetAdapterModeCount(static_cast<UINT>(adapter));
        for (UINT index = count; index > 0u; --index)
        {
            D3DDISPLAYMODE mode;
            d3d->EnumAdapterModes(static_cast<UINT>(adapter), index - 1u, &mode);

            const DWORD format = static_cast<DWORD>(mode.Format);
            DWORD colorBits = 0u;
            if (format == 21u || format == 22u)
                colorBits = 32u;
            else if (format == 23u || format == 24u)
                colorBits = 16u;
            else
                continue;

            if (!isAllowedDisplayMode(startupSettings, mode.Width, mode.Height, colorBits))
                continue;
            if (record.GetMode(static_cast<int>(mode.Width), static_cast<int>(mode.Height), static_cast<int>(colorBits)) >= 0)
                continue;

            const DWORD slot = record.displayModeCount;
            record.displayModeWidths[slot] = mode.Width;
            record.displayModeHeights[slot] = mode.Height;
            record.displayModeFormats[slot] = format;
            record.depthStencilFormats[slot] = chooseDepthStencilFormat(d3d, static_cast<UINT>(adapter), mode.Format);
            record.displayModeCount = slot + 1u;
            const STRING depthName = D3DFormatToString(record.depthStencilFormats[slot]);
            const STRING colorName = D3DFormatToString(format);
            LOG::Write("   Enum display modes %ix%i %s %s",
                       static_cast<int>(mode.Width),
                       static_cast<int>(mode.Height),
                       colorName.c_str(), depthName.c_str());
        }

        D3DCAPS8 caps{};
        (void)d3d->GetDeviceCaps(static_cast<UINT>(adapter), D3DDEVTYPE_HAL, &caps);

        record.videoMemoryBudgetBytes = 0x007A1200u;


        const DWORD capabilityBits =
            ((caps.Caps2 >> 19u) & 0x1u) | ((caps.Caps2 & 0x2u) << 2u);
        record.capabilityFlags = (record.capabilityFlags & 0xFFFFFFF0u) | capabilityBits;
    }

    GRAPH::GRAPH()
    {

    }

    GRAPH::~GRAPH()
    {
        deinit();
    }

    void GRAPH::SetCamera(float x, float y)
    {
        auto& drawState = core::GlobalApplicationDrawDispatcherState();
        drawState.setCameraShiftX(x);
        drawState.setCameraShiftY(y);
    }
    float GRAPH::cameraX() const { return core::GlobalApplicationDrawDispatcherState().cameraShiftX(); }
    float GRAPH::cameraY() const { return core::GlobalApplicationDrawDispatcherState().cameraShiftY(); }


    int GRAPH::SetAlphaBlend(DWORD srcBlend, DWORD dstBlend)
    {
        IDirect3DDevice8* const device = graphDevice(m_device);
        (void)device->SetRenderState(static_cast<D3DRENDERSTATETYPE>(0x1Bu), 1u);
        (void)device->SetRenderState(static_cast<D3DRENDERSTATETYPE>(0x13u), srcBlend);
        return static_cast<int>(device->SetRenderState(
            static_cast<D3DRENDERSTATETYPE>(0x14u), dstBlend));
    }

    void GRAPH::reloadPaletteLightBuffer()
    {

        if (!m_lightBuffer || m_lightBuffer->format() != 41u)
            return;

        invokeBaseTextureVirtualDestructor(m_lightBuffer);
        m_lightBuffer = new BASE_TEXTURE(256, 256, 41u, 0u);
        if (!m_lightBuffer->isLoaded())
            LOG::ResourceError("%s", 3, "Light at RelodPalette()", 0, "GRAPH");

        if (m_lightBuffer->format() == 41u)
        {
            std::array<DWORD, 256> palette{};
            for (DWORD value = 0; value < 256u; ++value)
                palette[value] = 0xFF000000u | value | (value << 8u) | (value << 16u);
            m_lightBuffer->createPaletteSlot(palette.data());
        }
        LOG::Write("ReloadPalettes");
    }

    void GRAPH::drawBackBufferPixel2x2(float x, float y, DWORD color)
    {

        lockBackBuffer();

        if (!(x >= static_cast<double>(m_viewportLeft)) ||
            !(y >= static_cast<double>(m_viewportTop)) ||
            !(x < static_cast<double>(m_viewportRight) - 1.0) ||
            !(y < static_cast<double>(m_viewportBottom) - 1.0))
            return;

        const int ix = graphConvertFloatToInt32(x);
        const int iy = graphConvertFloatToInt32(y);
        if ((m_graphFlags & 2u) != 0u)
        {
            DWORD* const pixels = static_cast<DWORD*>(m_lockedBackBufferPixels);
            pixels[ix + (iy + 1) * m_backBufferPitchPixels + 1] = color;
            pixels[ix + iy * m_backBufferPitchPixels + 1] = color;
            pixels[ix + (iy + 1) * m_backBufferPitchPixels] = color;
            pixels[ix + iy * m_backBufferPitchPixels] = color;
            return;
        }

        WORD* const pixels = static_cast<WORD*>(m_lockedBackBufferPixels);
        const WORD packed = static_cast<WORD>(
            ((color >> 3u) & 0x1Fu) |
            (g_color16RedMask & (color >> (16u - g_color16RedShift))) |
            (g_color16GreenMask & (color >> (8u - g_color16GreenShift))));
        pixels[ix + (iy + 1) * m_backBufferPitchPixels + 1] = packed;
        pixels[ix + iy * m_backBufferPitchPixels + 1] = packed;
        pixels[ix + (iy + 1) * m_backBufferPitchPixels] = packed;
        pixels[ix + iy * m_backBufferPitchPixels] = packed;
    }

    DWORD* GRAPH::sampleBackBufferPixel(DWORD* colorOut, float x, float y)
    {

        lockBackBuffer();
        if (!(x >= static_cast<double>(m_viewportLeft) &&
              x < static_cast<double>(m_viewportRight) &&
              y >= static_cast<double>(m_viewportTop) &&
              y < static_cast<double>(m_viewportBottom)))
        {
            *colorOut = 0xFF000000u;
            return colorOut;
        }

        const int ix = graphConvertFloatToInt32(x);
        const int iy = graphConvertFloatToInt32(y);
        const std::ptrdiff_t index = static_cast<std::ptrdiff_t>(ix) +
                                     static_cast<std::ptrdiff_t>(iy) * static_cast<std::ptrdiff_t>(m_backBufferPitchPixels);
        if ((m_graphFlags & 2u) != 0)
        {
            *colorOut = static_cast<const DWORD*>(m_lockedBackBufferPixels)[index];
        }
        else
        {
            const WORD raw = static_cast<const WORD*>(m_lockedBackBufferPixels)[index];
            *colorOut = 8u * (raw & 0x1Fu) |
                        ((static_cast<DWORD>(raw) << (8u - g_color16GreenShift)) & 0x0000FF00u) |
                        ((static_cast<DWORD>(raw) << (16u - g_color16RedShift)) & 0x00FF0000u);
        }
        return colorOut;
    }


    void GRAPH::SaveTGA(const STRING* outputPath, int x, int y, int width, int height)
    {
        if (!outputPath)
            return;

        images::PICTURE_RESOURCE pictureResource(width, height, 1);
        images::PICTURE* picture = pictureResource.picture();
        if (!picture)
            return;

        for (int py = 0; py < height; ++py)
        {
            for (int px = 0; px < width; ++px)
            {
                DWORD color = 0;
                sampleBackBufferPixel(&color, static_cast<float>(px + x), static_cast<float>(py + y));
                pictureResource.writePictureResourcePixel(px, py, color);
            }
        }
        (void)picture->saveTGA(*outputPath, 0, 0, -1, -1);
    }


    int GRAPH::DrawLoadBar(VID* drawVid, VID* secondVid, int shiftX, int shiftY)
    {
        static DWORD primaryTime = 0u;
        static DWORD secondaryTime = 0u;
        static int primaryFrame = 0;
        static int secondaryFrame = 0;

        const DWORD sampledTime = ::timeGetTime();
        core::RealCurrentTime = sampledTime;

        const bool drawPrimary = drawVid != nullptr &&
            sampledTime - primaryTime > static_cast<DWORD>(drawVid->defaultFrameSpeed());
        const bool drawSecondary = secondVid != nullptr &&
            sampledTime - secondaryTime > static_cast<DWORD>(secondVid->defaultFrameSpeed());
        if (!drawPrimary && !drawSecondary)
            return 0;

        (void)PreTact();

        VID* special = EmptyVid;
        core::ApplicationVidTable& appVidTable = core::GlobalApplicationVidTable();
        if (appVidTable.count() > 8 && appVidTable.slot(8))
            special = appVidTable.slot(8);

        if (drawVid != special)
            (void)clearFrameBuffers(0xFF000000u);

        if (drawVid)
        {
            const VECTOR& link = drawVid->linkOffset();
            drawVidFrame(drawVid, primaryFrame,
                m_sizeX * 0.5f + link.x,
                m_sizeY * 0.5f + link.y + 1.0f,
                1.0f);

            if (drawPrimary)
            {
                ++primaryFrame;
                const int frameCount = static_cast<int>(static_cast<std::int16_t>(drawVid->totalFrames()));
                if (primaryFrame >= frameCount)
                    primaryFrame = 0;
                primaryTime = sampledTime;
            }
        }

        if (secondVid)
        {
            drawVidFrame(secondVid, secondaryFrame,
                m_sizeX * 0.5f + static_cast<float>(shiftX),
                m_sizeY * 0.5f + static_cast<float>(shiftY) + 2.0f,
                2.0f);

            if (drawSecondary)
            {
                ++secondaryFrame;
                const int frameCount = static_cast<int>(static_cast<std::int16_t>(secondVid->totalFrames()));
                if (secondaryFrame >= frameCount)
                    secondaryFrame = (drawVid == special) ? 2 : 0;
                secondaryTime = sampledTime;
            }
        }

        DrawEffect(1);
        if (!m_loadingPresentationText.isEmpty())
        {
            (void)drawTextColored(200.0f, m_viewportTop + 5.0f,
                m_loadingPresentationText.c_str(), g_colorGreen.color);
        }
        (void)PostTact(1);
        return 1;
    }

    void GRAPH::drawVidFrame(VID* vid, int cadr, float x, float y, float z)
    {

        if (!vid || vid == EmptyVid)
            return;

        const int frameCount = static_cast<int>(static_cast<std::int16_t>(vid->totalFrames()));
        if (cadr < 0 || cadr >= frameCount)
        {
            logFileLoggerResourceError(g_fileLogger, "GRAPH", 4, "ncadr in DrawVid", cadr);
            return;
        }

        const bool wasLocked = m_lockedBackBufferPixels != nullptr;
        const int vidLayer = vid->renderLayer();
        if (vidLayer == 5 || vidLayer == 6 || vidLayer == 7)
        {
            if (!m_lockedBackBufferPixels)
            {

                D3DLOCKED_RECT locked;
                IDirect3DSurface8* const surface = graphSurface(m_backBuffer);
                const HRESULT lockResult = surface->LockRect(&locked, nullptr, 0);
                if (FAILED(lockResult))
                    logGraphResourceError(0, "backBuffer", 0);
                m_lockedBackBufferPixels = locked.pBits;
                const int divisor = (m_graphFlags & 2u) != 0u ? 4 : 2;
                m_backBufferPitchPixels = locked.Pitch / divisor;
            }
        }
        else if (m_lockedBackBufferPixels)
        {
            graphSurface(m_backBuffer)->UnlockRect();
            m_lockedBackBufferPixels = nullptr;
        }

        if (vidLayer > 8)
        {
            setRenderStateCached(14u, 0u);
            setRenderStateCached(27u, 1u);
            setRenderStateCached(23u, 7u);
        }
        else
        {
            setRenderStateCached(27u, 0u);
            setRenderStateCached(14u, 1u);
        }

        MAP* const map = Map;
        const core::ApplicationDrawDispatcherState& appDraw =
            core::GlobalApplicationDrawDispatcherState();
        SPRITE sprite(
            map,
            vid,
            VECTOR{x + appDraw.cameraShiftX(), y + appDraw.cameraShiftY(), z},
            ANGLE{});
        sprite.setCurrentFrameDirect(cadr);
        vid->Draw(&sprite);

        if (!wasLocked)
        {
            if (m_lockedBackBufferPixels)
            {
                graphSurface(m_backBuffer)->UnlockRect();
                m_lockedBackBufferPixels = nullptr;
            }
        }
        else if (!m_lockedBackBufferPixels)
        {

            D3DLOCKED_RECT locked;
            IDirect3DSurface8* const surface = graphSurface(m_backBuffer);
            const HRESULT lockResult = surface->LockRect(&locked, nullptr, 0);
            if (FAILED(lockResult))
                logFileLoggerResourceError(g_fileLogger, "GRAPH", 0, "backBuffer", 0);
            m_lockedBackBufferPixels = locked.pBits;
            const int divisor = (m_graphFlags & 2u) != 0u ? 4 : 2;
            m_backBufferPitchPixels = locked.Pitch / divisor;
        }
    }


    int GRAPH::logGraphResourceError(int value1, const char* message, int value2)
    {

        return static_cast<int>(logFileLoggerResourceError(g_fileLogger, "GRAPH", value1, message, value2));
    }


    void GRAPH::SaveParameters(RESOURCE* stream)
    {
        if (!stream)
            return;


        (void)stream->write(&m_renderFlags, 4);
        (void)stream->write(&m_gammaPair.first, 4);
        (void)stream->write(&m_gammaPair.second, 4);
        (void)stream->write(&m_windDirection, 4);
        (void)stream->write(&m_windSpeed, 4);
    }

    void GRAPH::invalidateDeviceObjects() noexcept
    {
        unlockBackBufferIfLocked();

        if (m_textFont)
            m_textFont->InvalidateDeviceObjects();
        if (MAP* const map = Map)
            map->invalidateFontVidDeviceObjects();


        if (m_backBuffer)
        {
            const ULONG releaseResult = graphSurface(m_backBuffer)->Release();
            m_backBuffer = nullptr;
            LOG::Rewrite("backBuffer release %i", static_cast<int>(releaseResult));
        }
    }


    int GRAPH::restoreDeviceRenderStates() noexcept
    {
        if (!m_device)
            return 1;

        IDirect3DDevice8* const device = graphDevice(m_device);


        (void)device->SetTextureStageState(0u, static_cast<D3DTEXTURESTAGESTATETYPE>(2u), 2u);
        (void)device->SetTextureStageState(0u, static_cast<D3DTEXTURESTAGESTATETYPE>(3u), 0u);
        (void)device->SetTextureStageState(0u, static_cast<D3DTEXTURESTAGESTATETYPE>(1u), 4u);
        (void)device->SetTextureStageState(0u, static_cast<D3DTEXTURESTAGESTATETYPE>(5u), 2u);
        (void)device->SetTextureStageState(0u, static_cast<D3DTEXTURESTAGESTATETYPE>(6u), 0u);
        (void)device->SetTextureStageState(0u, static_cast<D3DTEXTURESTAGESTATETYPE>(4u), 4u);
        (void)device->SetTextureStageState(0u, static_cast<D3DTEXTURESTAGESTATETYPE>(17u), 1u);
        (void)device->SetTextureStageState(0u, static_cast<D3DTEXTURESTAGESTATETYPE>(16u), 1u);
        (void)device->SetTextureStageState(0u, static_cast<D3DTEXTURESTAGESTATETYPE>(13u), 3u);
        (void)device->SetTextureStageState(0u, static_cast<D3DTEXTURESTAGESTATETYPE>(14u), 3u);


        (void)device->SetRenderState(static_cast<D3DRENDERSTATETYPE>(29u), 0u);
        (void)device->SetRenderState(static_cast<D3DRENDERSTATETYPE>(27u), 0u);
        (void)setRenderStateCached(26u, 1u);
        (void)setRenderStateCached(142u, 0u);
        (void)setRenderStateCached(137u, 0u);
        (void)setRenderStateCached(15u, 0u);
        (void)device->SetRenderState(static_cast<D3DRENDERSTATETYPE>(23u), 8u);
        (void)device->SetRenderState(static_cast<D3DRENDERSTATETYPE>(14u), 0u);
        (void)setRenderStateCached(7u, 1u);
        return setRenderStateCached(23u, 7u);
    }

    int GRAPH::restoreDeviceObjects() noexcept
    {
        if (!m_device)
            return 1;
        IDirect3DDevice8* const device = graphDevice(m_device);


        if (m_textFont)
            (void)m_textFont->RestoreDeviceObjects();
        if (MAP* const map = Map)
            map->restoreFontVidDeviceObjects();

        IDirect3DSurface8* backBuffer = nullptr;
        const HRESULT backBufferResult = device->GetBackBuffer(0u, D3DBACKBUFFER_TYPE_MONO, &backBuffer);
        if (backBufferResult != D3D_OK || !backBuffer)
        {
            LOG::ResourceError("%s", 9, "BackBuffer", static_cast<int>(backBufferResult), "GRAPH");
            return 2;
        }
        m_backBuffer = backBuffer;


        (void)restoreDeviceRenderStates();
        return 0;
    }

    int GRAPH::PreTact()
    {

        if ((m_graphFlags & 0x00000400u) != 0u)
            return 0;

        IDirect3DDevice8* const device = graphDevice(m_device);
        const HRESULT cooperative = device->TestCooperativeLevel();
        if (cooperative == D3DERR_DEVICELOST)
        {
            reloadPaletteLightBuffer();
            LOG::Write("TestCooperativeLevel() -> D3DERR_DEVICELOST");
            return 1;
        }

        if (cooperative == D3DERR_DEVICENOTRESET)
        {
            invalidateDeviceObjects();

            D3DPRESENT_PARAMETERS8& pp = m_d3d8PresentParameters;
            const HRESULT resetResult = device->Reset(&pp);
            LOG::ResourceError("%s", 4, "device notreset", static_cast<int>(resetResult), "GRAPH");
            if (FAILED(resetResult))
                return 2;

            if (restoreDeviceObjects() != 0)
                return 2;
        }
        else if (cooperative != D3D_OK)
        {
            return 3;
        }

        const HRESULT beginResult = device->BeginScene();
        if (beginResult != D3D_OK)
        {
            LOG::ResourceError("%s", 10, "3dBeginScene for PreTact", static_cast<int>(beginResult), "GRAPH");
            return 4;
        }
        m_graphFlags |= 0x00000400u;
        return 0;
    }

    int GRAPH::PostTact(int presentFlag)
    {

        if ((m_graphFlags & 0x00000400u) == 0u)
            return 0;

        unlockBackBufferIfLocked();
        IDirect3DDevice8* const device = graphDevice(m_device);
        const HRESULT endResult = device->EndScene();
        if (endResult != D3D_OK)
            LOG::ResourceError("%s", 10, "3dEndScene for PostTact", static_cast<int>(endResult), "GRAPH");

        int result = 0;
        if (presentFlag && m_effectStartTimes[6] == 0u && m_effectStartTimes[7] == 0u)
        {
            RECT sourceRect{};
            sourceRect.left = static_cast<LONG>(m_viewportLeft);
            sourceRect.top = static_cast<LONG>(m_viewportTop);
            sourceRect.right = static_cast<LONG>(m_viewportRight);
            sourceRect.bottom = static_cast<LONG>(m_viewportBottom);
            RECT destinationRect = sourceRect;

            if (!fullscreenRequested())
            {
                RECT clientRect{};
                RECT windowRect{};
                ::GetClientRect(static_cast<HWND>(m_windowHandle), &clientRect);
                ::ClientToScreen(static_cast<HWND>(m_windowHandle), reinterpret_cast<POINT*>(&clientRect));
                ::GetWindowRect(static_cast<HWND>(m_windowHandle), &windowRect);
                const LONG offsetX = windowRect.left - clientRect.left;
                const LONG offsetY = windowRect.top - clientRect.top;
                destinationRect.left += offsetX;
                destinationRect.right += offsetX;
                destinationRect.top += offsetY;
                destinationRect.bottom += offsetY;
            }

            const HRESULT presentResult = fullscreenRequested()
                ? device->Present(nullptr, nullptr, nullptr, nullptr)
                : device->Present(&sourceRect, &destinationRect, nullptr, nullptr);
            result = static_cast<int>(presentResult);
            if (presentResult != D3D_OK)
            {
                result = static_cast<int>(logFileLoggerResourceError(
                    g_fileLogger, "%s", 4, "Present", static_cast<int>(presentResult), "GRAPH"));
            }
        }
        else if (m_effectStartTimes[6] != 0u)
        {
            result = static_cast<int>(m_effectStartTimes[6]);
        }
        else if (m_effectStartTimes[7] != 0u)
        {
            result = static_cast<int>(m_effectStartTimes[7]);
        }

        m_graphFlags &= ~0x00000400u;
        return result;
    }


    void GRAPH::FlipToGDI()
    {
        if ((m_graphFlags & 0x00000080u) == 0u)
            return;

        (void)PostTact(1);

        HWND hwnd = static_cast<HWND>(m_windowHandle);
        if (win::applicationWinInstance())
            hwnd = win::applicationWinInstance()->nativeWindow();

        HDC const dc = ::GetDC(hwnd);
        if (!dc)
            (void)logFileLoggerResourceError(g_fileLogger, "GRAPH", 9, "DC in FlipToGDI", 0);

        if (::SetPixel(dc, 0, 0, 0x00FFFFFFu) == CLR_INVALID)
            (void)logFileLoggerResourceError(g_fileLogger, "GRAPH", 8, "pixel in FlipToGDI", 0);

        ::Sleep(150u);
        for (int i = 0; ::GetPixel(dc, 0, 0) == 0x00FFFFFFu; ++i)
        {
            if (i >= 8)
                break;
            (void)PreTact();
            (void)PostTact(1);
            ::Sleep(150u);
        }
        (void)::ReleaseDC(hwnd, dc);
    }


    int GRAPH::clearFrameBuffers(DWORD color)
    {

        unlockBackBufferIfLocked();


        graphDevice(m_device)->Clear(0u, nullptr, D3DCLEAR_TARGET, color, 0.0f, 0u);
        const std::uint32_t heightRaw = static_cast<std::uint32_t>(graphConvertFloatToInt32(m_sizeY));
        std::uint32_t wordCount = heightRaw * static_cast<std::uint32_t>(m_softwareDepthPitch);
        std::uint16_t* out = m_softwareDepthBuffer;
        if ((wordCount & 1u) != 0u)
        {
            *out++ = static_cast<std::uint16_t>(0x03FFu);
            --wordCount;
        }
        const std::uint32_t dwordCount = wordCount >> 1u;
        const std::uint32_t fill = 0x03FF03FFu;
        for (std::uint32_t i = 0; i < dwordCount; ++i)
        {
            std::memcpy(out, &fill, sizeof(fill));
            out += 2;
        }
        return static_cast<int>(0x03FF03FFu);
    }


    void GRAPH::DrawSquall()
    {

        if ((core::ApplicationFlags() & application_flags::BucketTimingActive) != 0u)
            return;

        const std::uint32_t now = core::CurrentTimeMilliseconds();
        if ((m_renderFlags & 0x80u) != 0u)
        {
            if (graphFcompC3Equal(old_w_speed, -1.0f))
                old_w_speed = m_windSpeed;

            const std::uint32_t elapsed = now - start_squall;
            std::uint32_t triangularTime = 0u;
            if (elapsed <= 0x800u)
            {
                triangularTime = elapsed;
            }
            else if (elapsed <= 0x1000u)
            {
                triangularTime = 0x1000u - elapsed;
            }
            else
            {
                m_renderFlags &= 0xFFFFFF7Fu;
                m_windSpeed = old_w_speed;
                old_w_speed = -1.0f;
                return;
            }

            m_windSpeed =
                static_cast<float>(triangularTime) * old_w_speed * 0.001953125f +
                old_w_speed;
            return;
        }

        start_squall = now;
        if (graphFcompC3Equal(old_w_speed, -1.0f))
        {
            old_w_speed = -1.0f;
            return;
        }

        m_windSpeed = old_w_speed;
        old_w_speed = -1.0f;
    }

    void GRAPH::drawFogBufferOverlay(float left, float top, float right, float bottom,
                              int a6, int a7, DWORD colorMask, const WORD* ramp,
                              int baseDepth, int blendFlag)
    {

        if ((m_graphFlags & 0x04u) != 0u || ramp == nullptr ||
            graphFcompC0(right, m_viewportLeft) ||
            !graphFcompC0(left, m_viewportRight) ||
            graphFcompC0(bottom, m_viewportTop) ||
            !graphFcompC0(top, m_viewportBottom))
            return;

        int clippedLeft = graphConvertFloatToInt32(left);
        int clippedTop = graphConvertFloatToInt32(top);
        int clippedRight = graphConvertFloatToInt32(right);
        int clippedBottom = graphConvertFloatToInt32(bottom);
        if (graphFcompC0(left, m_viewportLeft))
            clippedLeft = graphConvertFloatToInt32(m_viewportLeft);
        if (graphFcompC0(top, m_viewportTop))
            clippedTop = graphConvertFloatToInt32(m_viewportTop);
        if (!graphFcompC0(right, m_viewportRight))
            clippedRight = graphConvertFloatToInt32(m_viewportRight);
        if (!graphFcompC0(bottom, m_viewportBottom))
            clippedBottom = graphConvertFloatToInt32(m_viewportBottom);

        const int width = clippedRight - clippedLeft;
        const int height = clippedBottom - clippedTop;
        if (width < 4 || height < 4)
            return;

        RECTI sourceRect{0, 0, width / 4, height / 4};
        RECTI destinationRect{clippedLeft, clippedTop, clippedRight, clippedBottom};
        const std::uint32_t lowerDepthRaw =
            static_cast<std::uint32_t>(baseDepth) +
            ((static_cast<std::uint32_t>(a6) - static_cast<std::uint32_t>(a7)) << 3u);
        const int lowerDepth = static_cast<std::int32_t>(lowerDepthRaw);

        int texturePitchBytes = 0;
        std::uint16_t* const locked = m_lightBuffer->lock16(&texturePitchBytes, &sourceRect);
        if (!locked)
        {
            LOG::ResourceError("%s", 0, "fog buffer", 0, "GRAPH");
            return;
        }

        const std::uint16_t* const depth = m_softwareDepthBuffer;
        int depthIndex = clippedLeft + m_softwareDepthPitch * clippedTop;
        if (m_lightBuffer->format() == 41u)
        {
            std::uint8_t* output = reinterpret_cast<std::uint8_t*>(locked);
            std::uint8_t previous = 0u;
            const unsigned rowCount = static_cast<unsigned>(height + 3) >> 2u;
            for (unsigned row = 0; row < rowCount; ++row)
            {
                const unsigned columnCount = static_cast<unsigned>(width + 3) >> 2u;
                for (unsigned column = 0; column < columnCount; ++column)
                {
                    const std::uint16_t first = depth[depthIndex];
                    const std::uint16_t second = depth[depthIndex + 3];
                    const int depthValue = static_cast<int>(first < second ? first : second) - 1024;
                    if (depthValue > baseDepth)
                    {
                        const int upperFadeDepth = static_cast<std::int32_t>(
                            static_cast<std::uint32_t>(baseDepth) + 10u);
                        if (depthValue <= upperFadeDepth)
                        {
                            previous = 0u;
                            *output = 0u;
                        }
                        else
                        {
                            *output = previous;
                        }
                    }
                    else if (depthValue > lowerDepth)
                    {
                        previous = reinterpret_cast<const std::uint8_t*>(ramp)[2 * (baseDepth - depthValue)];
                        *output = previous;
                    }
                    else
                    {
                        previous = 0xFFu;
                        *output = 0xFFu;
                    }
                    ++output;
                    depthIndex += 4;
                }
                const int quarterWidth = (width + 3) / 4;
                depthIndex += 4 * (m_softwareDepthPitch - quarterWidth);
                output += texturePitchBytes - quarterWidth;
            }
        }
        else
        {
            std::uint16_t* output = locked;
            std::uint16_t previous = 0u;
            const unsigned rowCount = static_cast<unsigned>(height + 3) >> 2u;
            for (unsigned row = 0; row < rowCount; ++row)
            {
                const unsigned columnCount = static_cast<unsigned>(width + 3) >> 2u;
                for (unsigned column = 0; column < columnCount; ++column)
                {
                    const std::uint16_t first = depth[depthIndex];
                    const std::uint16_t second = depth[depthIndex + 3];
                    const int depthValue = static_cast<int>(first < second ? first : second) - 1024;
                    if (depthValue > baseDepth)
                    {
                        const int upperFadeDepth = static_cast<std::int32_t>(
                            static_cast<std::uint32_t>(baseDepth) + 10u);
                        if (depthValue <= upperFadeDepth)
                        {
                            previous = 0u;
                            *output = 0u;
                        }
                        else
                        {
                            *output = previous;
                        }
                    }
                    else if (depthValue > lowerDepth)
                    {
                        previous = ramp[baseDepth - depthValue];
                        *output = previous;
                    }
                    else
                    {
                        previous = ramp[baseDepth - lowerDepth];
                        *output = previous;
                    }
                    ++output;
                    depthIndex += 4;
                }
                const int quarterWidth = (width + 3) / 4;
                depthIndex += 4 * (m_softwareDepthPitch - quarterWidth);
                output += texturePitchBytes / 2 - quarterWidth;
            }
        }

        m_lightBuffer->unlock();
        SetAlphaBlend(2u - (blendFlag != 0 ? 1u : 0u), 4u);
        const int zDepth = static_cast<std::int32_t>(
            static_cast<std::uint32_t>(baseDepth) + 0x3FEu);
        const float z = static_cast<float>(zDepth) * 0.000015258789f;
        const Gamma colors(~colorMask, Color(0, 0, 0).color);
        const DWORD zRaw = floatRaw(z);
        m_lightBuffer->DrawDepthRectangle(
            zRaw, zRaw, destinationRect, sourceRect, reinterpret_cast<const DWORD*>(&colors));
    }

    void GRAPH::drawSnowLightBuffer()
    {

        const core::ApplicationDrawDispatcherState& appDraw = core::GlobalApplicationDrawDispatcherState();
        const int shiftX = graphConvertFloatToInt32(appDraw.cameraShiftX());
        const int shiftY = graphConvertFloatToInt32(appDraw.cameraShiftY());
        const int startX = (-(shiftX & 3)) & 3;
        const int startY = (-(shiftY & 3)) & 3;

        if ((m_graphFlags & 0x04u) != 0u ||
            (core::ApplicationFlags() & application_flags::BucketTimingActive) != 0u)
            return;

        const std::uint32_t now = core::CurrentTimeMilliseconds();
        if ((m_renderFlags & 0x40u) == 0u)
        {
            g_groundSnowStart = now;
            g_groundSnowAmount = 0u;
            return;
        }

        if (g_groundSnowAmount < 0x100u)
            g_groundSnowAmount = (now - g_groundSnowStart) >> 7u;

        const int screenWidth = graphConvertFloatToInt32(m_sizeX);
        const int screenHeight = graphConvertFloatToInt32(m_sizeY);
        RECTI textureRect{0, 0, screenWidth / 4, screenHeight / 4};
        RECTI screenRect{0, 0, screenWidth, screenHeight};
        int texturePitchBytes = 0;
        std::uint16_t* const locked = m_lightBuffer->lock16(&texturePitchBytes, &textureRect);
        if (!locked)
        {
            LOG::ResourceError("%s", 0, "snow buffer", 0, "GRAPH");
            return;
        }

        const std::uint16_t* const depth = m_softwareDepthBuffer;
        const int depthPitch = m_softwareDepthPitch;
        const bool paletteTexture = m_lightBuffer->format() == 0x29u;
        if (paletteTexture)
        {
            std::uint8_t* const output = reinterpret_cast<std::uint8_t*>(locked);
            for (int y = startY; graphFcompC0(static_cast<float>(y), m_sizeY); y += 4)
            {
                int depthIndex = startX + y * depthPitch;
                for (int x = startX; graphFcompC0(static_cast<float>(x), m_sizeX); x += 4, depthIndex += 4)
                {
                    std::uint32_t intensity = 0u;
                    bool accepted = false;
                    if (y > 3)
                        accepted = graphSnowEdgeIntensity(depth[depthIndex], depth[depthIndex - 4 * depthPitch], intensity);
                    if (!accepted)
                    {
                        intensity = 0u;
                        graphSnowEdgeIntensity(depth[depthIndex], depth[depthIndex + 4 * depthPitch], intensity);
                    }
                    output[(y / 4) * texturePitchBytes + (x / 4)] = static_cast<std::uint8_t>(intensity);
                }
            }
        }
        else
        {
            std::uint16_t* const output = locked;
            const int texturePitchWords = texturePitchBytes / 2;
            for (int y = startY; graphFcompC0(static_cast<float>(y), m_sizeY); y += 4)
            {
                int depthIndex = startX + y * depthPitch;
                for (int x = startX; graphFcompC0(static_cast<float>(x), m_sizeX); x += 4, depthIndex += 4)
                {
                    std::uint32_t intensity = 0u;
                    bool accepted = false;
                    if (y > 3)
                        accepted = graphSnowEdgeIntensity(depth[depthIndex], depth[depthIndex - 4 * depthPitch], intensity);
                    if (!accepted)
                    {
                        intensity = 0u;
                        graphSnowEdgeIntensity(depth[depthIndex], depth[depthIndex + 4 * depthPitch], intensity);
                    }
                    output[(y / 4) * texturePitchWords + (x / 4)] =
                        m_intensityPalette16[static_cast<std::size_t>(intensity)];
                }
            }
        }

        m_lightBuffer->unlock();
        setRenderStateCached(0x1Du, 0u);
        SetAlphaBlend(2u, 4u);
        const DWORD colors[2] = {0u, 0u};
        m_lightBuffer->DrawFixedDepthRectangle(screenRect, textureRect, colors);
    }


    int GRAPH::drawLineParticles()
    {

        int result = static_cast<int>(m_renderFlags);
        if ((m_renderFlags & 0x00000C00u) == 0u)
        {
            g_lineParticleCount = 0;
            return result;
        }
        if ((m_graphFlags & 0x04u) != 0u)
            return result;
        void* const applicationOwner = core::ApplicationOwner();
        result = static_cast<int>(static_cast<std::uint32_t>(
            reinterpret_cast<std::uintptr_t>(applicationOwner)));
        if ((core::ApplicationFlags() & application_flags::BucketTimingActive) != 0u)
            return result;

        const DWORD direction = m_windDirection;
        if (g_lineParticleWindDirection != direction ||
            !graphFcompC3Equal(g_lineParticleWindSpeed, m_windSpeed))
        {
            const float oldWindX = g_lineParticleWindX;
            g_lineParticleWindX = graphWeatherWindX(m_windDirection, m_windSpeed);
            if (g_lineParticleCount > 0)
            {
                const float windDifference = g_lineParticleWindX - oldWindX;
                for (int index = 0; index < g_lineParticleCount; ++index)
                {
                    GraphWeatherLineParticle& particle = g_lineParticles[static_cast<std::size_t>(index)];
                    particle.vertex[1].x +=
                        (particle.vertex[1].y - particle.vertex[0].y) * windDifference * kGraphOneOver200;
                }
            }
            g_lineParticleWindDirection = direction;
            g_lineParticleWindSpeed = m_windSpeed;
        }

        const DWORD mode = m_renderFlags & 0x00000C00u;
        switch (mode)
        {
        case 0x00000C00u:
            g_lineParticleCount = 250;
            break;
        case 0x00000800u:
            if (g_lineParticleCount >= 250)
                SetEnvironment(0x00000C00u);
            else
                ++g_lineParticleCount;
            break;
        case 0x00000400u:
            if (g_lineParticleCount <= 0)
                m_renderFlags &= 0xFFFFF3FFu;
            else
                --g_lineParticleCount;
            break;
        default:
            break;
        }

        if (g_lineParticleCount > 0)
        {
            const std::uint32_t deltaMilliseconds =
                core::CurrentTimeMilliseconds() - core::PreviousWorldTimeMilliseconds();
            int respawned = 0;
            for (int index = 0; index < g_lineParticleCount; ++index)
            {
                GraphWeatherLineParticle& particle = g_lineParticles[static_cast<std::size_t>(index)];
                GraphWeatherVertex44& first = particle.vertex[0];
                GraphWeatherVertex44& second = particle.vertex[1];
                const bool active =
                    first.color != 0u &&
                    first.x >= m_viewportLeft && first.x < m_viewportRight &&
                    first.y >= m_viewportTop && first.y < m_viewportBottom &&
                    first.z >= 0.015625f;

                if (active)
                {
                    const float travel =
                        (second.z - first.z) * static_cast<float>(deltaMilliseconds) * 200.0f;
                    float windTravel = g_lineParticleWindX * travel * kGraphOneOver200;
                    if (graphFcompC0(windTravel + first.x, 0.0f))
                        windTravel += static_cast<float>(m_sizeX);
                    if (windTravel + first.x > static_cast<float>(m_sizeX))
                        windTravel -= static_cast<float>(m_sizeX);
                    const float depthTravel = travel * kGraphOneOver8192;
                    first.x += windTravel;
                    first.y += travel;
                    first.z -= depthTravel;
                    second.x += windTravel;
                    second.y += travel;
                    second.z -= depthTravel;
                }
                else
                {
                    ++respawned;
                    const float length = static_cast<float>((std::rand() % 51) + 15);
                    const float windTail = g_lineParticleWindX * length * -kGraphOneOver200;
                    const float x = static_cast<float>(std::rand()) *
                        (m_viewportRight - 1.0f) * kGraphRand32767;
                    const float yLimit = respawned >= 50 ? m_viewportBottom : 40.0f;
                    const float y = static_cast<float>(std::rand()) * yLimit * kGraphRand32767;
                    const float z = (length + 10.0f) * kGraphOneOver819_2 + 0.015625f;
                    first = GraphWeatherVertex44{x, y, z, 1.0f, 0x70E0E0FFu};
                    second = GraphWeatherVertex44{
                        x + windTail,
                        y - length,
                        z + length * kGraphOneOver8192,
                        1.0f,
                        0x308080FFu};
                }
            }
        }

        IDirect3DDevice8* const device = graphDevice(m_device);
        device->SetTexture(0u, nullptr);
        SetAlphaBlend(5u, 6u);
        result = drawPrimitiveUp(
            2u,
            0x44u,
            g_lineParticles.data(),
            static_cast<DWORD>(sizeof(GraphWeatherVertex44)),
            2 * g_lineParticleCount);
        return result;
    }


    int GRAPH::drawCrossParticles()
    {

        int result = static_cast<int>(m_renderFlags);
        if ((m_renderFlags & 0x0000C000u) == 0u)
        {
            g_crossParticleCount = 0;
            return result;
        }
        if ((m_graphFlags & 0x04u) != 0u)
            return result;
        void* const applicationOwner = core::ApplicationOwner();
        result = static_cast<int>(static_cast<std::uint32_t>(
            reinterpret_cast<std::uintptr_t>(applicationOwner)));
        if ((core::ApplicationFlags() & application_flags::BucketTimingActive) != 0u)
            return result;

        const DWORD direction = m_windDirection;
        if (g_crossParticleWindDirection != direction ||
            !graphFcompC3Equal(g_crossParticleWindSpeed, m_windSpeed))
        {
            g_crossParticleWindX = graphWeatherWindX(m_windDirection, m_windSpeed);
            g_crossParticleWindDirection = direction;
            g_crossParticleWindSpeed = m_windSpeed;
        }

        const DWORD mode = m_renderFlags & 0x0000C000u;
        switch (mode)
        {
        case 0x0000C000u:
            g_crossParticleCount = 1000;
            break;
        case 0x00008000u:
            if (g_crossParticleCount >= 1000)
            {
                g_crossParticleCount = 1000;
                SetEnvironment(0x0000C000u);
            }
            else
            {
                ++g_crossParticleCount;
            }
            break;
        case 0x00004000u:
            if (g_crossParticleCount <= 0)
                m_renderFlags &= 0xFFFF3FFFu;
            else
                --g_crossParticleCount;
            break;
        default:
            break;
        }

        int count = g_crossParticleCount;
        if (count > 0)
        {
            const std::uint32_t deltaMilliseconds =
                core::CurrentTimeMilliseconds() - core::PreviousWorldTimeMilliseconds();
            const core::ApplicationDrawDispatcherState& appDraw = core::GlobalApplicationDrawDispatcherState();
            const float cameraX = appDraw.cameraShiftX();
            const float cameraY = appDraw.cameraShiftY();
            int respawned = 0;
            for (int index = 0; index < count; ++index)
            {
                GraphWeatherCrossParticle& particle = g_crossParticles[static_cast<std::size_t>(index)];
                GraphWeatherVertex44& first = particle.vertex[0];
                const bool active = first.color != 0u && first.z >= 0.015625f;
                if (active)
                {
                    const float travel =
                        (particle.vertex[1].z - first.z - kGraphOneOver8192) *
                        static_cast<float>(deltaMilliseconds) * 150.0f;
                    const float xTravel =
                        (g_crossParticleWindX * travel + 50.0f - static_cast<float>(std::rand() % 101)) * 0.02f -
                        (g_crossParticlePreviousNegatedCameraX - -cameraX);
                    const float yTravel =
                        travel - (g_crossParticlePreviousNegatedCameraY - -cameraY);

                    float adjustedXTravel = xTravel;
                    float adjustedYTravel = yTravel;
                    const float movedX = adjustedXTravel + first.x;
                    if (!graphFcompC0(m_viewportLeft - 30.0f, movedX) &&
                        !graphFcompC3Equal(m_viewportLeft - 30.0f, movedX))
                        adjustedXTravel += m_viewportRight - m_viewportLeft;
                    const float movedXAfterLeft = adjustedXTravel + first.x;
                    if (graphFcompC0(m_viewportRight + 30.0f, movedXAfterLeft))
                        adjustedXTravel -= m_viewportRight - m_viewportLeft;

                    const float movedY = adjustedYTravel + first.y;
                    if (!graphFcompC0(m_viewportTop - 30.0f, movedY) &&
                        !graphFcompC3Equal(m_viewportTop - 30.0f, movedY))
                        adjustedYTravel += m_viewportBottom - m_viewportTop;
                    const float movedYAfterTop = adjustedYTravel + first.y;
                    if (graphFcompC0(m_viewportBottom + 30.0f, movedYAfterTop))
                        adjustedYTravel -= m_viewportBottom - m_viewportTop;

                    const float depthTravel = travel * kGraphOneOver8192;
                    for (GraphWeatherVertex44& vertex : particle.vertex)
                    {
                        vertex.x += adjustedXTravel;
                        vertex.y += adjustedYTravel;
                        vertex.z -= depthTravel;
                    }
                }
                else
                {
                    ++respawned;
                    const float radius = static_cast<float>((std::rand() % 3) + 2);
                    const float centerX = static_cast<float>(std::rand()) *
                        (m_viewportRight - 1.0f) * kGraphRand32767;
                    const float yLimit = respawned >= 50 ? m_viewportBottom : 40.0f;
                    const float centerY = static_cast<float>(std::rand()) * yLimit * kGraphRand32767;
                    const float z = static_cast<float>(std::rand()) *
                        (m_viewportBottom + 50.0f) * kGraphRand268435456 + 0.015625f;
                    const float halfRadius = radius * 0.5f;
                    const float zTail = z + radius * kGraphOneOver8192;

                    particle.vertex[0] = GraphWeatherVertex44{centerX - radius, centerY, z, 1.0f, 0xFFFFFFFFu};
                    particle.vertex[1] = GraphWeatherVertex44{centerX + radius, centerY, zTail, 1.0f, 0xFFFFFFFFu};
                    particle.vertex[2] = GraphWeatherVertex44{centerX - halfRadius, centerY - radius, z, 1.0f, 0xFFFFFFFFu};
                    particle.vertex[3] = GraphWeatherVertex44{centerX + halfRadius, centerY + radius, z, 1.0f, 0xFFFFFFFFu};
                    particle.vertex[4] = GraphWeatherVertex44{centerX - halfRadius, centerY + radius, z, 1.0f, 0xFFFFFFFFu};
                    particle.vertex[5] = GraphWeatherVertex44{centerX + halfRadius, centerY - radius, z, 1.0f, 0xFFFFFFFFu};
                }
            }

            for (int index = 0; index < count; ++index)
            {
                if ((std::rand() % 5) == 0)
                    graphTransformCrossParticle(g_crossParticles[static_cast<std::size_t>(index)]);
            }
        }

        const core::ApplicationDrawDispatcherState& appDrawForShiftCache =
            core::GlobalApplicationDrawDispatcherState();
        g_crossParticlePreviousNegatedCameraX = -appDrawForShiftCache.cameraShiftX();
        g_crossParticlePreviousNegatedCameraY = -appDrawForShiftCache.cameraShiftY();

        IDirect3DDevice8* const device = graphDevice(m_device);
        device->SetTexture(0u, nullptr);
        SetAlphaBlend(5u, 6u);
        result = drawPrimitiveUp(
            2u,
            0x44u,
            g_crossParticles.data(),
            static_cast<DWORD>(sizeof(GraphWeatherVertex44)),
            2 * g_crossParticleCount);
        return result;
    }

    void GRAPH::Tact(int worldTickFlag)
    {

        core::ApplicationDrawDispatcherState& appDraw = core::GlobalApplicationDrawDispatcherState();
        MAP& map = *Map;


        float preSquallCameraX = 0.0f;
        float preSquallCameraY = 0.0f;
        const bool squallShift =
            (m_renderFlags & 4u) != 0u &&
            (core::ApplicationFlags() & application_flags::BucketTimingActive) == 0u;
        if (squallShift)
        {
            preSquallCameraX = appDraw.cameraShiftX();
            preSquallCameraY = appDraw.cameraShiftY();
            const int jitterY = std::rand() % 9;
            const int jitterX = std::rand() % 9;
            map.SetShiftCoor(
                preSquallCameraX + static_cast<float>(m_sizeX) * 0.5f + 4.0f - static_cast<float>(jitterX),
                preSquallCameraY + static_cast<float>(m_sizeY) * 0.5f + 4.0f - static_cast<float>(jitterY),
                0);
        }

        auto setStage0MagFilter = [this](DWORD value) -> DWORD
        {
            return static_cast<DWORD>(D3D8SetSamplerState(graphDevice(m_device),
                0u, D3DSAMP_MAGFILTER, value));
        };

        if (worldTickFlag)
        {


            clearFrameBuffers(GammaRawCreateOpaque(0, 0, 0));


            setRenderStateCached(0x1Bu, 0u);
            setRenderStateCached(0x17u, 8u);
            setRenderStateCached(0x0Eu, 1u);
            setStage0MagFilter(1u);

            unlockBackBufferIfLocked();
            core::Application::drawSpritePass(appDraw, 0);
            graphDevice(m_device)->SetTexture(0u, nullptr);
            lockBackBuffer();
            core::Application::drawSpritePass(appDraw, 1);
            core::Application::drawSpritePass(appDraw, 2);
            core::Application::drawSpritePass(appDraw, 3);

            unlockBackBufferIfLocked();
            core::Application::drawSpritePass(appDraw, 4);

            lockBackBuffer();
            core::Application::drawSpritePass(appDraw, 5);
            core::Application::drawSpritePass(appDraw, 6);
            core::Application::drawSpritePass(appDraw, 7);

            unlockBackBufferIfLocked();


            setRenderStateCached(0x17u, 7u);
            core::Application::drawSpritePass(appDraw, 8);


            setRenderStateCached(0x0Eu, 0u);
            setRenderStateCached(0x1Bu, 1u);
            setStage0MagFilter(1u);
            core::Application::drawSpritePass(appDraw, 9);
            core::Application::drawSpritePass(appDraw, 10);

            setStage0MagFilter(2u);
            core::Application::drawSpritePass(appDraw, 11);
            drawSnowLightBuffer();
            drawCrossParticles();

            setStage0MagFilter(1u);
            core::Application::drawSpritePass(appDraw, 12);
            drawLineParticles();


            core::Application::drawSpritePass(appDraw, 14);

            lockBackBuffer();
            core::Application::drawSpritePass(appDraw, 13);
            unlockBackBufferIfLocked();

            core::Application::drawSpritePass(appDraw, 15);

            setRenderStateCached(0x0Eu, 1u);
            setStage0MagFilter(1u);

            lockBackBuffer();
            core::Application::drawSpritePass(appDraw, 16);
            unlockBackBufferIfLocked();

            core::Application::drawSpritePass(appDraw, 17);

            lockBackBuffer();
            core::Application::drawSpritePass(appDraw, 18);
            unlockBackBufferIfLocked();

            if (Mouse && Mouse->hardwareCursorEnabled() == 0)
            {
                SPRITE* node = mouseSprite();
                VID* const mouseVid = node ? node->Vid() : nullptr;
                if (mouseVid && (mouseVid->properties() & 0x00008000u) != 0u)
                {
                    while (node)
                    {
                        if (!node->isDrawSuppressed())
                            node->Draw();
                        node = node->childChain();
                    }
                }
            }
        }

        DrawSquall();
        if (squallShift)
        {
            map.SetShiftCoor(
                preSquallCameraX + static_cast<float>(m_sizeX) * 0.5f,
                preSquallCameraY + static_cast<float>(m_sizeY) * 0.5f,
                0);
        }
        unlockBackBufferIfLocked();

        releaseMoviePlaybackIfComplete();

        setRenderStateCached(0x1Du, 0u);
        DrawEffect(worldTickFlag);
        unlockBackBufferIfLocked();
    }


    int GRAPH::setRenderStateCached(DWORD renderState, DWORD value)
    {

        if (renderState == 0x17u || renderState == 0x0Eu || renderState == 7u)
            return 0;

        return static_cast<int>(graphDevice(m_device)->SetRenderState(
            static_cast<D3DRENDERSTATETYPE>(renderState),
            value));
    }


    void GRAPH::PlayMovie(const STRING* moviePath)
    {
        if (!moviePath)
            return;
        const int centerY = signedHalfFromFloatTowardZero(m_sizeY);
        const int centerX = signedHalfFromFloatTowardZero(m_sizeX);
        m_movie.Open(moviePath, centerX, centerY);
    }


    void MOVIE::Open(const STRING* moviePath, int centerX, int centerY)
    {
        (void)centerX;
        (void)centerY;
        if (!moviePath)
            return;
        Release();
        if (Graph)
            Graph->FlipToGDI();

        IGraphBuilder* graphBuilder = nullptr;
        HRESULT hr = ::CoCreateInstance(
            CLSID_FilterGraph,
            nullptr,
            1u,
            IID_IGraphBuilder,
            reinterpret_cast<void**>(&graphBuilder));
        pGraph = graphBuilder;
        if (hr < 0)
        {
            (void)logFileLoggerResourceError(g_fileLogger, "MOVIE", 3, "GraphBuilder", static_cast<int>(hr));
            return;
        }

        hr = graphBuilder->QueryInterface(
            IID_IMediaControl,
            reinterpret_cast<void**>(&pMediaControl));
        if (hr < 0)
        {
            (void)logFileLoggerResourceError(g_fileLogger, "MOVIE", 3, "MediaControl", static_cast<int>(hr));
            Release();
            return;
        }

        (void)graphBuilder->QueryInterface(IID_IMediaEvent, reinterpret_cast<void**>(&pEvent));

        wchar_t widePath[1024];
        (void)convertStringToWideChars(*moviePath, widePath, 1024);
        hr = graphBuilder->RenderFile(widePath, nullptr);
        if (hr < 0)
        {
            (void)logFileLoggerResourceError(g_fileLogger, "MOVIE", 4, "RenderFile", static_cast<int>(hr));
            Release();
            return;
        }

        (void)graphBuilder->QueryInterface(IID_IVideoWindow, reinterpret_cast<void**>(&pVidWin));
        IVideoWindow* const videoWindow = static_cast<IVideoWindow*>(pVidWin);
        const HWND hwnd = win::applicationWinInstance()->nativeWindow();
        videoWindow->put_Owner(reinterpret_cast<OAHWND>(hwnd));
        videoWindow->put_WindowStyle(0x44000000L);

        if (Graph)
            Graph->FlipToGDI();
        const int left = Graph ? graphConvertFloatToInt32(Graph->ViewXMin()) : 0;
        const int top = Graph ? graphConvertFloatToInt32(Graph->ViewYMin()) : 0;
        const int right = Graph ? graphConvertFloatToInt32(Graph->ViewXMax()) : 0;
        const int bottom = Graph ? graphConvertFloatToInt32(Graph->ViewYMax()) : 0;
        videoWindow->SetWindowPosition(left, top, right - left + 1, bottom - top + 1);

        IMediaControl* const mediaControl = static_cast<IMediaControl*>(pMediaControl);
        (void)mediaControl->Run();
        ::SetCapture(hwnd);
        const int cursorX = Graph ? static_cast<int>(Graph->SizeX()) : 0;
        const int cursorY = Graph ? static_cast<int>(Graph->SizeY()) : 0;
        (void)::SetCursorPos(cursorX, cursorY);
    }


    int MOVIE::Update() const noexcept
    {
        if (!pEvent)
            return 1;
        long eventCode = 0;
        static_cast<IMediaEvent*>(pEvent)->WaitForCompletion(0, &eventCode);
        return eventCode == 1 ? 1 : 0;
    }


    void MOVIE::Pause() noexcept
    {
        if (pMediaControl)
            (void)static_cast<IMediaControl*>(pMediaControl)->Pause();
    }

    void MOVIE::Resume() noexcept
    {
        if (pMediaControl)
            (void)static_cast<IMediaControl*>(pMediaControl)->Run();
    }


    void GRAPH::BeginPause()
    {
        m_graphFlags |= 0x1u;
        if (m_lockedBackBufferPixels)
        {
            graphSurface(m_backBuffer)->UnlockRect();
            m_lockedBackBufferPixels = nullptr;
        }
        FlipToGDI();
        ::DrawMenuBar(static_cast<HWND>(m_windowHandle));
        ::RedrawWindow(static_cast<HWND>(m_windowHandle), nullptr, nullptr, 0x400u);
        m_movie.Pause();
    }


    void GRAPH::EndPause()
    {
        m_graphFlags &= ~0x1u;
        m_movie.Resume();
    }

    int GRAPH::enterModalRenderState()
    {
        BeginPause();
        return 0;
    }

    int GRAPH::leaveModalRenderState()
    {
        EndPause();
        return 0;
    }


    int GRAPH::lockBackBuffer()
    {

        if (m_lockedBackBufferPixels)
        {
            return static_cast<int>(
                static_cast<std::intptr_t>(reinterpret_cast<std::uintptr_t>(m_lockedBackBufferPixels)));
        }
        IDirect3DSurface8* const surface = graphSurface(m_backBuffer);
        D3DLOCKED_RECT locked;
        const HRESULT hr = surface->LockRect(&locked, nullptr, 0);
        if (FAILED(hr))
            LOG::ResourceError("%s", 0, "backBuffer", 0, "GRAPH");
        m_lockedBackBufferPixels = locked.pBits;
        const int divisor = (m_graphFlags & 2u) != 0u ? 4 : 2;
        m_backBufferPitchPixels = locked.Pitch / divisor;
        return m_backBufferPitchPixels;
    }

    namespace
    {
        struct GraphOverlayVertexRaw
        {
            DWORD x;
            DWORD y;
            DWORD z;
            DWORD rhw;
            DWORD diffuse;
            DWORD specular;
        };


        GraphOverlayVertexRaw makeGraphOverlayVertex(int xRaw, int yRaw, int colorRaw)
        {
            GraphOverlayVertexRaw vertex{};
            vertex.x = static_cast<DWORD>(xRaw);
            vertex.y = static_cast<DWORD>(yRaw);
            vertex.z = 0x3F7FFFFEu;
            vertex.rhw = 0x3F800000u;
            vertex.diffuse = static_cast<DWORD>(colorRaw);
            vertex.specular = 0xFFFFFFFFu;
            return vertex;
        }
    }


    void GRAPH::ShadowBar(float x, float y, float x1, float y1, int shadow)
    {
        (void)drawAlphaOverlayQuad(
            static_cast<int>(floatRaw(x)), static_cast<int>(floatRaw(y)),
            static_cast<int>(floatRaw(x1)), static_cast<int>(floatRaw(y1)), shadow);
    }


    void GRAPH::LightBar(float x, float y, float x1, float y1, DWORD bright)
    {
        (void)drawAdditiveOverlayQuad(
            static_cast<int>(floatRaw(x)), static_cast<int>(floatRaw(y)),
            static_cast<int>(floatRaw(x1)), static_cast<int>(floatRaw(y1)),
            static_cast<int>(bright));
    }


    void GRAPH::Bar(float x, float y, float x1, float y1, DWORD color)
    {
        const GraphOverlayVertexRaw vertices[4] = {
            makeGraphOverlayVertex(static_cast<int>(floatRaw(x)),  static_cast<int>(floatRaw(y)),  static_cast<int>(color)),
            makeGraphOverlayVertex(static_cast<int>(floatRaw(x1)), static_cast<int>(floatRaw(y)),  static_cast<int>(color)),
            makeGraphOverlayVertex(static_cast<int>(floatRaw(x1)), static_cast<int>(floatRaw(y1)), static_cast<int>(color)),
            makeGraphOverlayVertex(static_cast<int>(floatRaw(x)),  static_cast<int>(floatRaw(y1)), static_cast<int>(color)),
        };

        unlockBackBufferIfLocked();
        graphDevice(m_device)->SetTexture(0, nullptr);
        (void)setRenderStateCached(0x0Fu, 0u);
        if ((color & 0xFF000000u) == 0xFF000000u)
            (void)setRenderStateCached(0x1Bu, 0u);
        else
            (void)SetAlphaBlend(5u, 6u);
        (void)setRenderStateCached(0x0Eu, 0u);
        (void)drawPrimitiveUp(6u, 0xC4u, vertices, 24u, 4);
        (void)setRenderStateCached(0x0Eu, 1u);
    }

    int GRAPH::drawAlphaOverlayQuad(int leftRaw, int topRaw, int rightRaw, int bottomRaw, int colorRaw)
    {

        const GraphOverlayVertexRaw vertices[4] = {
            makeGraphOverlayVertex(leftRaw, topRaw, colorRaw),
            makeGraphOverlayVertex(rightRaw, topRaw, colorRaw),
            makeGraphOverlayVertex(rightRaw, bottomRaw, colorRaw),
            makeGraphOverlayVertex(leftRaw, bottomRaw, colorRaw),
        };

        unlockBackBufferIfLocked();
        graphDevice(m_device)->SetTexture(0, nullptr);


        setRenderStateCached(0x0Fu, 0u);
        SetAlphaBlend(1u, 4u);
        setRenderStateCached(0x0Eu, 0u);
        drawPrimitiveUp(6u, 0xC4u, vertices, 24u, 4);
        return setRenderStateCached(0x0Eu, 1u);
    }


    void GRAPH::DrawDebugText(const char* text, ...)
    {
        m_loadingPresentationText.Assign(text ? text : "");

        (void)PreTact();

        const float top = m_viewportTop;
        const GraphOverlayVertexRaw vertices[4] = {
            makeGraphOverlayVertex(
                static_cast<int>(floatRaw(200.0f)),
                static_cast<int>(floatRaw(top + 5.0f)),
                static_cast<int>(0xFF000000u)),
            makeGraphOverlayVertex(
                static_cast<int>(floatRaw(800.0f)),
                static_cast<int>(floatRaw(top + 5.0f)),
                static_cast<int>(0xFF000000u)),
            makeGraphOverlayVertex(
                static_cast<int>(floatRaw(800.0f)),
                static_cast<int>(floatRaw(top + 30.0f)),
                static_cast<int>(0xFF000000u)),
            makeGraphOverlayVertex(
                static_cast<int>(floatRaw(200.0f)),
                static_cast<int>(floatRaw(top + 30.0f)),
                static_cast<int>(0xFF000000u)),
        };

        unlockBackBufferIfLocked();
        graphDevice(m_device)->SetTexture(0, nullptr);
        (void)setRenderStateCached(0x1Bu, 0u);
        (void)drawPrimitiveUp(6u, 0xC4u, vertices, 24u, 4);

        (void)drawTextColored(
            200.0f,
            top + 5.0f,
            m_loadingPresentationText.c_str(),
            g_colorGreen.color);

        (void)PostTact(1);
    }

    int GRAPH::drawAdditiveOverlayQuad(int leftRaw, int topRaw, int rightRaw, int bottomRaw, int colorRaw)
    {

        const GraphOverlayVertexRaw vertices[4] = {
            makeGraphOverlayVertex(leftRaw, topRaw, colorRaw),
            makeGraphOverlayVertex(rightRaw, topRaw, colorRaw),
            makeGraphOverlayVertex(rightRaw, bottomRaw, colorRaw),
            makeGraphOverlayVertex(leftRaw, bottomRaw, colorRaw),
        };

        unlockBackBufferIfLocked();
        graphDevice(m_device)->SetTexture(0, nullptr);


        setRenderStateCached(0x0Fu, 0u);
        setRenderStateCached(0x1Du, 0u);
        SetAlphaBlend(9u, 2u);
        setRenderStateCached(0x0Eu, 0u);
        drawPrimitiveUp(6u, 0xC4u, vertices, 24u, 4);
        return setRenderStateCached(0x0Eu, 1u);
    }

    int GRAPH::unlockBackBufferIfLocked()
    {

        if (!m_lockedBackBufferPixels)
            return 0;
        const HRESULT result = graphSurface(m_backBuffer)->UnlockRect();
        m_lockedBackBufferPixels = nullptr;
        return static_cast<int>(result);
    }

    int GRAPH::isMoviePlaybackComplete()
    {
        return m_movie.Update();
    }

    void GRAPH::releaseMoviePlaybackIfComplete()
    {

        if (!m_movie.pGraph)
            return;
        if (isMoviePlaybackComplete())
            releaseMoviePlayback();
    }


    void MOVIE::Release() noexcept
    {
        ::ReleaseCapture();
        void** const slots[4] = { &pVidWin, &pMediaControl, &pEvent, &pGraph };
        for (void** slot : slots)
        {
            void*& object = *slot;
            if (object)
                static_cast<IUnknown*>(object)->Release();
            object = nullptr;
        }
    }

    int GRAPH::releaseMoviePlayback()
    {
        m_movie.Release();
        return 0;
    }


    void GRAPH::SetEnvironment(DWORD env)
    {
        if ((env & 0x80000000u) != 0u)
        {
            m_renderFlags &= ~env;
            return;
        }

        if (env == 1u || env == 2u)
            m_renderFlags &= 0xFFFFFFFCu;
        if ((env & 0x00000C00u) != 0u)
            m_renderFlags &= 0xFFFFF3FFu;
        if ((env & 0x0000C000u) != 0u)
            m_renderFlags &= 0xFFFF3FFFu;
        m_renderFlags |= env;
    }

    void GRAPH::deinit()
    {

        releaseMoviePlayback();

        if (m_lockedBackBufferPixels)
        {
            graphSurface(m_backBuffer)->UnlockRect();
            m_lockedBackBufferPixels = nullptr;
        }

        if (m_device)
            graphDevice(m_device)->SetStreamSource(0, nullptr, 0u);

        destroyCD3DFont(m_textFont);

        if (m_softwareDepthBuffer)
        {
            ::operator delete(m_softwareDepthBuffer);
            m_softwareDepthBuffer = nullptr;
        }

        if (m_tempBuffer)
        {
            graphSurface(m_tempBuffer)->Release();
            m_tempBuffer = nullptr;
        }

        deleteBaseTextureThroughVirtualDestructor(m_alphaBuffer);
        deleteBaseTextureThroughVirtualDestructor(m_lightBuffer);
        deleteBaseTextureThroughVirtualDestructor(m_hiBuffer);

        if (m_device)
        {
            const ULONG result = graphDevice(m_device)->Release();
            m_device = nullptr;
            LOG::Write("d3dDevice release %i", static_cast<int>(result));
        }
        if (m_backBuffer)
        {
            const ULONG result = graphSurface(m_backBuffer)->Release();
            m_backBuffer = nullptr;
            LOG::Rewrite("backBuffer release %i", static_cast<int>(result));
        }
        if (m_direct3D)
        {
            const ULONG result = graphD3D(m_direct3D)->Release();
            m_direct3D = nullptr;
            LOG::Write("d3d release %i", static_cast<int>(result));
        }

        releaseMoviePlayback();
    }


    void GRAPH::PrintfXY(float x, float y, const char* format, ...)
    {
        char text[1024];
        va_list args;
        va_start(args, format);
        std::vsprintf(text, format, args);
        va_end(args);

        (void)drawTextColored(x, y, text, g_colorGreen.color);
    }

    void GRAPH::DrawText(float x, float y, const char* format, ...)
    {

        char text[1024];
        va_list args;
        va_start(args, format);
        std::vsprintf(text, format, args);
        va_end(args);


        (void)drawTextColored(x, y, text, g_colorGreen.color);
    }


    int GRAPH::drawTextColored(float x, float y, const char* text, DWORD color)
    {

        if (!m_textFont)
            return 0;
        unlockBackBufferIfLocked();
        return m_textFont->DrawText(x, y, color, text, 0u);
    }


    int GRAPH::drawStringColored(float x, float y, const STRING& text, DWORD color)
    {

        return drawTextColored(x, y, text.c_str(), color);
    }

    void GRAPH::drawBackBufferPixel(float x, float y, DWORD color)
    {

        if (!(x >= m_viewportLeft && x < m_viewportRight &&
              y >= m_viewportTop && y < m_viewportBottom))
            return;

        (void)lockBackBuffer();
        const int ix = graphConvertFloatToInt32(x);
        const int iy = graphConvertFloatToInt32(y);
        const std::ptrdiff_t index = static_cast<std::ptrdiff_t>(ix) +
                                     static_cast<std::ptrdiff_t>(iy) * static_cast<std::ptrdiff_t>(m_backBufferPitchPixels);

        if ((m_graphFlags & 2u) != 0u)
        {
            static_cast<DWORD*>(m_lockedBackBufferPixels)[index] = color;
            return;
        }

        const DWORD greenShift = 8u - g_color16GreenShift;
        const DWORD redShift = 16u - g_color16RedShift;
        const WORD packed = static_cast<WORD>(
            ((color >> greenShift) & g_color16GreenMask) |
            ((color >> redShift) & g_color16RedMask) |
            ((color >> 3u) & 0x1Fu));
        static_cast<WORD*>(m_lockedBackBufferPixels)[index] = packed;
    }


    void GRAPH::Line(float x0, float y0, float x1, float y1, DWORD color)
    {

        constexpr double kCoordinateLimit = 10000.0;
        if (std::fabs(static_cast<double>(x0)) > kCoordinateLimit)
        {
            (void)logFileLoggerResourceError(g_fileLogger, "%s", 4, "x in Line", 0, "GRAPH");
            x0 = 0.0f;
        }
        if (std::fabs(static_cast<double>(x1)) > kCoordinateLimit)
        {
            (void)logFileLoggerResourceError(g_fileLogger, "%s", 4, "x1 in Line", 0, "GRAPH");
            x1 = 0.0f;
        }
        if (std::fabs(static_cast<double>(y0)) > kCoordinateLimit)
        {
            (void)logFileLoggerResourceError(g_fileLogger, "%s", 4, "y in Line", 0, "GRAPH");
            y0 = 0.0f;
        }
        if (std::fabs(static_cast<double>(y1)) > kCoordinateLimit)
        {
            (void)logFileLoggerResourceError(g_fileLogger, "%s", 4, "y1 in Line", 0, "GRAPH");
            y1 = 0.0f;
        }

        int major = graphConvertFloatToInt32(x0);
        int minor = graphConvertFloatToInt32(y0);
        int majorStep = (x1 > x0) ? 1 : -1;
        int minorStep = (y1 > y0) ? 1 : -1;

        int dx = std::abs(graphConvertFloatToInt32(x1 - x0));
        int dy = std::abs(graphConvertFloatToInt32(y1 - y0));
        bool axesSwapped = false;
        if (dy > dx)
        {
            std::swap(major, minor);
            std::swap(dx, dy);
            std::swap(majorStep, minorStep);
            axesSwapped = true;
        }

        int error = dy * 2 - dx;
        const int errorAdvance = dy * 2;
        int remaining = dx;
        while (remaining != 0)
        {
            if (axesSwapped)
                drawBackBufferPixel(static_cast<float>(minor), static_cast<float>(major), color);
            else
                drawBackBufferPixel(static_cast<float>(major), static_cast<float>(minor), color);

            if (error >= 0)
            {
                const int subtract = dx * 2;
                do
                {
                    minor += minorStep;
                    error -= subtract;
                }
                while (error >= 0);
            }

            major += majorStep;
            error += errorAdvance;
            --remaining;
        }

        drawBackBufferPixel(x1, y1, color);
    }


    void GRAPH::Box(float left, float top, float right, float bottom, DWORD color)
    {

        Line(left, top, right, top, color);
        Line(left, bottom, right, bottom, color);
        Line(left, top, left, bottom, color);
        Line(right, top, right, bottom, color);
    }


    float GRAPH::ViewXMin() noexcept
    {
        return m_viewportLeft;
    }


    float GRAPH::ViewXMax() noexcept
    {
        return m_viewportRight;
    }


    float GRAPH::ViewYMin() noexcept
    {
        return m_viewportTop;
    }


    float GRAPH::ViewYMax() noexcept
    {
        return m_viewportBottom;
    }


    int GRAPH::setViewport(float left, float top, float right, float bottom)
    {

        m_viewportLeft = left;
        m_viewportRight = right;
        m_viewportTop = top;
        m_viewportBottom = bottom;
        g_softwareClipLeft = graphConvertFloatToInt32(left);
        g_softwareClipRight = graphConvertFloatToInt32(right);
        g_softwareClipTop = graphConvertFloatToInt32(top);
        g_softwareClipBottom = graphConvertFloatToInt32(bottom);

        int result = g_softwareClipBottom;
        IDirect3DDevice8* const device = graphDevice(m_device);
        if (!device)
            return result;

        D3DVIEWPORT8 viewport{};
        viewport.X = static_cast<DWORD>(graphConvertFloatToInt32(left));
        viewport.Y = static_cast<DWORD>(graphConvertFloatToInt32(top));
        viewport.Width = static_cast<DWORD>(graphConvertFloatToInt32(right - left));
        viewport.Height = static_cast<DWORD>(graphConvertFloatToInt32(bottom - top));
        viewport.MinZ = 0.0f;
        viewport.MaxZ = 1.0f;

        result = static_cast<int>(device->SetViewport(&viewport));
        if (result != D3D_OK)
            (void)logFileLoggerResourceError(g_fileLogger, "GRAPH", 8, "viewport", result);

        D3DMATRIX matrix{};
        matrix._11 = 2.0f / static_cast<float>(static_cast<std::int32_t>(viewport.Width));
        matrix._22 = -2.0f / static_cast<float>(static_cast<std::int32_t>(viewport.Height));
        matrix._33 = (1.0f / (viewport.MaxZ - viewport.MinZ)) * 0.0010000000474974513f;
        matrix._44 = 1.0f;

        result = static_cast<int>(device->SetTransform(D3DTS_PROJECTION, &matrix));
        if (result < 0)
            result = static_cast<int>(logFileLoggerResourceError(g_fileLogger, "GRAPH", 8, "Transform projection", result));
        return result;
    }


    int GRAPH::drawTextureRectClipped(const RECTI& destination, const RECTI& source, BASE_TEXTURE& texture)
    {


        RECTI dst = destination;
        RECTI src = source;
        if (static_cast<float>(dst.right) < m_viewportLeft ||
            static_cast<float>(dst.left) >= m_viewportRight ||
            static_cast<float>(dst.bottom) < m_viewportTop ||
            static_cast<float>(dst.top) >= m_viewportBottom)
        {
            return 0;
        }

        if (static_cast<float>(dst.left) < m_viewportLeft)
        {
            const int clippedLeft = graphConvertFloatToInt32(m_viewportLeft);
            src.left += clippedLeft - dst.left;
            dst.left = clippedLeft;
        }
        if (static_cast<float>(dst.top) < m_viewportTop)
        {
            const int clippedTop = graphConvertFloatToInt32(m_viewportTop);
            src.top += clippedTop - dst.top;
            dst.top = clippedTop;
        }
        if (static_cast<float>(dst.right) > m_viewportRight)
        {
            const int clippedRight = graphConvertFloatToInt32(m_viewportRight);
            src.right += clippedRight - dst.right;
            dst.right = clippedRight;
        }
        if (static_cast<float>(dst.bottom) > m_viewportBottom)
        {
            const int clippedBottom = graphConvertFloatToInt32(m_viewportBottom);
            src.bottom += clippedBottom - dst.bottom;
            dst.bottom = clippedBottom;
        }

        int sourcePitchBytes = 0;
        const std::uint16_t* sourcePixels = texture.lock16(&sourcePitchBytes, &src);

        const int width = dst.right - dst.left;
        const int height = dst.bottom - dst.top;
        const int pairCount = width / 2;
        const int sourcePitchWords = sourcePitchBytes / 2;
        const std::ptrdiff_t destinationOffset =
            static_cast<std::ptrdiff_t>(dst.left) +
            static_cast<std::ptrdiff_t>(dst.top) * static_cast<std::ptrdiff_t>(m_softwareDepthPitch);
        std::uint16_t* destinationRow = m_softwareDepthBuffer + destinationOffset;
        const std::uint16_t* sourceRow = sourcePixels;

        for (int row = 0; row < height; ++row)
        {
            if (pairCount > 0)
                std::memcpy(destinationRow, sourceRow, static_cast<std::size_t>(pairCount) * 4u);
            if ((width & 1) != 0)
                destinationRow[pairCount * 2] = sourceRow[pairCount * 2];
            sourceRow += sourcePitchWords;
            destinationRow += m_softwareDepthPitch;
        }
        return 0;
    }


    int GRAPH::drawPrimitiveUp(DWORD primitiveType, DWORD vertexShader, const void* vertexData, DWORD vertexStride, int vertexCount)
    {


        int primitiveCount = vertexCount;
        switch (primitiveType)
        {
        case 5:
        case 6:
            primitiveCount = static_cast<std::int32_t>(
                static_cast<std::uint32_t>(vertexCount) - 2u);
            break;
        case 2:
            primitiveCount = vertexCount / 2;
            break;
        case 4:
            primitiveCount = vertexCount / 3;
            break;
        default:
            break;
        }

        IDirect3DDevice8* const device = graphDevice(m_device);
        device->SetVertexShader(vertexShader);
        const HRESULT result = device->DrawPrimitiveUP(
            static_cast<D3DPRIMITIVETYPE>(primitiveType),
            static_cast<UINT>(primitiveCount),
            vertexData,
            static_cast<UINT>(vertexStride));
        if (result != D3D_OK)
            return static_cast<int>(logFileLoggerResourceError(
                g_fileLogger, "%s", 10, "DrawPrimitiveUP", static_cast<int>(result), "GRAPH"));
        return static_cast<int>(result);
    }

    int GRAPH::SetLayer(int layer, DWORD textureToken, DWORD srcToken, DWORD flags, DWORD fallbackFlags)
    {
        if (layer < 0 || layer >= 16)
            return layer;
        DWORD resolved = fallbackFlags;
        if (resolved == 0)
        {
            switch (layer)
            {
            case 1:
            case 3:
                resolved = 2304;
                break;
            case 2:
                resolved = 512;
                break;
            case 5:
            case 10:
                resolved = 1024;
                break;
            case 9:
                resolved = 1280;
                break;
            default:
                break;
            }
        }
        if (m_device)
        {
            IDirect3DDevice8* device = graphDevice(m_device);
            if (layer == 0)
            {
                device->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
                device->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
            }
            else if (layer == 5)
            {
                device->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
                device->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
                device->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_ONE);
            }
            else
            {
                device->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
                device->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
                device->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
            }
        }
        (void)textureToken;
        (void)srcToken;
        (void)flags;
        return static_cast<int>(resolved);
    }


    int GRAPH::initializeWindowDevice(void* hWnd)
    {


        core::RealCurrentTime = ::timeGetTime();


        if (zs1::g_UserMngr)
        {
            zs1::g_UserMngr->SetInt(true, "ScreenX", static_cast<int>(m_sizeX));
            zs1::g_UserMngr->SetInt(true, "ScreenY", static_cast<int>(m_sizeY));
            zs1::g_UserMngr->SetInt(true, "BPP", (m_graphFlags & 0x2u) != 0u ? 32 : 16);
            zs1::g_UserMngr->SetInt(true, "Device", m_selectedAdapterIndex);
            zs1::g_UserMngr->SetInt(true, "FullScreen", fullscreenRequested() ? 1 : 0);
        }


        const DD_DRIVER& startupCatalog = selectedAdapterRecord();
        DWORD flags = m_graphFlags & ~0x00000220u;


        flags = (flags & ~0x00000010u) |
                ((startupCatalog.capabilityFlags & 0x2u) != 0u ? 0x00000010u : 0u);
        flags &= ~0x00000010u;

        const bool transientFullscreenBuffer =
            (flags & 0x00000080u) != 0u && (flags & 0x00000400u) == 0u;
        flags = (flags & ~0x00000040u) |
                (transientFullscreenBuffer ? 0x00000040u : 0u);


        if ((startupCatalog.capabilityFlags & 0x1u) == 0u)
            flags |= 0x00000080u;
        m_graphFlags = flags;

        if (!fullscreenRequested())
        {
            const int desktopWidth = ::GetSystemMetrics(SM_CXSCREEN);
            const int desktopHeight = ::GetSystemMetrics(SM_CYSCREEN);
            if (static_cast<float>(desktopWidth) < m_sizeX)
                m_sizeX = static_cast<float>(desktopWidth);
            if (static_cast<float>(desktopHeight) < m_sizeY)
                m_sizeY = static_cast<float>(desktopHeight);

            const bool desktop32 = displayFormatBits(startupCatalog.desktopDisplayFormat) == 32;
            if (((m_graphFlags >> 1u) & 1u) != static_cast<DWORD>(desktop32))
                m_graphFlags = (m_graphFlags & ~0x2u) | (desktop32 ? 0x2u : 0u);
        }


        const int halfVideoBudget = static_cast<int>(startupCatalog.videoMemoryBudgetBytes) / 2;
        const float tripleBufferFootprint = m_sizeY * m_sizeX * 2.0f * 3.0f;
        if (!(tripleBufferFootprint < static_cast<float>(halfVideoBudget)))
            m_graphFlags &= ~0x00000040u;

        RECT windowRect{};
        RECT clientRect{};
        m_windowHandle = hWnd;
        HWND const window = static_cast<HWND>(m_windowHandle);
        GetWindowRect(window, &windowRect);
        GetClientRect(window, &clientRect);

        POINT clientTopLeft{clientRect.left, clientRect.top};
        POINT clientBottomRight{clientRect.right, clientRect.bottom};
        ClientToScreen(window, &clientTopLeft);
        ClientToScreen(window, &clientBottomRight);
        m_deviceLifecycleState = 0x17u;

        if (init(hWnd) != 0)
            return 1;

        if (fullscreenRequested() && (m_graphFlags & 0x00000400u) == 0u)
        {
            (void)setViewport(
                0.0f,
                0.0f,
                static_cast<float>(m_sizeX),
                static_cast<float>(m_sizeY));
        }
        else
        {
            (void)setViewport(
                static_cast<float>(clientTopLeft.x - windowRect.left),
                static_cast<float>(clientTopLeft.y - windowRect.top),
                static_cast<float>(clientBottomRight.x - windowRect.left),
                static_cast<float>(clientBottomRight.y - windowRect.top));
        }

        LOG::Write(
            "SetViewPort (%.0f,%.0f) - (%.0f,%.0f)",
            m_viewportLeft,
            m_viewportTop,
            m_viewportRight,
            m_viewportBottom);

        LOG::Write("%s", buildTextureStageDebugText());
        (void)restoreDeviceRenderStates();
        LOG::Write("%s", buildTextureStageDebugText());
        publishBaseTextureCaps(m_device);

        m_lightBuffer = new (std::nothrow) BASE_TEXTURE(0x100, 0x100, 0x29u, 0u);
        if (!m_lightBuffer->isLoaded())
        {
            LOG::ResourceError("%s", 3, "light buffer", 0, "GRAPH");
            return 1;
        }
        if (m_lightBuffer->format() == 0x29u)
        {
            std::array<DWORD, 256> palette{};
            for (DWORD value = 0; value < 256u; ++value)
                palette[value] = 0xFF000000u | value | (value << 8u) | (value << 16u);
            m_lightBuffer->createPaletteSlot(palette.data());
        }

        m_hiBuffer = new (std::nothrow) BASE_TEXTURE(0x100, 0x100, 0x17u, 0u);
        if (!m_hiBuffer->isLoaded())
        {
            LOG::ResourceError("%s", 3, "hiBuffer", 0, "GRAPH");
            return 1;
        }

        m_alphaBuffer = new (std::nothrow) BASE_TEXTURE(0x100, 0x100, 0x1Au, 0u);
        if (!m_alphaBuffer->isLoaded())
        {
            LOG::ResourceError("%s", 3, "alphaBuffer", 0, "GRAPH");
            return 1;
        }

        clearFrameBuffers(0xFF000000u);
        rebuildTextFont(STRING("Courier"), 7, 8);

        if (m_hiBuffer->format() == 0x17u)
        {
            g_color16RedMask = 0xF800u;
            g_color16GreenMask = 0x07E0u;
            g_color16RedShift = 8u;
            g_color16GreenShift = 3u;
        }
        else
        {
            g_color16RedMask = 0x7C00u;
            g_color16GreenMask = 0x03E0u;
            g_color16RedShift = 7u;
            g_color16GreenShift = 2u;
        }
        buildGraphIntensityPalette(m_intensityPalette16, m_hiBuffer->format() == 0x17u);

        STRING caps;


        if ((m_graphFlags & 0x00000004u) != 0u)
            appendCStringToString(caps, "ALPHAPALETTE ");
        if ((m_graphFlags & 0x00000010u) != 0u)
            appendCStringToString(caps, "NONGDI ");
        if ((m_graphFlags & 0x00000020u) != 0u)
            appendCStringToString(caps, "SOFTWARE ");
        else
            appendCStringToString(caps, "HARDWARE ");
        if ((m_graphFlags & 0x00000200u) != 0u)
            appendCStringToString(caps, "LOWDETAIL ");
        if ((m_graphFlags & 0x00000008u) != 0u)
            appendCStringToString(caps, "AGP ");
        if (!fullscreenRequested())
            appendCStringToString(caps, "WINDOWED ");
        if ((m_graphFlags & 0x00000040u) != 0u)
            appendCStringToString(caps, "TRIPLEBUFFER ");
        else
            appendCStringToString(caps, "DOUBLEBUFFER ");
        if ((m_graphFlags & 0x00000800u) != 0u)
            appendCStringToString(caps, "DOTPRODUCT3 ");
        if ((m_graphFlags & 0x00001000u) == 0u)
            appendCStringToString(caps, "NOTMODULATE2X ");
        if ((m_graphFlags & 0x00002000u) == 0u)
            appendCStringToString(caps, "CAN'T_Z_BLT ");
        if ((m_graphFlags & 0x00000002u) != 0u)
            appendCStringToString(caps, "COLOR32 ");
        if ((m_graphFlags & 0x00000100u) != 0u)
            appendCStringToString(caps, "VSYNC ");
        STRING pixelShader;
        constructFormattedString(pixelShader, "PIXELSHADER=%i", static_cast<int>(m_pixelShaderVersion));
        appendStringOwner(caps, pixelShader);
        LOG::Write("caps=%s", caps.c_str());

        if (!m_lockedBackBufferPixels)
        {
            D3DLOCKED_RECT lockedRect;
            const HRESULT lockResult =
                graphSurface(m_backBuffer)->LockRect(&lockedRect, nullptr, 0u);
            if (lockResult < 0)
                LOG::ResourceError("%s", 0, "backBuffer", 0, "GRAPH");

            m_lockedBackBufferPixels = lockedRect.pBits;
            const int bytesPerPixel = (m_graphFlags & 0x2u) != 0u ? 4 : 2;
            m_backBufferPitchPixels = lockedRect.Pitch / bytesPerPixel;
        }
        if (m_lockedBackBufferPixels)
        {
            graphSurface(m_backBuffer)->UnlockRect();
            m_lockedBackBufferPixels = nullptr;
        }

        const int bytesPerPixel = (m_graphFlags & 0x2u) != 0u ? 4 : 2;
        LOG::Write(
            "Pitch=%i zPitch=%i",
            m_backBufferPitchPixels * bytesPerPixel,
            2 * m_softwareDepthPitch);
        reloadPaletteLightBuffer();
        return 0;
    }


    int GRAPH::init(void* hWnd)
    {
        (void)hWnd;
        std::memset(&m_d3d8PresentParameters, 0, sizeof(m_d3d8PresentParameters));
        D3DPRESENT_PARAMETERS8& pp = m_d3d8PresentParameters;
        const DD_DRIVER& adapterRecord = selectedAdapterRecord();

        pp.Windowed = fullscreenRequested() ? FALSE : TRUE;
        pp.BackBufferCount = 1u;
        pp.MultiSampleType = D3DMULTISAMPLE_NONE;
        pp.SwapEffect = (m_graphFlags & 0x00000100u) != 0u
            ? D3DSWAPEFFECT_COPY_VSYNC_D3D8
            : D3DSWAPEFFECT_COPY;
        pp.hDeviceWindow = static_cast<HWND>(m_windowHandle);
        pp.EnableAutoDepthStencil = FALSE;
        pp.Flags = D3DPRESENTFLAG_LOCKABLE_BACKBUFFER;

        const int width = graphConvertFloatToInt32(m_sizeX);
        const int height = graphConvertFloatToInt32(m_sizeY);
        const int bitsPerPixel = (m_graphFlags & 0x00000002u) != 0u ? 32 : 16;

        if (fullscreenRequested())
        {
            const int modeIndex = adapterRecord.GetMode(width, height, bitsPerPixel);
            if (modeIndex < 0)
            {
                LOG::Write("Selected display mode %.0fx%.0f", static_cast<double>(m_sizeX), static_cast<double>(m_sizeY));
                LOG::ResourceError("%s", 10, "display mode", 0, "GRAPH");
                return 1;
            }
            m_selectedDisplayFormat = adapterRecord.displayModeFormats[modeIndex];
        }
        else
        {
            m_selectedDisplayFormat = adapterRecord.desktopDisplayFormat;
        }


        pp.BackBufferWidth = static_cast<UINT>(width);
        pp.BackBufferHeight = static_cast<UINT>(height);
        pp.BackBufferFormat = static_cast<D3DFORMAT>(m_selectedDisplayFormat);

        IDirect3D8* const d3d = graphD3D(m_direct3D);
        if (!d3d)
            return 1;

        const D3DDEVTYPE deviceType = (m_graphFlags & 0x00000020u) != 0u
            ? D3DDEVTYPE_REF
            : D3DDEVTYPE_HAL;

        IDirect3DDevice8* device = nullptr;
        HRESULT hr = d3d->CreateDevice(
            static_cast<UINT>(m_selectedAdapterIndex),
            deviceType,
            static_cast<HWND>(m_windowHandle),
            D3DCREATE_HARDWARE_VERTEXPROCESSING,
            &pp,
            &device);
        if (hr != D3D_OK)
        {
            hr = d3d->CreateDevice(
                static_cast<UINT>(m_selectedAdapterIndex),
                deviceType,
                static_cast<HWND>(m_windowHandle),
                D3DCREATE_SOFTWARE_VERTEXPROCESSING,
                &pp,
                &device);
        }
        if (hr != D3D_OK || !device)
        {
            LOG::ResourceError("%s", 3, "3dDevice", static_cast<int>(hr), "GRAPH");
            return 1;
        }
        m_device = device;

        IDirect3DSurface8* backBuffer = nullptr;
        hr = device->GetBackBuffer(0u, D3DBACKBUFFER_TYPE_MONO, &backBuffer);
        if (hr != D3D_OK || !backBuffer)
        {
            LOG::ResourceError("%s", 9, "BackBuffer", static_cast<int>(hr), "GRAPH");
            return 1;
        }
        m_backBuffer = backBuffer;


        (void)device->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
        (void)device->SetRenderState(D3DRS_ZENABLE, FALSE);

        D3DCAPS8 caps{};
        hr = device->GetDeviceCaps(&caps);
        if (hr != D3D_OK)
        {
            LOG::ResourceError("%s", 9, "Caps", static_cast<int>(hr), "GRAPH");
            return 1;
        }
        DWORD derivedCapabilityFlags = 0u;
        if ((caps.TextureOpCaps & 0x00800000u) != 0u)
            derivedCapabilityFlags |= 0x00000800u;
        if ((caps.DevCaps & 0x00001000u) != 0u)
            derivedCapabilityFlags |= 0x00000008u;
        if ((caps.TextureCaps & 0x00000080u) != 0u)
            derivedCapabilityFlags |= 0x00000004u;
        if ((caps.TextureOpCaps & 0x00000010u) != 0u)
            derivedCapabilityFlags |= 0x00001000u;
        m_graphFlags = (m_graphFlags & 0xFFFFE7F3u) | derivedCapabilityFlags;
        m_pixelShaderVersion = caps.PixelShaderVersion;


        IDirect3DSurface8* tempBuffer = nullptr;
        hr = device->CreateImageSurface(
            pp.BackBufferWidth, pp.BackBufferHeight, pp.BackBufferFormat, &tempBuffer);
        if (hr != D3D_OK || !tempBuffer)
        {
            LOG::ResourceError("%s", 3, "tempBuffer", static_cast<int>(hr), "GRAPH");
            return 1;
        }
        m_tempBuffer = tempBuffer;

        const std::size_t depthWords =
            static_cast<std::size_t>(pp.BackBufferWidth) * static_cast<std::size_t>(pp.BackBufferHeight);
        m_softwareDepthBuffer = static_cast<std::uint16_t*>(
            ::operator new(depthWords * sizeof(std::uint16_t), std::nothrow));
        if (depthWords != 0u && !m_softwareDepthBuffer)
            return 1;
        m_softwareDepthPitch = width;

        m_viewportLeft = 0.0f;
        m_viewportTop = 0.0f;
        m_viewportRight = m_sizeX;
        m_viewportBottom = m_sizeY;

        return 0;
    }


    bool GRAPH::fullscreenRequested() const noexcept
    {
        return (m_graphFlags & 0x80u) != 0u;
    }

    void GRAPH::setFullscreenRequested(bool enabled) noexcept
    {
        m_graphFlags = enabled ? (m_graphFlags | 0x80u) : (m_graphFlags & ~0x80u);
    }


    ANGLE GRAPH::WindDirectionAngle() const noexcept
    {
        return ANGLE(static_cast<unsigned char>(m_windDirection));
    }


    float GRAPH::SizeX() const
    {
        return m_sizeX;
    }


    float GRAPH::SizeY() const
    {
        return m_sizeY;
    }


    void GRAPH::SetWind(int speed, ANGLE direction)
    {
        m_windSpeed = static_cast<float>(speed) * 0.001f;
        m_windDirection = direction.value;
    }


    int GRAPH::Effect(int effect, int argument1, int argument2, int duration)
    {

        int result = effect;
        if (effect < 0 || effect >= 16)
            return result;

        if (effect == 3 || effect == 9 || effect == 10)
        {
            m_effectStartTimes[10] = 0;
            m_effectStartTimes[9] = 0;
            m_effectStartTimes[3] = 0;
        }

        std::uint32_t rawDuration = static_cast<std::uint32_t>(duration);
        if (rawDuration == 0u)
        {
            rawDuration = 0x400u;
            switch (effect)
            {
            case 2: rawDuration = 0x200u; break;
            case 1:
            case 3: rawDuration = 0x900u; break;
            case 9: rawDuration = 0x500u; break;
            default: break;
            }
        }

        const std::size_t index = static_cast<std::size_t>(effect);


        m_effectStartTimes[index] = rawDuration == 1u ? 0u : core::RealCurrentTime;
        m_effectArgument1[index] = static_cast<std::uint32_t>(argument1);
        m_effectDurations[index] = rawDuration;
        m_effectArgument2[index] = static_cast<std::uint32_t>(argument2);

        if (effect == 0)
        {
            std::memset(m_effectStartTimes, 0, sizeof(m_effectStartTimes));
            return 0;
        }

        if (effect == 5)
        {
            if (m_tempBuffer)
            {
                m_effectStartTimes[10] = 0;
                m_effectStartTimes[9] = 0;
                m_effectStartTimes[3] = 0;
                const HRESULT copyResult = graphDevice(m_device)->CopyRects(
                    graphSurface(m_backBuffer), nullptr, 0u,
                    static_cast<IDirect3DSurface8*>(m_tempBuffer), nullptr);
                result = static_cast<int>(copyResult);
                if (copyResult != D3D_OK)
                {
                    result = static_cast<int>(logFileLoggerResourceError(
                        g_fileLogger, "GRAPH", 1, "for EFF_ALPHAAPPEAR",
                        static_cast<int>(copyResult)));
                }
            }
            return result;
        }

        if (effect == 2)
        {


            const float cameraY = core::GlobalApplicationDrawDispatcherState().cameraShiftY();
            const float snapshotX = m_sizeX * 0.5f + cameraY;
            const float snapshotY = m_sizeY * 0.5f + cameraY;
            std::memcpy(&m_effectSnapshotXBits, &snapshotX, sizeof(m_effectSnapshotXBits));
            std::memcpy(&m_effectSnapshotYBits, &snapshotY, sizeof(m_effectSnapshotYBits));
            return static_cast<int>(reinterpret_cast<std::uintptr_t>(core::ApplicationOwner()));
        }

        if (effect == 11)
            m_effectGammaPair = m_gammaPair;

        return result;
    }


    void GRAPH::DrawEffect(int drawEffects)
    {

        std::uint32_t now = core::RealCurrentTime;

        const std::uint32_t effect5Start = m_effectStartTimes[5];
        if (effect5Start != 0u)
        {
            const std::uint32_t elapsed = now - effect5Start;
            const std::uint32_t duration = m_effectDurations[5];
            if (elapsed >= duration)
            {
                m_effectStartTimes[5] = 0u;
            }
            else
            {
                SetAlphaBlend(6u, 5u);
                std::uint32_t tileNow = now;
                const float left = m_viewportLeft;
                const float top = m_viewportTop;
                const float right = m_viewportRight;
                const float bottom = m_viewportBottom;
                for (float tileY = top; tileY < bottom; tileY += 256.0f)
                {
                    for (float tileX = left; tileX < right; tileX += 256.0f)
                    {
                        const float tileWidth = (tileX + 256.0f < right) ? 256.0f : (right - tileX);
                        const float tileHeight = (tileY + 256.0f < bottom) ? 256.0f : (bottom - tileY);
                        const RECTI destination{
                            graphConvertFloatToInt32(tileX - left),
                            graphConvertFloatToInt32(tileY - top),
                            graphConvertFloatToInt32(tileX + tileWidth - left),
                            graphConvertFloatToInt32(tileY + tileHeight - top)};
                        const RECTI copyOrigin{0, 0, 0, 0};
                        const RECTI source{0, 0,
                            graphConvertFloatToInt32(tileWidth),
                            graphConvertFloatToInt32(tileHeight)};
                        if (drawEffects != 0)
                        {
                            int copyResult = -1;
                            copyResult = m_hiBuffer->PrepareSurfaceCopy(
                                static_cast<IDirect3DSurface8*>(m_tempBuffer), destination, &copyOrigin);
                            if (copyResult == 0)
                            {
                                const DWORD tileElapsed = tileNow - effect5Start;
                                const DWORD alpha = (tileElapsed << 8u) / duration;


                                const Color diffuse(static_cast<int>(alpha), 255, 255, 255);
                                const Color specular(0, 0, 0);
                                const Gamma colors(diffuse, specular);
                                m_hiBuffer->DrawFixedDepthRectangle(destination, source, &colors.first);
                            }

                            tileNow = core::RealCurrentTime;
                        }
                    }
                }
            }
            now = core::RealCurrentTime;
        }


        const std::uint32_t effect2Start = m_effectStartTimes[2];
        if (effect2Start != 0u)
        {
            const std::uint32_t elapsed = now - effect2Start;
            const std::uint32_t duration = m_effectDurations[2];
            MAP* const map = Map;
            const float startX = rawFloat(m_effectSnapshotXBits);
            const float startY = rawFloat(m_effectSnapshotYBits);
            const float targetX = static_cast<float>(static_cast<std::int32_t>(m_effectArgument1[2]));
            const float targetY = static_cast<float>(static_cast<std::int32_t>(m_effectArgument2[2]));
            if (elapsed > duration)
            {
                map->SetShiftCoor(targetX, targetY, 0);
                m_effectStartTimes[2] = 0u;
            }
            else
            {

                const float t =
                    static_cast<float>(static_cast<std::int32_t>(elapsed)) /
                    static_cast<float>(static_cast<std::int32_t>(duration));
                map->SetShiftCoor((targetX - startX) * t + startX,
                                  (targetY - startY) * t + startY,
                                  0);
            }
            now = core::RealCurrentTime;
        }

        const DWORD screenRightRaw = floatRaw(static_cast<float>(m_sizeX));
        const DWORD screenBottomRaw = floatRaw(static_cast<float>(m_sizeY));


        const std::uint32_t effect1Start = m_effectStartTimes[1];
        if (effect1Start != 0u)
        {
            const std::uint32_t duration = m_effectDurations[1];
            const std::uint32_t elapsed = now - effect1Start;
            if (elapsed >= duration)
            {
                m_effectStartTimes[1] = 0u;
            }
            else if (drawEffects != 0)
            {
                const std::uint32_t ninth = duration / 9u;
                DWORD numerator = 0u;
                DWORD denominator = duration;
                if (elapsed >= ninth)
                {
                    numerator = (duration - elapsed) << 8u;
                    denominator = duration - ninth;
                }
                else
                {
                    numerator = (elapsed * 9u) << 8u;
                }
                const DWORD scale = numerator / denominator;
                const DWORD color = graphScaleRgb(m_effectArgument1[1], scale);
                drawAdditiveOverlayQuad(0, 0, static_cast<int>(screenRightRaw), static_cast<int>(screenBottomRaw), static_cast<int>(color));
                drawAdditiveOverlayQuad(0, 0, static_cast<int>(screenRightRaw), static_cast<int>(screenBottomRaw), static_cast<int>(color));
                drawAdditiveOverlayQuad(0, 0, static_cast<int>(screenRightRaw), static_cast<int>(screenBottomRaw), static_cast<int>(color));
            }
            now = core::RealCurrentTime;
        }


        const std::uint32_t effect3Start = m_effectStartTimes[3];
        if (effect3Start != 0u)
        {
            const std::uint32_t duration = m_effectDurations[3];
            const std::uint32_t elapsed = now - effect3Start;
            if (elapsed >= duration)
            {
                m_effectStartTimes[3] = 0u;
            }
            else if (drawEffects != 0)
            {
                const std::uint32_t riseEnd = (4u * duration) / 9u;
                const std::uint32_t plateauEnd = (5u * duration) / 9u;
                DWORD level = 255u;
                if (elapsed < riseEnd)
                    level = (2304u * elapsed) / (4u * duration);
                else if (elapsed > plateauEnd)
                    level = (2304u * (duration - elapsed)) / (4u * duration);
                const DWORD color = graphGrayRgb(level);
                drawAlphaOverlayQuad(0, 0, static_cast<int>(screenRightRaw), static_cast<int>(screenBottomRaw), static_cast<int>(color));
            }
            now = core::RealCurrentTime;
        }


        const std::uint32_t effect9Start = m_effectStartTimes[9];
        if (effect9Start != 0u)
        {
            const std::uint32_t duration = m_effectDurations[9];
            const std::uint32_t elapsed = now - effect9Start;
            if (elapsed >= duration)
            {
                m_effectStartTimes[9] = 0u;
            }
            else if (drawEffects != 0)
            {
                const DWORD level = elapsed >= (4u * duration) / 5u
                    ? 255u
                    : (1280u * elapsed) / (4u * duration);
                const DWORD color = graphGrayRgb(level);
                drawAlphaOverlayQuad(0, 0, static_cast<int>(screenRightRaw), static_cast<int>(screenBottomRaw), static_cast<int>(color));
            }

            now = core::RealCurrentTime;
        }


        const std::uint32_t effect10Start = m_effectStartTimes[10];
        if (effect10Start != 0u)
        {
            const std::uint32_t duration = m_effectDurations[10];
            const std::uint32_t elapsed = now - effect10Start;
            if (elapsed >= duration)
            {
                m_effectStartTimes[10] = 0u;
            }
            else if (drawEffects != 0)
            {
                const DWORD level = ((duration - elapsed) << 8u) / duration;
                const DWORD color = graphGrayRgb(level);
                drawAlphaOverlayQuad(0, 0, static_cast<int>(screenRightRaw), static_cast<int>(screenBottomRaw), static_cast<int>(color));
            }
        }

        now = core::RealCurrentTime;
        const std::uint32_t effect11Start = m_effectStartTimes[11];
        if (effect11Start != 0u)
        {
            const std::uint32_t duration = m_effectDurations[11];
            const std::uint32_t elapsed = now - effect11Start;
            const Gamma targetGamma(Gamma::DECODE, m_effectArgument1[11]);

            if (elapsed >= duration)
            {
                setGamma(targetGamma);
                m_effectStartTimes[11] = 0u;
                return;
            }

            if (drawEffects != 0)
            {
                const float t =
                    static_cast<float>(elapsed) / static_cast<float>(duration);
                setGamma(interpolateGamma(m_effectGammaPair, targetGamma, t));
            }
        }
    }


    int GRAPH::GetEffectState(int effect) const
    {

        if (effect < 1 || effect > 15)
            return -1;

        const std::size_t index = static_cast<std::size_t>(effect);
        const std::uint32_t start = m_effectStartTimes[index];
        if (start == 0u)
            return -1;

        const std::uint32_t now = core::RealCurrentTime;
        const std::uint32_t elapsedTimes100 = (now - start) * 100u;
        return static_cast<int>(elapsedTimes100 / m_effectDurations[index]);
    }


    void GRAPH::setGamma(const Gamma& rawGamma)
    {

        if (m_gammaPair.first == rawGamma.first &&
            m_gammaPair.second == rawGamma.second)
            return;

        m_gammaPair = rawGamma;

        auto& appVidTable = core::GlobalApplicationVidTable();
        const int rawVidCount = appVidTable.count();
        for (int slot = 0; slot < static_cast<int>(core::ApplicationVidTable::kCapacity); ++slot)
        {

            VID* const vid = (slot < rawVidCount) ? appVidTable.slot(slot) : nullptr;


            if (!vid || vid == EmptyVid)
                continue;

            vid->SetGamma(rawGamma, 4);
        }
    }

    void GRAPH::setGamma(DWORD diffuse, DWORD specular)
    {
        setGamma(Gamma{diffuse, specular});
    }


    void GRAPH::DrawLightSource(float x, float y, float z, float sizeXValue, float sizeYValue, DWORD color)
    {


        g_lightBufferToggle ^= 1u;

        if ((color & 0x00FFFFFFu) == 0u)
            return;

        const int sizeX = graphConvertFloatToInt32(sizeXValue);
        const int sizeY = graphConvertFloatToInt32(sizeYValue);
        int halfExtentX = static_cast<std::int32_t>(static_cast<std::uint32_t>(sizeX / 2) * 3u) & ~3;
        int halfExtentY = static_cast<std::int32_t>(static_cast<std::uint32_t>(sizeY / 2) * 3u) & ~3;
        if (halfExtentX > 512)
            halfExtentX = 512;
        if (halfExtentY > 512)
            halfExtentY = 512;

        const int depthScaleDivisor = static_cast<std::int32_t>(
            static_cast<std::uint32_t>(sizeX) * static_cast<std::uint32_t>(sizeY)) / 500;
        const int sourceZ = graphConvertFloatToInt32(z);
        const int doubledSourceZ = static_cast<std::int32_t>(static_cast<std::uint32_t>(sourceZ) * 2u);
        const int lightCenterZ = static_cast<std::int32_t>(static_cast<std::uint32_t>(doubledSourceZ / 3) + 8u);
        const float centerX = x;
        const float centerY = y - (static_cast<float>(lightCenterZ) - z);

        const float right = centerX + static_cast<float>(halfExtentX);
        if (graphFcompC0(right, m_viewportLeft))
            return;
        const float left = centerX - static_cast<float>(halfExtentX);
        if (!graphFcompC0(left, m_viewportRight))
            return;
        const float bottom = centerY + static_cast<float>(halfExtentY);
        if (graphFcompC0(bottom, m_viewportTop))
            return;
        const float top = centerY - static_cast<float>(halfExtentY);
        if (!graphFcompC0(top, m_viewportBottom))
            return;

        RECTI destination{
            graphConvertFloatToInt32(left),
            graphConvertFloatToInt32(top),
            graphConvertFloatToInt32(right),
            graphConvertFloatToInt32(bottom)
        };
        RECTI source{0, 0, halfExtentX / 2, halfExtentY / 2};

        BASE_TEXTURE* const texture = g_lightBufferToggle != 0u ? m_hiBuffer : m_lightBuffer;

        int texturePitchBytes = 0;
        std::uint16_t* const locked = texture->lock16(&texturePitchBytes, &source);
        if (!locked)
        {
            LOG::ResourceError("%s", 10, "light buffer", 0, "GRAPH");
            return;
        }

        const bool paletteTexture = texture->format() == 0x29u;
        const int texturePitch = paletteTexture ? texturePitchBytes : texturePitchBytes / 2;
        std::uint8_t* const output8 = reinterpret_cast<std::uint8_t*>(locked);
        std::uint16_t* const output16 = locked;
        const std::uint16_t* const worldDepth = softwareDepthBuffer();
        const int worldDepthPitch = softwareDepthPitch();

        int outputY = 0;
        for (int yOffset = -halfExtentY; yOffset < halfExtentY; yOffset += 4, outputY += 4)
        {
            const float sampleY = centerY + static_cast<float>(yOffset);
            const float sampleY3 = sampleY + 3.0f;
            const int outputRow = outputY / 4;

            for (int xOffset = -halfExtentX; xOffset < halfExtentX; xOffset += 4)
            {
                const float sampleX = centerX + static_cast<float>(xOffset);
                const float sampleX3 = sampleX + 3.0f;
                int sampledDepth = 0x7FFF;

                if (!graphFcompC0(sampleX, m_viewportLeft) &&
                    graphFcompC0(sampleX, m_viewportRight) &&
                    !graphFcompC0(sampleY, m_viewportTop) &&
                    graphFcompC0(sampleY, m_viewportBottom))
                {
                    const int x = static_cast<std::int32_t>(static_cast<std::uint32_t>(xOffset) + static_cast<std::uint32_t>(graphConvertFloatToInt32(centerX)));
                    const int y = static_cast<std::int32_t>(static_cast<std::uint32_t>(yOffset) + static_cast<std::uint32_t>(graphConvertFloatToInt32(centerY)));
                    sampledDepth = static_cast<int>(worldDepth[y * worldDepthPitch + x] >> 3u) - 128;
                }

                int sampledDepth3 = 0x7FFF;
                if (!graphFcompC0(sampleX3, m_viewportLeft) &&
                    graphFcompC0(sampleX3, m_viewportRight) &&
                    !graphFcompC0(sampleY3, m_viewportTop) &&
                    graphFcompC0(sampleY3, m_viewportBottom))
                {
                    const int x = static_cast<std::int32_t>(static_cast<std::uint32_t>(xOffset) + static_cast<std::uint32_t>(graphConvertFloatToInt32(centerX)) + 3u);
                    const int y = static_cast<std::int32_t>(static_cast<std::uint32_t>(yOffset) + static_cast<std::uint32_t>(graphConvertFloatToInt32(centerY)) + 3u);
                    sampledDepth3 = static_cast<int>(worldDepth[y * worldDepthPitch + x] >> 3u) - 128;
                }

                if (sampledDepth3 < sampledDepth)
                    sampledDepth = sampledDepth3;
                if (sampledDepth == 0x7FFF)
                    continue;

                const int vertical = static_cast<std::int32_t>(
                    static_cast<std::uint32_t>(yOffset) + static_cast<std::uint32_t>(sampledDepth) - static_cast<std::uint32_t>(lightCenterZ));
                const int depthDelta = static_cast<std::int32_t>(
                    static_cast<std::uint32_t>(sampledDepth) - static_cast<std::uint32_t>(sourceZ));
                const int xSquare = static_cast<std::int32_t>(
                    static_cast<std::uint32_t>(xOffset) * static_cast<std::uint32_t>(xOffset));
                const int verticalSquare = static_cast<std::int32_t>(
                    static_cast<std::uint32_t>(vertical) * static_cast<std::uint32_t>(vertical));
                const int verticalNine = static_cast<std::int32_t>(
                    static_cast<std::uint32_t>(verticalSquare) * 9u);
                const int depthSquare = static_cast<std::int32_t>(
                    static_cast<std::uint32_t>(depthDelta) * static_cast<std::uint32_t>(depthDelta));
                const int metric = static_cast<std::int32_t>(
                    static_cast<std::uint32_t>(xSquare) +
                    static_cast<std::uint32_t>(verticalNine / 4) +
                    static_cast<std::uint32_t>(depthSquare / 4));
                int intensity = depthScaleDivisor != 0
                    ? 256 - metric / depthScaleDivisor
                    : 256 - metric;
                if (intensity < 0)
                    intensity = 0;
                else if (intensity > 255)
                    intensity = 255;

                const int outputX = (xOffset + halfExtentX) / 4;
                if (paletteTexture)
                    output8[outputRow * texturePitch + outputX] = static_cast<std::uint8_t>(intensity);
                else
                    output16[outputRow * texturePitch + outputX] = m_intensityPalette16[static_cast<std::size_t>(intensity)];
            }
        }

        texture->unlock();
        setRenderStateCached(29u, 0u);
        SetAlphaBlend(9u, 2u);

        ++source.left;
        --source.right;
        ++source.top;
        --source.bottom;

        const float deviceDepth =
            (z + sizeXValue + 50.0f) * 0.0001220703125f + 0.015625f;
        const DWORD deviceDepthRaw = graphFloatBits(deviceDepth);
        const DWORD colors[2] = {~color, 0xFF000000u};
        texture->DrawDepthRectangle(deviceDepthRaw, deviceDepthRaw, destination, source, colors);
    }


    void GRAPH::LoadParameters(RESOURCE* map)
    {
        if (!map)
            return;


        Gamma raw{};
        std::uint32_t direction = 0u;
        map->read(&m_renderFlags, 4);
        map->read(&raw.first, 4);
        map->read(&raw.second, 4);
        setGamma(raw);
        map->read(&direction, 4);
        m_windDirection = (m_windDirection & 0xFFFFFF00u) | (direction & 0xFFu);
        map->read(&m_windSpeed, 4);
    }


    void GRAPH::OldLoadParameters(RESOURCE* map)
    {
        if (!map)
            return;


        std::uint32_t packedGamma = 0u;
        std::uint8_t direction = 0u;
        std::int16_t magnitude = 0;
        map->read(&m_renderFlags, 4);
        map->read(&packedGamma, 4);
        const Gamma raw(Gamma::DECODE, packedGamma);
        setGamma(raw);
        map->read(&direction, 1);
        map->read(&magnitude, 2);
        m_windDirection = (m_windDirection & 0xFFFFFF00u) | direction;
        m_windSpeed = static_cast<float>(magnitude) * 0.001f;
    }
}
