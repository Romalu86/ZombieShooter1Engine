#pragma once
#include "core/types.h"
#include "core/as_string.h"
#include "core/base_stream.h"
#include "core/resource.h"
#include "core/application.h"
#include "constant.h"
#include "weapon.h"
#include "vid/vid.h"
#include "sprite.h"
#include "script.h"
#include "groups.h"
#include "player.h"
#include "base_sprite_list.h"
#include <map>
#include <memory>
#include <unordered_map>
#include <set>
#include <vector>

namespace as1
{
    using GamePath = STRING;

    class GRAPH;
    class MAP;
    extern MAP* Map;

    extern std::uint32_t prev_second_time;
    namespace win { class ApplicationWin; }

    long double approximatePlanarDistance(float dx, float dy) noexcept;


    struct SFX
    {
        std::vector<BYTE> raw;
        DWORD property = 0;
        BYTE priority = 0;
        int volumeBias = 0;
        std::vector<STRING> ffbNames;
        std::vector<STRING> wavNames;
        std::vector<STRING> missingFiles;

    };
    struct MapSpriteRestoreRecord
    {
        int oldAddress = 0;
        SpriteRestoreState state;
        SPRITE* sprite = nullptr;
        bool attachedToSprite = false;
        std::vector<SPRITE*> resolvedObjectRefs;
        size_t resolvedCommandArgRefs = 0;
    };
    class RelationTable
    {
    public:
        RelationTable() noexcept;
        ~RelationTable() noexcept;
        RelationTable(const RelationTable&) = delete;
        RelationTable& operator=(const RelationTable&) = delete;

        void append(int oldSpriteAddr, SPRITE* newSpritePtr);
        SPRITE* getPointer(int oldSpriteAddr) const noexcept;
        void clear() noexcept;
        size_t size() const noexcept { return static_cast<size_t>(m_old.count); }

    private:
        struct RawList
        {
            DWORD vtable;
            int count;
            int capacity;
            DWORD items;
        };

        RawList m_old;
        RawList m_new;

        template <class T>
        static T* decodePointer(DWORD pointer) noexcept
        {
            return reinterpret_cast<T*>(static_cast<std::uintptr_t>(pointer));
        }
    };

    class MAP
    {
    public:
        explicit MAP(GRAPH* graph = nullptr);
        MAP(GRAPH* graph, STRING resourceRoot);
        ~MAP();

        void setResourceRoot(const STRING& root) { data().resourceRoot = root; }
        const STRING& resourceRoot() const { return data().resourceRoot; }
        void setObjectsResource(const STRING& name) { data().objectsResource = name; }
        const STRING& objectsResource() const { return data().objectsResource; }

        bool loadVids(RESOURCE* res);
        bool ReloadVid();
        void LoadWeapon(RESOURCE* res);
        const CONSTANT& Constants() const { return data().constants; }
        void invalidateFontVidDeviceObjects() noexcept;
        void restoreFontVidDeviceObjects() noexcept;
        size_t noVid() const;
        size_t noWeapon() const { const int count = weaponCount(); return count > 0 ? static_cast<size_t>(count) : 0u; }
        const WEAPON* weapons() const { return weaponTable(); }
        int weaponCount() const noexcept
        {
            return *reinterpret_cast<const int*>(reinterpret_cast<const BYTE*>(this) + core::application_layout::WeaponCount);
        }
        WEAPON* weaponTable() noexcept
        {
            return *reinterpret_cast<WEAPON**>(reinterpret_cast<BYTE*>(this) + core::application_layout::WeaponTable);
        }
        const WEAPON* weaponTable() const noexcept
        {
            return *reinterpret_cast<WEAPON* const*>(reinterpret_cast<const BYTE*>(this) + core::application_layout::WeaponTable);
        }
        const std::vector<SFX>& sfx() const { return data().sfx; }
        size_t noSfx() const { return data().sfx.size(); }
        size_t noSprite() const { return data().sprites.size(); }
        size_t noGroup() const { return groupOwner().size(); }
        int version() const { return data().version; }
        DWORD currentTime() const { return data().currentTime; }
        const VECTOR2& sizeXY() const { return data().sizeXY; }
        const VECTOR2& shiftXY() const { return data().shiftXY; }
        const VECTOR2& originalShiftXY() const { return data().originalShiftXY; }

