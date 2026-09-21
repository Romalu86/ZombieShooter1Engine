#pragma once


#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <unknwn.h>
#include <d3d9.h>

static constexpr UINT D3D8_SDK_VERSION = 220u;

struct IDirect3D8;
struct IDirect3DDevice8;
struct IDirect3DResource8;
struct IDirect3DBaseTexture8;
struct IDirect3DTexture8;
struct IDirect3DVolumeTexture8;
struct IDirect3DCubeTexture8;
struct IDirect3DVertexBuffer8;
struct IDirect3DIndexBuffer8;
struct IDirect3DSurface8;
struct IDirect3DVolume8;
struct IDirect3DSwapChain8;

using D3DVIEWPORT8 = D3DVIEWPORT9;
using D3DMATERIAL8 = D3DMATERIAL9;
using D3DLIGHT8 = D3DLIGHT9;
using D3DCLIPSTATUS8 = D3DCLIPSTATUS9;


static constexpr D3DSWAPEFFECT D3DSWAPEFFECT_COPY_VSYNC_D3D8 =
    static_cast<D3DSWAPEFFECT>(4);


struct D3DADAPTER_IDENTIFIER8
{
    char Driver[MAX_DEVICE_IDENTIFIER_STRING];
    char Description[MAX_DEVICE_IDENTIFIER_STRING];
    LARGE_INTEGER DriverVersion;
    DWORD VendorId;
    DWORD DeviceId;
    DWORD SubSysId;
    DWORD Revision;
    GUID DeviceIdentifier;
    DWORD WHQLLevel;
};


struct D3DCAPS8
{
    D3DDEVTYPE DeviceType;
    UINT AdapterOrdinal;
    DWORD Caps;
    DWORD Caps2;
    DWORD Caps3;
    DWORD PresentationIntervals;
    DWORD CursorCaps;
    DWORD DevCaps;
    DWORD PrimitiveMiscCaps;
    DWORD RasterCaps;
    DWORD ZCmpCaps;
    DWORD SrcBlendCaps;
    DWORD DestBlendCaps;
    DWORD AlphaCmpCaps;
    DWORD ShadeCaps;
    DWORD TextureCaps;
    DWORD TextureFilterCaps;
    DWORD CubeTextureFilterCaps;
    DWORD VolumeTextureFilterCaps;
    DWORD TextureAddressCaps;
    DWORD VolumeTextureAddressCaps;
    DWORD LineCaps;
    DWORD MaxTextureWidth;
    DWORD MaxTextureHeight;
    DWORD MaxVolumeExtent;
    DWORD MaxTextureRepeat;
    DWORD MaxTextureAspectRatio;
    DWORD MaxAnisotropy;
    float MaxVertexW;
    float GuardBandLeft;
    float GuardBandTop;
    float GuardBandRight;
    float GuardBandBottom;
    float ExtentsAdjust;
    DWORD StencilCaps;
    DWORD FVFCaps;
    DWORD TextureOpCaps;
    DWORD MaxTextureBlendStages;
    DWORD MaxSimultaneousTextures;
    DWORD VertexProcessingCaps;
    DWORD MaxActiveLights;
    DWORD MaxUserClipPlanes;
    DWORD MaxVertexBlendMatrices;
    DWORD MaxVertexBlendMatrixIndex;
    float MaxPointSize;
    DWORD MaxPrimitiveCount;
    DWORD MaxVertexIndex;
    DWORD MaxStreams;
    DWORD MaxStreamStride;
    DWORD VertexShaderVersion;
    DWORD MaxVertexShaderConst;
    DWORD PixelShaderVersion;
    float MaxPixelShaderValue;
};


#define PixelShader1xMaxValue MaxPixelShaderValue


struct D3DSURFACE_DESC8
{
    D3DFORMAT Format;
    D3DRESOURCETYPE Type;
    DWORD Usage;
    D3DPOOL Pool;
    UINT Size;
    D3DMULTISAMPLE_TYPE MultiSampleType;
    UINT Width;
    UINT Height;
};


