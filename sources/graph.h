#pragma once
#include "core/types.h"
#include "core/as_string.h"
#include "core/configuration.h"
#include "graphics/rect.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace as1 { namespace win
{
    struct DialogItemRef;
} }

namespace as1
{

    extern DWORD g_color16RedMask;
    extern DWORD g_color16GreenMask;
    extern DWORD g_color16RedShift;
    extern DWORD g_color16GreenShift;

    extern DWORD g_packedSoftwareDepth;
    extern WORD g_softwareDepthWordPrimary;
    extern WORD g_softwareDepthWordSecondary;
    extern const DWORD* g_softwarePaletteLookup;

    extern int g_softwareClipLeft;
    extern int g_softwareClipRight;
    extern int g_softwareClipTop;
    extern int g_softwareClipBottom;

    class RESOURCE;
    class MAP;
    class SPRITE;
    class VID;
    class VID_HARDWARE_Z;
    class BASE_TEXTURE;
    class CD3DFont;
    class GRAPH;
    struct ANGLE;

    class MOVIE
    {
    public:
        MOVIE() noexcept : pGraph(nullptr), pMediaControl(nullptr), pEvent(nullptr), pVidWin(nullptr) {}
        void Open(const STRING* file, int centerX, int centerY);
        int Update() const noexcept;
        void Pause() noexcept;
        void Resume() noexcept;

        void Release() noexcept;
        void* object(std::size_t index) const noexcept
        {
            const void* const slots[4] = { pGraph, pMediaControl, pEvent, pVidWin };
            return index < 4u ? const_cast<void*>(slots[index]) : nullptr;
        }

        void* pGraph;
        void* pMediaControl;
        void* pEvent;
        void* pVidWin;
    };


    extern GRAPH* Graph;

    CD3DFont* createVidFontOwner(const STRING& face, int sizeX, int sizeY);
    void destroyVidFontOwner(CD3DFont*& owner) noexcept;
    int invalidateVidFontOwner(CD3DFont* owner) noexcept;
    int restoreVidFontOwner(CD3DFont* owner) noexcept;
    bool drawVidFontOwner(CD3DFont* owner, float x, float y, DWORD color,
                                const char* text, DWORD flags) noexcept;

    struct DD_DRIVER
    {
        char description[0x28];
        std::uint8_t rawIdentifierTail28[0x14];
        DWORD videoMemoryBudgetBytes;
        DWORD displayModeCount;
        DWORD displayModeWidths[16];
        DWORD displayModeHeights[16];
        DWORD displayModeFormats[16];
        DWORD depthStencilFormats[16];
        DWORD desktopDisplayFormat;
        DWORD capabilityFlags;

        int GetMode(int width, int height, int bitsPerPixel) const noexcept;
        STRING GetModeDesctription(int modeIndex) const;
    };


    class GRAPH
    {
    public:
        GRAPH();
        ~GRAPH();


        virtual int restoreDeviceRenderStates() noexcept;

        int init(void* hWnd);

        int initializeWindowDevice(void* hWnd);
        void deinit();
        void* direct3DHandle() const { return m_direct3D; }
        void* deviceHandle() const { return m_device; }
        BASE_TEXTURE* lightBuffer() const { return m_lightBuffer; }
        std::uint16_t intensityPaletteEntry(std::size_t index) const noexcept { return m_intensityPalette16[index]; }
        BASE_TEXTURE* hiBuffer() const { return m_hiBuffer; }
        BASE_TEXTURE* alphaBuffer() const { return m_alphaBuffer; }
        void* temporarySurface() const { return m_tempBuffer; }

        void DrawText(float x, float y, const char* format, ...);

        void drawBackBufferPixel(float x, float y, DWORD color);
        void Line(float x0, float y0, float x1, float y1, DWORD color);
        void Box(float left, float top, float right, float bottom, DWORD color);

        int PreTact();
        int PostTact(int presentFlag);