        float SizeX() const
        {
            return *reinterpret_cast<const float*>(reinterpret_cast<const BYTE*>(this) + core::application_layout::MapExtentX);
        }
        float SizeY() const
        {
            return *reinterpret_cast<const float*>(reinterpret_cast<const BYTE*>(this) + core::application_layout::MapExtentY);
        }
        float FromScreenX(float x) const;
        float FromScreenY(float y) const;
        float ToScreenX(float x) const;
        float ToScreenY(float y) const;
        float ToScreenY(float y, float z) const;
        void SetScrollBox(float minX, float minY, float maxX, float maxY);

        void SetShiftCoor(float centerX, float centerY, int effect = 0);
        void SetShiftCoor(const VECTOR2& center, int effect = 0) { SetShiftCoor(center.x, center.y, effect); }
        SPRITE* FirstSprite(int layer, int* index);

        SPRITE* NextSprite(int layer, int* index);
        int noGridX() const { return terrainGridWidth(); }
        int noGridY() const { return terrainGridHeight(); }
        const short* gridZ() const { return terrainGrid(); }
        const std::vector<std::unique_ptr<SPRITE>>& sprites() const { return data().sprites; }
        VID* CreateVid(RESOURCE* res, int nvid);
        VID* Vid(int nvid) const;

        int ValidateVid(int nvid) const;
        GamePath resolveGameFile(const STRING& gamePath) const;

        void release();
        void DeleteExtraVid();
        void DeletePointerToSprite(SPRITE* sprite);
        __forceinline const SCRIPT& script() const noexcept
        {
            const BYTE* const owner = reinterpret_cast<const BYTE*>(this);
            return *reinterpret_cast<const SCRIPT*>(owner + core::application_layout::ScriptRuntime);
        }
        __forceinline SCRIPT& scriptRuntime() noexcept
        {
            BYTE* const owner = reinterpret_cast<BYTE*>(this);
            return *reinterpret_cast<SCRIPT*>(owner + core::application_layout::ScriptRuntime);
        }
        STRING ScriptVariable(STRING name);
        int ExecFunc(int opcode);
        STRING* PopStr();
        int PopInt();
        VID* PopVid(const char* errorContext);
        void PushInt(int value);
        void PushStr(const STRING& value);
        void PushObject(int value, const STRING* context);
        __forceinline RESOURCE& demoResource() noexcept
        {
            BYTE* const owner = reinterpret_cast<BYTE*>(this);
            return *reinterpret_cast<RESOURCE*>(owner + core::application_layout::DemoResource);
        }
        __forceinline const RESOURCE& demoResource() const noexcept
        {
            const BYTE* const owner = reinterpret_cast<const BYTE*>(this);
            return *reinterpret_cast<const RESOURCE*>(owner + core::application_layout::DemoResource);
        }
        SPRITE* OldLoadSprite(BaseStream* res);
        SPRITE* LoadSprite(BaseStream* res, int version);
        void CreateEmptyHardwareGround();
        SPRITE* CreateSprite(VID* vid, const VECTOR& v, const ANGLE& direction, SPRITE* parent = nullptr, bool remoteControlled = false, bool createChildRoute = true);
        PLAYER* Player(int playerIndex) const noexcept;
        void SetFlagman(int playerIndex, SPRITE* sprite) noexcept;
        SPRITE* flagmanSpriteForPlayer(int playerIndex) const noexcept;

        SPRITE* ReadPointer(BaseStream* stream);