struct D3DPRESENT_PARAMETERS8
{
    UINT BackBufferWidth;
    UINT BackBufferHeight;
    D3DFORMAT BackBufferFormat;
    UINT BackBufferCount;
    D3DMULTISAMPLE_TYPE MultiSampleType;
    D3DSWAPEFFECT SwapEffect;
    HWND hDeviceWindow;
    BOOL Windowed;
    BOOL EnableAutoDepthStencil;
    D3DFORMAT AutoDepthStencilFormat;
    DWORD Flags;
    UINT FullScreen_RefreshRateInHz;
    UINT FullScreen_PresentationInterval;
};

struct IDirect3D8 : public IUnknown
{
    virtual HRESULT STDMETHODCALLTYPE RegisterSoftwareDevice(void*) = 0;
    virtual UINT STDMETHODCALLTYPE GetAdapterCount() = 0;
    virtual HRESULT STDMETHODCALLTYPE GetAdapterIdentifier(UINT, DWORD, D3DADAPTER_IDENTIFIER8*) = 0;
    virtual UINT STDMETHODCALLTYPE GetAdapterModeCount(UINT) = 0;
    virtual HRESULT STDMETHODCALLTYPE EnumAdapterModes(UINT, UINT, D3DDISPLAYMODE*) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetAdapterDisplayMode(UINT, D3DDISPLAYMODE*) = 0;
    virtual HRESULT STDMETHODCALLTYPE CheckDeviceType(UINT,D3DDEVTYPE,D3DFORMAT,D3DFORMAT,BOOL) = 0;
    virtual HRESULT STDMETHODCALLTYPE CheckDeviceFormat(UINT,D3DDEVTYPE,D3DFORMAT,DWORD,D3DRESOURCETYPE,D3DFORMAT) = 0;
    virtual HRESULT STDMETHODCALLTYPE CheckDeviceMultiSampleType(UINT,D3DDEVTYPE,D3DFORMAT,BOOL,D3DMULTISAMPLE_TYPE) = 0;
    virtual HRESULT STDMETHODCALLTYPE CheckDepthStencilMatch(UINT,D3DDEVTYPE,D3DFORMAT,D3DFORMAT,D3DFORMAT) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetDeviceCaps(UINT,D3DDEVTYPE,D3DCAPS8*) = 0;
    virtual HMONITOR STDMETHODCALLTYPE GetAdapterMonitor(UINT) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateDevice(UINT,D3DDEVTYPE,HWND,DWORD,D3DPRESENT_PARAMETERS8*,IDirect3DDevice8**) = 0;
};

struct IDirect3DResource8 : public IUnknown
{
    virtual HRESULT STDMETHODCALLTYPE GetDevice(IDirect3DDevice8**) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID,const void*,DWORD,DWORD) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID,void*,DWORD*) = 0;
    virtual HRESULT STDMETHODCALLTYPE FreePrivateData(REFGUID) = 0;
    virtual DWORD STDMETHODCALLTYPE SetPriority(DWORD) = 0;
    virtual DWORD STDMETHODCALLTYPE GetPriority() = 0;
    virtual void STDMETHODCALLTYPE PreLoad() = 0;
    virtual D3DRESOURCETYPE STDMETHODCALLTYPE GetType() = 0;
};

struct IDirect3DBaseTexture8 : public IDirect3DResource8
{
    virtual DWORD STDMETHODCALLTYPE SetLOD(DWORD) = 0;
    virtual DWORD STDMETHODCALLTYPE GetLOD() = 0;
    virtual DWORD STDMETHODCALLTYPE GetLevelCount() = 0;
};