        void DrawDebugText(const char* text, ...);
        void FlipToGDI();
        int setViewport(float left, float top, float right, float bottom);
        int drawTextureRectClipped(const RECTI& destination, const RECTI& source, BASE_TEXTURE& texture);
        int drawPrimitiveUp(DWORD primitiveType, DWORD vertexShader, const void* vertexData, DWORD vertexStride, int vertexCount);
        int SetLayer(int layer, DWORD textureToken, DWORD srcToken, DWORD flags, DWORD fallbackFlags);
        float viewportLeft() const noexcept { return m_viewportLeft; }
        float viewportRight() const noexcept { return m_viewportRight; }
        float viewportTop() const noexcept { return m_viewportTop; }
        float viewportBottom() const noexcept { return m_viewportBottom; }
        float ViewXMin() noexcept;
        float ViewXMax() noexcept;

        float ViewYMin() noexcept;
        float ViewYMax() noexcept;
        void rawSetSoftwareClipBounds(int left, int top, int right, int bottom) noexcept
        {
            g_softwareClipLeft = left;
            g_softwareClipTop = top;
            g_softwareClipRight = right;
            g_softwareClipBottom = bottom;
        }

        int SetAlphaBlend(DWORD srcBlend, DWORD dstBlend);
        int clearFrameBuffers(DWORD color);
        int setRenderStateCached(DWORD renderState, DWORD value);
        void Tact(int worldTickFlag);
        void DrawSquall();
        void drawFogBufferOverlay(float left, float top, float right, float bottom, int a6, int a7, DWORD colorMask, const WORD* ramp, int baseDepth, int blendFlag);
        void drawSnowLightBuffer();
        int drawLineParticles();
        int drawCrossParticles();
        void PlayMovie(const STRING* moviePath);
        void PrintfXY(float x, float y, const char* format, ...);
        int lockBackBuffer();
        void reloadPaletteLightBuffer();
        int DrawLoadBar(VID* drawVid, VID* secondVid, int shiftX, int shiftY);
        DWORD* sampleBackBufferPixel(DWORD* colorOut, float x, float y);

        void drawBackBufferPixel2x2(float x, float y, DWORD color);
        void SaveTGA(const STRING* outputPath, int x, int y, int width, int height);
        void drawVidFrame(VID* vid, int cadr, float x, float y, float z);
        int logGraphResourceError(int value1, const char* message, int value2);
        void SaveParameters(RESOURCE* stream);
        const DD_DRIVER& selectedAdapterRecord() const noexcept;
        DD_DRIVER& selectedAdapterRecord() noexcept;
        int syncDisplayModeDialog(const win::DialogItemRef& deviceRef, const win::DialogItemRef& modeRef, const win::DialogItemRef* fullscreenRef);
        void BeginPause();
        void EndPause();
        void ShadowBar(float x, float y, float x1, float y1, int shadow);
        void LightBar(float x, float y, float x1, float y1, DWORD bright);
        void Bar(float x, float y, float x1, float y1, DWORD color);
        int drawAlphaOverlayQuad(int leftRaw, int topRaw, int rightRaw, int bottomRaw, int colorRaw);
        int drawAdditiveOverlayQuad(int leftRaw, int topRaw, int rightRaw, int bottomRaw, int colorRaw);

        void DrawEffect(int drawEffects);
        int unlockBackBufferIfLocked();
        int backBufferPitchPixels() const { return m_backBufferPitchPixels; }
        void* backBufferPixels() const { return m_lockedBackBufferPixels; }
        void* backBufferSurface() const { return m_backBuffer; }

        std::uint16_t* softwareDepthBuffer() { return m_softwareDepthBuffer; }
        const std::uint16_t* softwareDepthBuffer() const { return m_softwareDepthBuffer; }
        int softwareDepthPitch() const { return m_softwareDepthPitch; }

        float screenWidth() const noexcept { return m_sizeX; }
        float screenHeight() const noexcept { return m_sizeY; }
        int isMoviePlaybackComplete();
        void releaseMoviePlaybackIfComplete();
        int releaseMoviePlayback();
        int enterModalRenderState();
        int leaveModalRenderState();
        void SetEnvironment(DWORD env);
        void* movieComObject(std::size_t index) const { return m_movie.object(index); }
        DWORD renderFlags() const { return m_renderFlags; }
        void SetCamera(float x, float y);