        __forceinline
        SPRITE* ReadSpriteHandle(BaseStream* stream, int* oldAddress = nullptr) const
        {
            int handle = -1;
            if (stream)
                stream->read(&handle, 4u);
            if (oldAddress)
                *oldAddress = handle;
            return (handle == -1) ? nullptr : ResolveOldSpriteHandle(handle);
        }
        __forceinline
        SPRITE* ResolveOldSpriteHandle(int oldAddress) const
        {
            if (oldAddress == 0 || oldAddress == -1)
                return nullptr;
            return relationTable().getPointer(oldAddress);
        }
        __forceinline GROUPS& groupOwner() noexcept
        {
            BYTE* const owner = reinterpret_cast<BYTE*>(this);
            return *reinterpret_cast<GROUPS*>(owner + core::application_layout::Groups);
        }
        __forceinline const GROUPS& groupOwner() const noexcept
        {
            const BYTE* const owner = reinterpret_cast<const BYTE*>(this);
            return *reinterpret_cast<const GROUPS*>(owner + core::application_layout::Groups);
        }
        __forceinline
        void BindLoadedSpriteHandle(int oldAddress, SPRITE* sprite)
        {
            if (oldAddress == 0 || oldAddress == -1)
                return;
            relationTable().append(oldAddress, sprite);
        }

        void ExchangeVid(VID* first, VID* second);

        float GetGroundZScr(float screenX, float screenY) const noexcept;
        int isLineUnderGroundWithBullet(float startX, float startY, float startZ,
                                            float endX, float endY, float endZ) const noexcept;
        float GetGroundZ(const VECTOR2& v) const;
        float GetGroundZ(const VID* vid, VECTOR2 v) const;
        void ResetGroundZ();
        void SetGroundZ(float x, float y, float z);
        void SetTempGroundZ(float x, float y, float z);
        void ClearTempGroundZ(float x, float y, float z);

    private:
        friend class win::ApplicationWin;

        __forceinline
        short* terrainGrid() noexcept
        {
            const BYTE* const owner = reinterpret_cast<const BYTE*>(this);
            return *reinterpret_cast<short* const*>(owner + core::application_layout::TerrainGrid);
        }
        __forceinline
        const short* terrainGrid() const noexcept
        {
            const BYTE* const owner = reinterpret_cast<const BYTE*>(this);
            return *reinterpret_cast<short* const*>(owner + core::application_layout::TerrainGrid);
        }
        __forceinline
        int terrainGridWidth() const noexcept
        {
            const BYTE* const owner = reinterpret_cast<const BYTE*>(this);
            return *reinterpret_cast<const int*>(owner + core::application_layout::TerrainGridWidth);
        }
        __forceinline
        int terrainGridHeight() const noexcept
        {
            const BYTE* const owner = reinterpret_cast<const BYTE*>(this);
            return *reinterpret_cast<const int*>(owner + core::application_layout::TerrainGridHeight);
        }
        __forceinline
        void setTerrainGridDimensions(int x, int y) noexcept
        {
            BYTE* const owner = reinterpret_cast<BYTE*>(this);
            *reinterpret_cast<int*>(owner + core::application_layout::TerrainGridWidth) = x;
            *reinterpret_cast<int*>(owner + core::application_layout::TerrainGridHeight) = y;
        }
        void releaseTerrainGridStorage() noexcept;
        void setWeaponTableState(int count, WEAPON* table) noexcept;
        __forceinline
        RelationTable& relationTable() noexcept
        {
            BYTE* const owner = reinterpret_cast<BYTE*>(this);
            return *reinterpret_cast<RelationTable*>(owner + core::application_layout::RelationTable);
        }
        __forceinline
        const RelationTable& relationTable() const noexcept
        {
            const BYTE* const owner = reinterpret_cast<const BYTE*>(this);
            return *reinterpret_cast<const RelationTable*>(owner + core::application_layout::RelationTable);
        }

        struct Data
        {
            GRAPH* graph = nullptr;
            STRING fileName;
            STRING resourceRoot{"."};
            STRING objectsResource{"objects.res"};
            std::vector<std::unique_ptr<SPRITE>> sprites;
            std::vector<SFX> sfx;
            CONSTANT constants;
            int version = 0;
            bool useLegacyCompactSpriteRecords = false;
            DWORD currentTime = 0;
            VECTOR2 sizeXY{640.0f, 480.0f};
            VECTOR2 scrollMinXY;
            VECTOR2 scrollMaxXY;
            VECTOR2 shiftXY{0.0f, 1.0f};
            VECTOR2 originalShiftXY;
            VECTOR2 shiftDeltaXY;
        };

        static Data* s_data;
        static __forceinline Data& data() noexcept { return *s_data; }
    };
}