struct IDirect3DTexture8 : public IDirect3DBaseTexture8
{
    virtual HRESULT STDMETHODCALLTYPE GetLevelDesc(UINT,D3DSURFACE_DESC8*) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetSurfaceLevel(UINT,IDirect3DSurface8**) = 0;
    virtual HRESULT STDMETHODCALLTYPE LockRect(UINT,D3DLOCKED_RECT*,const RECT*,DWORD) = 0;
    virtual HRESULT STDMETHODCALLTYPE UnlockRect(UINT) = 0;
    virtual HRESULT STDMETHODCALLTYPE AddDirtyRect(const RECT*) = 0;
};

struct IDirect3DVertexBuffer8 : public IDirect3DResource8
{
    virtual HRESULT STDMETHODCALLTYPE Lock(UINT,UINT,void**,DWORD) = 0;
    virtual HRESULT STDMETHODCALLTYPE Unlock() = 0;
    virtual HRESULT STDMETHODCALLTYPE GetDesc(D3DVERTEXBUFFER_DESC*) = 0;
};

struct IDirect3DIndexBuffer8 : public IDirect3DResource8
{
    virtual HRESULT STDMETHODCALLTYPE Lock(UINT,UINT,void**,DWORD) = 0;
    virtual HRESULT STDMETHODCALLTYPE Unlock() = 0;
    virtual HRESULT STDMETHODCALLTYPE GetDesc(D3DINDEXBUFFER_DESC*) = 0;
};

struct IDirect3DSurface8 : public IUnknown
{
    virtual HRESULT STDMETHODCALLTYPE GetDevice(IDirect3DDevice8**) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID,const void*,DWORD,DWORD) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID,void*,DWORD*) = 0;
    virtual HRESULT STDMETHODCALLTYPE FreePrivateData(REFGUID) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetContainer(REFIID,void**) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetDesc(D3DSURFACE_DESC8*) = 0;
    virtual HRESULT STDMETHODCALLTYPE LockRect(D3DLOCKED_RECT*,const RECT*,DWORD) = 0;
    virtual HRESULT STDMETHODCALLTYPE UnlockRect() = 0;
};