        float cameraX() const;
        float cameraY() const;


        void LoadParameters(RESOURCE* map);
        void OldLoadParameters(RESOURCE* map);

        float SizeX() const;
        float SizeY() const;
        VECTOR2 SizeXY() const { return VECTOR2{m_sizeX, m_sizeY}; }
        float DiffScreenScale() const { return 1.0f; }

        DWORD gammaDiffuse() const { return m_gammaPair.first; }
        DWORD gammaSpecular() const { return m_gammaPair.second; }
        DWORD windDirection() const { return m_windDirection; }
        ANGLE WindDirectionAngle() const noexcept;
        float windSpeed() const { return m_windSpeed; }

        void SetWind(int speed, ANGLE direction);

        void setGamma(const Gamma& rawGamma);
        void setGamma(DWORD diffuse, DWORD specular);
        const Gamma& rawGammaPair() const { return m_gammaPair; }
        DWORD gamma() const { return m_gammaPair.first; }
        int GetEffectState(int effect) const;
        int Effect(int effect, int argument1, int argument2, int duration);

        bool isModalRenderStateActive() const { return (m_graphFlags & 0x00000001u) != 0; }

        bool uses32BitColorDepth() const { return (m_graphFlags & 0x00000002u) != 0; }
        bool fullscreenRequested() const noexcept;

        void DrawLightSource(float x, float y, float z, float sizeX, float sizeY, DWORD color);
        void setScreenSize(int x, int y) { if (x > 0) m_sizeX = static_cast<float>(x); if (y > 0) m_sizeY = static_cast<float>(y); }
        void setFullscreenRequested(bool enabled) noexcept;

        GRAPH* initializeGraphState(const as1::core::StartupSettingsBlock& startupSettings);

        int rebuildTextFont(const STRING& face, int sizeX, int sizeY);
        int drawTextColored(float x, float y, const char* text, DWORD color);
        int drawStringColored(float x, float y, const STRING& text, DWORD color);


        static STRING D3DFormatToString(DWORD format);

    private:
        friend class MAP;
        friend class VID;
        friend class VID_HARDWARE_Z;


        std::uint32_t m_presentParameters[13];
        DWORD m_graphFlags;
        DWORD m_pixelShaderVersion;
        std::array<std::uint16_t, 256> m_intensityPalette16;
        void* m_lockedBackBufferPixels;
        float m_sizeX;
        float m_sizeY;
        int m_backBufferPitchPixels;
        std::uint16_t* m_softwareDepthBuffer;
        int m_softwareDepthPitch;
        float m_viewportLeft;
        float m_viewportRight;
        float m_viewportTop;
        float m_viewportBottom;
        DWORD m_adapterCount;
        DD_DRIVER m_adapterRecords[8];
        int m_selectedAdapterIndex;
        std::uint32_t m_effectSnapshotXBits;
        std::uint32_t m_effectSnapshotYBits;
        std::uint32_t m_effectArgument1[16];
        std::uint32_t m_effectArgument2[16];
        std::uint32_t m_effectStartTimes[16];
        std::uint32_t m_effectDurations[16];
        Gamma m_effectGammaPair;
        Gamma m_gammaPair;
        DWORD m_renderFlags;
        DWORD m_windDirection;
        float m_windSpeed;
        MOVIE m_movie;
        void* m_windowHandle;
        DWORD m_deviceLifecycleState;
        DWORD m_selectedDisplayFormat;
        BASE_TEXTURE* m_lightBuffer;
        BASE_TEXTURE* m_hiBuffer;
        BASE_TEXTURE* m_alphaBuffer;
        void* m_direct3D;
        void* m_device;
        void* m_backBuffer;
        void* m_tempBuffer;
        CD3DFont* m_textFont;
        STRING m_loadingPresentationText;

        void invalidateDeviceObjects() noexcept;
        int restoreDeviceObjects() noexcept;
        void buildAdapterRecord(DD_DRIVER& record, void* direct3D, int adapter,
                                const as1::core::StartupSettingsBlock& startupSettings);
    };

}