struct IDirect3DDevice8 : public IUnknown
{
    virtual HRESULT STDMETHODCALLTYPE TestCooperativeLevel() = 0;
    virtual UINT STDMETHODCALLTYPE GetAvailableTextureMem() = 0;
    virtual HRESULT STDMETHODCALLTYPE ResourceManagerDiscardBytes(DWORD) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetDirect3D(IDirect3D8**) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetDeviceCaps(D3DCAPS8*) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetDisplayMode(D3DDISPLAYMODE*) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetCreationParameters(D3DDEVICE_CREATION_PARAMETERS*) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetCursorProperties(UINT,UINT,IDirect3DSurface8*) = 0;
    virtual void STDMETHODCALLTYPE SetCursorPosition(int,int,DWORD) = 0;
    virtual BOOL STDMETHODCALLTYPE ShowCursor(BOOL) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateAdditionalSwapChain(D3DPRESENT_PARAMETERS8*,IDirect3DSwapChain8**) = 0;
    virtual HRESULT STDMETHODCALLTYPE Reset(D3DPRESENT_PARAMETERS8*) = 0;
    virtual HRESULT STDMETHODCALLTYPE Present(const RECT*,const RECT*,HWND,const RGNDATA*) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetBackBuffer(UINT,D3DBACKBUFFER_TYPE,IDirect3DSurface8**) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetRasterStatus(D3DRASTER_STATUS*) = 0;
    virtual void STDMETHODCALLTYPE SetGammaRamp(DWORD,const D3DGAMMARAMP*) = 0;
    virtual void STDMETHODCALLTYPE GetGammaRamp(D3DGAMMARAMP*) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateTexture(UINT,UINT,UINT,DWORD,D3DFORMAT,D3DPOOL,IDirect3DTexture8**) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateVolumeTexture(UINT,UINT,UINT,UINT,DWORD,D3DFORMAT,D3DPOOL,IDirect3DVolumeTexture8**) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateCubeTexture(UINT,UINT,DWORD,D3DFORMAT,D3DPOOL,IDirect3DCubeTexture8**) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateVertexBuffer(UINT,DWORD,DWORD,D3DPOOL,IDirect3DVertexBuffer8**) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateIndexBuffer(UINT,DWORD,D3DFORMAT,D3DPOOL,IDirect3DIndexBuffer8**) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateRenderTarget(UINT,UINT,D3DFORMAT,D3DMULTISAMPLE_TYPE,BOOL,IDirect3DSurface8**) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateDepthStencilSurface(UINT,UINT,D3DFORMAT,D3DMULTISAMPLE_TYPE,IDirect3DSurface8**) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateImageSurface(UINT,UINT,D3DFORMAT,IDirect3DSurface8**) = 0;
    virtual HRESULT STDMETHODCALLTYPE CopyRects(IDirect3DSurface8*,const RECT*,UINT,IDirect3DSurface8*,const POINT*) = 0;
    virtual HRESULT STDMETHODCALLTYPE UpdateTexture(IDirect3DBaseTexture8*,IDirect3DBaseTexture8*) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetFrontBuffer(IDirect3DSurface8*) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetRenderTarget(IDirect3DSurface8*,IDirect3DSurface8*) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetRenderTarget(IDirect3DSurface8**) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetDepthStencilSurface(IDirect3DSurface8**) = 0;
    virtual HRESULT STDMETHODCALLTYPE BeginScene() = 0;
    virtual HRESULT STDMETHODCALLTYPE EndScene() = 0;
    virtual HRESULT STDMETHODCALLTYPE Clear(DWORD,const D3DRECT*,DWORD,D3DCOLOR,float,DWORD) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetTransform(D3DTRANSFORMSTATETYPE,const D3DMATRIX*) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetTransform(D3DTRANSFORMSTATETYPE,D3DMATRIX*) = 0;
    virtual HRESULT STDMETHODCALLTYPE MultiplyTransform(D3DTRANSFORMSTATETYPE,const D3DMATRIX*) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetViewport(const D3DVIEWPORT8*) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetViewport(D3DVIEWPORT8*) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetMaterial(const D3DMATERIAL8*) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetMaterial(D3DMATERIAL8*) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetLight(DWORD,const D3DLIGHT8*) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetLight(DWORD,D3DLIGHT8*) = 0;
    virtual HRESULT STDMETHODCALLTYPE LightEnable(DWORD,BOOL) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetLightEnable(DWORD,BOOL*) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetClipPlane(DWORD,const float*) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetClipPlane(DWORD,float*) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetRenderState(D3DRENDERSTATETYPE,DWORD) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetRenderState(D3DRENDERSTATETYPE,DWORD*) = 0;
    virtual HRESULT STDMETHODCALLTYPE BeginStateBlock() = 0;
    virtual HRESULT STDMETHODCALLTYPE EndStateBlock(DWORD*) = 0;
    virtual HRESULT STDMETHODCALLTYPE ApplyStateBlock(DWORD) = 0;
    virtual HRESULT STDMETHODCALLTYPE CaptureStateBlock(DWORD) = 0;
    virtual HRESULT STDMETHODCALLTYPE DeleteStateBlock(DWORD) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateStateBlock(D3DSTATEBLOCKTYPE,DWORD*) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetClipStatus(const D3DCLIPSTATUS8*) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetClipStatus(D3DCLIPSTATUS8*) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetTexture(DWORD,IDirect3DBaseTexture8**) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetTexture(DWORD,IDirect3DBaseTexture8*) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetTextureStageState(DWORD,D3DTEXTURESTAGESTATETYPE,DWORD*) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetTextureStageState(DWORD,D3DTEXTURESTAGESTATETYPE,DWORD) = 0;
    virtual HRESULT STDMETHODCALLTYPE ValidateDevice(DWORD*) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetInfo(DWORD,void*,DWORD) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetPaletteEntries(UINT,const PALETTEENTRY*) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetPaletteEntries(UINT,PALETTEENTRY*) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetCurrentTexturePalette(UINT) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetCurrentTexturePalette(UINT*) = 0;
    virtual HRESULT STDMETHODCALLTYPE DrawPrimitive(D3DPRIMITIVETYPE,UINT,UINT) = 0;
    virtual HRESULT STDMETHODCALLTYPE DrawIndexedPrimitive(D3DPRIMITIVETYPE,UINT,UINT,UINT,UINT) = 0;
    virtual HRESULT STDMETHODCALLTYPE DrawPrimitiveUP(D3DPRIMITIVETYPE,UINT,const void*,UINT) = 0;
    virtual HRESULT STDMETHODCALLTYPE DrawIndexedPrimitiveUP(D3DPRIMITIVETYPE,UINT,UINT,UINT,const void*,D3DFORMAT,const void*,UINT) = 0;
    virtual HRESULT STDMETHODCALLTYPE ProcessVertices(UINT,UINT,UINT,IDirect3DVertexBuffer8*,DWORD) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateVertexShader(const DWORD*,const DWORD*,DWORD*,DWORD) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetVertexShader(DWORD) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetVertexShader(DWORD*) = 0;
    virtual HRESULT STDMETHODCALLTYPE DeleteVertexShader(DWORD) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetVertexShaderConstant(DWORD,const void*,DWORD) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetVertexShaderConstant(DWORD,void*,DWORD) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetVertexShaderDeclaration(DWORD,void*,DWORD*) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetVertexShaderFunction(DWORD,void*,DWORD*) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetStreamSource(UINT,IDirect3DVertexBuffer8*,UINT) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetStreamSource(UINT,IDirect3DVertexBuffer8**,UINT*) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetIndices(IDirect3DIndexBuffer8*,UINT) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetIndices(IDirect3DIndexBuffer8**,UINT*) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreatePixelShader(const DWORD*,DWORD*) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetPixelShader(DWORD) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetPixelShader(DWORD*) = 0;
    virtual HRESULT STDMETHODCALLTYPE DeletePixelShader(DWORD) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetPixelShaderConstant(DWORD,const void*,DWORD) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetPixelShaderConstant(DWORD,void*,DWORD) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetPixelShaderFunction(DWORD,void*,DWORD*) = 0;
    virtual HRESULT STDMETHODCALLTYPE DrawRectPatch(UINT,const float*,const D3DRECTPATCH_INFO*) = 0;
    virtual HRESULT STDMETHODCALLTYPE DrawTriPatch(UINT,const float*,const D3DTRIPATCH_INFO*) = 0;
    virtual HRESULT STDMETHODCALLTYPE DeletePatch(UINT) = 0;
};


extern "C" IDirect3D8* WINAPI Direct3DCreate8(UINT sdkVersion);

inline HRESULT D3D8SetSamplerState(IDirect3DDevice8* device, DWORD sampler,
                                   D3DSAMPLERSTATETYPE state, DWORD value)
{

    UINT mapped = 0u;
    switch (static_cast<UINT>(state))
    {
    case 1u: mapped = 13u; break;
    case 2u: mapped = 14u; break;
    case 3u: mapped = 25u; break;
    case 4u: mapped = 15u; break;
    case 5u: mapped = 16u; break;
    case 6u: mapped = 17u; break;
    case 7u: mapped = 18u; break;
    case 8u: mapped = 19u; break;
    case 9u: mapped = 20u; break;
    case 10u: mapped = 21u; break;
    default: return D3DERR_INVALIDCALL;
    }
    return device->SetTextureStageState(
        sampler, static_cast<D3DTEXTURESTAGESTATETYPE>(mapped), value);
}
