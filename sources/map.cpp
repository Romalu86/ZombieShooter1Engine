#include "map.h"
#include "vid/vid_software.h"
#include "vid/vid_software16.h"
#include "vid/vid_hardware.h"
#include "vid/vid_hardware_z.h"
#include "vid/vid_surface.h"
#include "vid/vid_light.h"
#include "vid/vid_font.h"
#include "core/application.h"
#include "core/resource.h"
#include "core/log.h"
#include "core/file_logger.h"
#include "core/configuration.h"
#include "constant.h"
#include "images/picture.h"
#include "graph.h"
#include "unit.h"
#include "avia.h"
#include "creature.h"
#include "civ_robot.h"
#include "engine.h"
#include "balloon.h"
#include "ball.h"
#include "depo.h"
#include "cannon.h"
#include "building.h"
#include "rail.h"
#include "sprite_act_const.h"
#include "script/action_constants.h"
#include "sprite_collector.h"
#include "mouse.h"
#include "menu.h"
#include "sound/sound_engine.h"
#include <stdexcept>
#include <algorithm>
#include <cstring>
#include <cctype>
#include <cmath>
#include <functional>
#include <unordered_map>
#include <limits>
#include <new>
#include <cstdlib>
#include "win/application_win.h"
#include <mmsystem.h>
#include "d3d8.h"
#include <xmmintrin.h>

namespace as1
{


    MAP* Map = nullptr;
    MAP::Data* MAP::s_data = nullptr;
    std::uint32_t prev_second_time = 0;


    PRIMITIVE::PRIMITIVE(MAP* owner, VID* vid, const VECTOR& xyz, const ANGLE& dir, SPRITE* parent)
        : SPRITE(owner, vid, xyz, dir, parent)
    {


    }

    void PRIMITIVE::Tact()
    {
        (void)advancePrimitiveFrame();
    }


    void PRIMITIVE::Draw() const
    {
        Vid()->Draw(this);
    }


    long double approximatePlanarDistance(float dx, float dy) noexcept
    {


        const float ax = std::fabs(dx);
        const float ay = std::fabs(dy);
        const float metric =
            (ax <= ay || std::isnan(ax) || std::isnan(ay))
                ? ax * 0.5f + ay
                : ax + ay * 0.5f;
        return static_cast<long double>(metric);
    }

    namespace
    {
        constexpr float UNLIMITED = 999999.0f;

        int mapStackValueToInteger(const script::StackObject& value)
        {
            return (value.flags & script::STACK_OBJECT_STRING)
                ? script::ParseStackIntegerText(value.text.c_str())
                : value.intValue;
        }



        STRING gamePathString(const GamePath& path)
        {
            return path;
        }

        bool isAbsoluteWindowsPath(const std::string& path) noexcept
        {
            return (path.size() >= 2u && std::isalpha(static_cast<unsigned char>(path[0])) && path[1] == ':') ||
                   (!path.empty() && (path[0] == '\\' || path[0] == '/'));
        }

        std::string joinGamePath(const std::string& root, const std::string& child)
        {
            if (root.empty() || root == "." || isAbsoluteWindowsPath(child))
                return child;
            std::string out = root;
            if (!out.empty() && out.back() != '\\' && out.back() != '/')
                out.push_back('\\');
            out += child;
            return out;
        }







        __forceinline int mapConvertFloatToInt32(float value) noexcept
        {
            const long double d = static_cast<long double>(value);
            if (!std::isfinite(d) ||
                d < static_cast<long double>(std::numeric_limits<std::int64_t>::min()) ||
                d > static_cast<long double>(std::numeric_limits<std::int64_t>::max()))
                return 0;
            const std::int64_t converted = static_cast<std::int64_t>(std::trunc(d));
            return static_cast<int>(static_cast<std::uint32_t>(converted));
        }

        __forceinline int truncateFloatToInt32ForMap(float value) noexcept
        {
            return _mm_cvtt_ss2si(_mm_set_ss(value));
        }

        __forceinline float minssForMap(float destination, float source) noexcept
        {
            return _mm_cvtss_f32(_mm_min_ss(_mm_set_ss(destination), _mm_set_ss(source)));
        }

        __forceinline float maxssForMap(float destination, float source) noexcept
        {
            return _mm_cvtss_f32(_mm_max_ss(_mm_set_ss(destination), _mm_set_ss(source)));
        }

        __forceinline int mapMultiplyAndConvertToInt32(float value, float multiplier) noexcept
        {
            const long double d = static_cast<long double>(value) * static_cast<long double>(multiplier);
            if (!std::isfinite(d) ||
                d < static_cast<long double>(std::numeric_limits<std::int64_t>::min()) ||
                d > static_cast<long double>(std::numeric_limits<std::int64_t>::max()))
                return 0;
            const std::int64_t converted = static_cast<std::int64_t>(std::trunc(d));
            return static_cast<int>(static_cast<std::uint32_t>(converted));
        }

        __forceinline bool x87LessOrUnorderedForMap(float lhs, float rhs) noexcept
        {
            return lhs < rhs || std::isnan(lhs) || std::isnan(rhs);
        }

        __forceinline int terrainGridDimension(float extent) noexcept
        {
            const int raw = static_cast<int>(extent + 7.0f);
            return (raw + (raw < 0 ? 7 : 0)) >> 3;
        }

        __forceinline int mapGridCoordinate(float value, float limit) noexcept
        {
            if (value < 0.0f)
                return 0;
            const float chosen = (value < limit || std::isnan(value)) ? value : (limit - 1.0f);
            const long double scaled = static_cast<long double>(chosen) * 0.125L;
            if (!std::isfinite(scaled) ||
                scaled < static_cast<long double>(std::numeric_limits<std::int64_t>::min()) ||
                scaled > static_cast<long double>(std::numeric_limits<std::int64_t>::max()))
                return 0;
            const std::int64_t converted = static_cast<std::int64_t>(std::trunc(scaled));
            return static_cast<int>(static_cast<std::uint32_t>(converted));
        }

        std::uint32_t currentMilliseconds()
        {
            return static_cast<std::uint32_t>(::timeGetTime());
        }

        void deleteVidThroughDeletingDestructor(VID* vid) noexcept
        {
            if (!vid)
                return;
            delete vid;
        }

        std::string normalizeGamePath(std::string s)
        {
            std::replace(s.begin(), s.end(), '\\', '/');
            while (!s.empty() && (s.front() == '/' || s.front() == '.'))
            {
                if (s.front() == '.')
                {
                    s.erase(s.begin());
                    if (!s.empty() && s.front() == '/')
                        s.erase(s.begin());
                }
                else
                    s.erase(s.begin());
            }
            return s;
        }

        std::string lowerCopy(std::string s)
        {
            std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return s;
        }

        bool isApplicationRenderPass(int pass) noexcept
        {
            return pass >= 0 && pass <= 10;
        }

        int applicationRenderPassSequenceIndex(int pass) noexcept
        {
            return isApplicationRenderPass(pass) ? pass : -1;
        }

        bool hasExtension(const std::string& path, const char* ext)
        {
            const std::string l = lowerCopy(path);
            const std::string e = ext;
            return l.size() >= e.size() && l.compare(l.size() - e.size(), e.size(), e) == 0;
        }

    }


    RelationTable::RelationTable() noexcept
    {
        m_old.vtable = BaseSpriteList<0>::CurrentImageRelationListVtable();
        m_old.count = 0;
        m_old.capacity = 0;
        m_old.items = 0;

        m_new.vtable = BaseSpriteList<0>::CurrentImageRelationListVtable();
        m_new.count = 0;
        m_new.capacity = 0;
        m_new.items = 0;
    }

    RelationTable::~RelationTable() noexcept
    {
        m_new.vtable = BaseSpriteList<0>::CurrentImageRelationListVtable();
        if (void* const items = decodePointer<void>(m_new.items))
            ::operator delete(items);
        m_new.items = 0;
        m_new.count = 0;

        m_old.vtable = BaseSpriteList<0>::CurrentImageRelationListVtable();
        if (void* const items = decodePointer<void>(m_old.items))
            ::operator delete(items);
        m_old.items = 0;
        m_old.count = 0;
    }

    void RelationTable::append(int oldSpriteAddr, SPRITE* newSpritePtr)
    {
        if (!oldSpriteAddr)
            return;

        if (m_old.count >= m_old.capacity)
        {
            const int oldCapacity = m_old.capacity;
            const int newCapacity = 2 * oldCapacity + 4;
            if (newCapacity > oldCapacity)
            {
                int* const oldItems = decodePointer<int>(m_old.items);
                int* const newItems = static_cast<int*>(::operator new(static_cast<size_t>(newCapacity) * sizeof(int), std::nothrow));
                if (!newItems)
                    fatalLogError(g_fileLogger, "!!!ERROR!!!::LIST: Not enough memory %i", newCapacity);
                if (oldItems)
                {
                    for (int i = 0; i < oldCapacity; ++i)
                        newItems[i] = oldItems[i];
                    ::operator delete(oldItems);
                }
                m_old.items = static_cast<DWORD>(reinterpret_cast<std::uintptr_t>(newItems));
                m_old.capacity = newCapacity;
            }
        }
        decodePointer<int>(m_old.items)[m_old.count++] = oldSpriteAddr;

        if (m_new.count >= m_new.capacity)
        {
            const int oldCapacity = m_new.capacity;
            const int newCapacity = 2 * oldCapacity + 4;
            if (newCapacity > oldCapacity)
            {
                DWORD* const oldItems = decodePointer<DWORD>(m_new.items);
                DWORD* const newItems = static_cast<DWORD*>(::operator new(static_cast<size_t>(newCapacity) * sizeof(DWORD), std::nothrow));
                if (!newItems)
                    fatalLogError(g_fileLogger, "!!!ERROR!!!::LIST: Not enough memory %i", newCapacity);
                if (oldItems)
                {
                    for (int i = 0; i < oldCapacity; ++i)
                        newItems[i] = oldItems[i];
                    ::operator delete(oldItems);
                }
                m_new.items = static_cast<DWORD>(reinterpret_cast<std::uintptr_t>(newItems));
                m_new.capacity = newCapacity;
            }
        }
        decodePointer<DWORD>(m_new.items)[m_new.count++] =
            static_cast<DWORD>(reinterpret_cast<std::uintptr_t>(newSpritePtr));
    }


    SPRITE* RelationTable::getPointer(int oldSpriteAddr) const noexcept
    {
        int index = m_old.count;
        if (!index)
            return nullptr;
        const int* const oldItems = decodePointer<int>(m_old.items);
        while (index)
        {
            --index;
            if (oldItems[index] == oldSpriteAddr)
                return decodePointer<SPRITE>(decodePointer<DWORD>(m_new.items)[index]);
        }
        return nullptr;
    }


    void RelationTable::clear() noexcept
    {
        void* const oldItems = decodePointer<void>(m_old.items);
        m_old.capacity = 0;
        m_old.count = 0;
        if (oldItems)
            ::operator delete(oldItems);
        m_old.items = 0;

        void* const newItems = decodePointer<void>(m_new.items);
        m_new.capacity = 0;
        m_new.count = 0;
        if (newItems)
            ::operator delete(newItems);
        m_new.items = 0;
    }

    MAP::MAP(GRAPH* graph)
    {
        if (!s_data)
            s_data = new (std::nothrow) Data;
        if (s_data)
            s_data->graph = graph;
    }

    MAP::MAP(GRAPH* graph, STRING resourceRoot)
    {
        if (!s_data)
            s_data = new (std::nothrow) Data;
        if (s_data)
        {
            s_data->graph = graph;
            s_data->resourceRoot = std::move(resourceRoot);
        }
    }

    MAP::~MAP()
    {
    }




    __forceinline void MAP::releaseTerrainGridStorage() noexcept
    {
        BYTE* const owner = reinterpret_cast<BYTE*>(this);
        short*& grid = *reinterpret_cast<short**>(owner + core::application_layout::TerrainGrid);
        short*& tempGrid = *reinterpret_cast<short**>(owner + core::application_layout::TempTerrainGrid);
        if (grid)
            ::operator delete(static_cast<void*>(grid));
        if (tempGrid)
            ::operator delete(static_cast<void*>(tempGrid));
        grid = nullptr;
        tempGrid = nullptr;
    }

    __forceinline void MAP::setWeaponTableState(int count, WEAPON* table) noexcept
    {
        BYTE* const owner = reinterpret_cast<BYTE*>(this);
        *reinterpret_cast<int*>(owner + core::application_layout::WeaponCount) = count;
        *reinterpret_cast<WEAPON**>(owner + core::application_layout::WeaponTable) = table;
    }






    STRING MAP::ScriptVariable(STRING name)
    {
        return scriptRuntime().GetVariableStr(name);
    }

    size_t MAP::noVid() const
    {
        const int count = core::GlobalApplicationVidTable().count();
        return count > 0 ? static_cast<size_t>(count) : 0u;
    }








    bool MAP::loadVids(RESOURCE* res)
    {
        const std::uint32_t loadStart = currentMilliseconds();

        if (!weaponTable())
        {
            LoadWeapon(res);
            const int count = weaponCount();
            if (count)
                LOG::Write("LoadWeapon::No=%-5i             sizeof(WEAPON)=%-4i",
                           count, static_cast<int>(WEAPON::RUNTIME_RECORD_SIZE));
        }

        if (res->GoBegin(RESOURCE::ResTypes::OBJECT))
        {
            LOG::ResourceError("%s", 11, "load 'VID'", 0, "");
            return false;
        }

        core::ApplicationVidTable& appVidTable = core::GlobalApplicationVidTable();
        const bool overlayExistingDepot = appVidTable.count() != 0;

        do
        {
            int nvid = 0;
            (void)res->read(&nvid, sizeof(nvid));

            if (nvid >= static_cast<int>(core::ApplicationVidTable::kCapacity))
                LOG::ResourceError("%s", 4, "nvid > MAX_VID", nvid, "");

            if (VID* const previous = appVidTable.slot(nvid))
            {
                deleteVidThroughDeletingDestructor(previous);
                appVidTable.setSlotCell(nvid, nullptr);
                LOG::ResourceError("%s", 5, "this VID already loaded", nvid, "");
            }


            VID* const created = CreateVid(res, nvid);
            if (created)
            {
                appVidTable.setSlotCell(nvid, created);
                if (nvid >= appVidTable.count())
                    appVidTable.setStoredCount(nvid + 1);
                if (overlayExistingDepot)
                    created->type = static_cast<WORD>(created->type | VID_TYPE_EXTRA);


                WEAPON* const weaponBase = weaponTable();
                const int noWeapon = weaponCount();
                if (created->nWeapon < noWeapon)
                    created->weapon = weaponBase + created->nWeapon;
                else
                {
                    created->logVidResourceError(10, "nWeapon > noWeapon", created->nWeapon);
                    created->weapon = weaponBase;
                }
                Graph->DrawLoadBar(appVidTable.slot(0), nullptr, 0, 0);
            }
        }
        while (!res->GoNextSub(RESOURCE::ResTypes::OBJECT));

        int maxSizeX = 0;
        int maxSizeY = 0;
        const int rawCount = appVidTable.count();
        for (int index = 0; index < rawCount; ++index)
        {
            VID* const vid = appVidTable.slot(index);
            if (!vid)
                continue;
            vid->SetChildAndLink();
            maxSizeX = std::max(maxSizeX, static_cast<int>(vid->vidWidth()));
            maxSizeY = std::max(maxSizeY, static_cast<int>(vid->vidHeight()));
        }

        const std::uint32_t elapsed = currentMilliseconds() - loadStart;
        LOG::Write("LoadVid::No   =%-15i   sizeof(VID)   =%-5i    load time     =%ims   MaxSizeX,Y=%i,%i",
                   rawCount, static_cast<int>(sizeof(VID)), static_cast<int>(elapsed), maxSizeX, maxSizeY);
        return true;
    }


    bool MAP::ReloadVid()
    {
        RESOURCE resource;
        LOG::Write("Reload vids");
        RESOURCE* const res = &resource;
        core::ApplicationVidTable& appVidTable = core::GlobalApplicationVidTable();
        int rawCount = appVidTable.count();
        for (int index = 0; index < rawCount; ++index)
        {
            VID* const tableVid = appVidTable.slot(index);
            if (!tableVid)
                continue;
            VID* const exchanged = tableVid->exchangedVidRef();
            if (exchanged != tableVid)
                (void)ExchangeVid(exchanged, tableVid);
            rawCount = appVidTable.count();
        }

        const STRING resourcePath(gamePathString(resolveGameFile(data().objectsResource)));
        if (resource.openFile(&resourcePath, RESOURCE::ResTypes::DATA) != 0)
        {
            LOG::ResourceError("%s", 7, "resource file", 0, "");
            return false;
        }

        LoadWeapon(res);

        if (res->GoBegin(RESOURCE::ResTypes::OBJECT))
        {
            LOG::ResourceError("%s", 11, "load 'VID'", 0, "");
            return false;
        }

        do
        {
            int objectNvid = 0;
            (void)res->read(&objectNvid, sizeof(objectNvid));

            if (objectNvid >= static_cast<int>(core::ApplicationVidTable::kCapacity))
                LOG::ResourceError("%s", 4, "nvid > MAX_VID", objectNvid, "");

            VID* const slotVid = appVidTable.slot(objectNvid);
            if (!slotVid)
                continue;

            VID* const exchanged = slotVid->exchangedVidRef();
            const int targetNvid = exchanged->nvid();
            VID* const target = appVidTable.slot(targetNvid);

            target->name.Read(res);
            target->LoadParameters(res);

            WEAPON* const weaponRecords = weaponTable();
            const int nWeapon = target->nWeapon;
            if (nWeapon < weaponCount())
                target->weapon = weaponRecords + nWeapon;
            else
                target->weapon = weaponRecords;
        }
        while (!res->GoNextSub(RESOURCE::ResTypes::OBJECT));

        rawCount = appVidTable.count();
        for (int index = 0; index < rawCount; ++index)
        {
            VID* const vid = appVidTable.slot(index);
            if (vid && (vid->formatFlags() & VID_TYPE_EXTRA) == 0u)
                vid->SetChildAndLink();
        }
        return true;
    }



    void MAP::LoadWeapon(RESOURCE* res)
    {
        if (WEAPON* const previous = weaponTable())
            ::operator delete(previous);
        setWeaponTableState(0, nullptr);


        void* serialized = nullptr;
        const int count = res->Load(RESOURCE::ResTypes::WEAPON, &serialized,
                                    static_cast<int>(WEAPON::SERIALIZED_RECORD_SIZE));
        if (count == 0 || !serialized)
        {
            setWeaponTableState(0, nullptr);
            EmptyVid->setWeaponRecord(nullptr);
            return;
        }

        const std::size_t runtimeBytes =
            static_cast<std::size_t>(count) * WEAPON::RUNTIME_RECORD_SIZE;
        BYTE* const runtime = static_cast<BYTE*>(::operator new(runtimeBytes, std::nothrow));

        const BYTE* const source = static_cast<const BYTE*>(serialized);
        constexpr std::uint32_t kUnlimitedBits = 0x497423F0u;
        for (int record = 0; record < count; ++record)
        {
            BYTE* const dst = runtime +
                static_cast<std::size_t>(record) * WEAPON::RUNTIME_RECORD_SIZE;
            const BYTE* const src = source +
                static_cast<std::size_t>(record) * WEAPON::SERIALIZED_RECORD_SIZE;


            std::memcpy(dst, src, 0x21Cu);


            for (int i = 0; i < 8; ++i)
            {
                std::uint32_t selector = 0;
                std::memcpy(&selector, src + 0x1C0u + static_cast<std::size_t>(i) * 4u, 4u);
                dst[0x1C0u + static_cast<std::size_t>(i)] = static_cast<BYTE>(selector);
                std::memcpy(&selector, src + 0x1E0u + static_cast<std::size_t>(i) * 4u, 4u);
                dst[0x1C8u + static_cast<std::size_t>(i)] = static_cast<BYTE>(selector);
                std::memcpy(&selector, src + 0x200u + static_cast<std::size_t>(i) * 4u, 4u);
                dst[0x1D0u + static_cast<std::size_t>(i)] = static_cast<BYTE>(selector);

                std::memcpy(dst + 0x1D8u + static_cast<std::size_t>(i) * 4u,
                            src + 0x220u + static_cast<std::size_t>(i) * 4u, 4u);

                std::uint32_t raw = 0;
                float value = 0.0f;
                std::memcpy(&raw, src + 0x240u + static_cast<std::size_t>(i) * 4u, 4u);
                std::memcpy(&value, &raw, 4u);
                if (raw != kUnlimitedBits)
                    value *= 0.001f;
                std::memcpy(dst + 0x1F8u + static_cast<std::size_t>(i) * 4u, &value, 4u);

                std::memcpy(&raw, src + 0x260u + static_cast<std::size_t>(i) * 4u, 4u);
                std::memcpy(&value, &raw, 4u);
                if (raw != kUnlimitedBits)
                    value *= 0.001f;
                std::memcpy(dst + 0x218u + static_cast<std::size_t>(i) * 4u, &value, 4u);
            }
            const std::int32_t maxColumn = 7;
            std::memcpy(dst + 0x238u, &maxColumn, sizeof(maxColumn));
        }

        ::operator delete(serialized);
        setWeaponTableState(count, reinterpret_cast<WEAPON*>(runtime));
        EmptyVid->setWeaponRecord(weaponTable());
    }





    void MAP::invalidateFontVidDeviceObjects() noexcept
    {
        core::ApplicationVidTable& table = core::GlobalApplicationVidTable();
        const int count = table.count();
        for (int index = 0; index < count; ++index)
        {
            VID* const vid = table.slot(index);
            if (vid && (vid->formatFlags() & VID_TYPE_FONT) != 0u)
                (void)static_cast<VID_FONT*>(vid)->InvalidateDeviceObjects();
        }
    }


    void MAP::restoreFontVidDeviceObjects() noexcept
    {
        core::ApplicationVidTable& table = core::GlobalApplicationVidTable();
        const int count = table.count();
        for (int index = 0; index < count; ++index)
        {
            VID* const vid = table.slot(index);
            if (vid && (vid->formatFlags() & VID_TYPE_FONT) != 0u)
                (void)static_cast<VID_FONT*>(vid)->RestoreDeviceObjects();
        }
    }


    VID* MAP::CreateVid(RESOURCE* res, int nvid)
    {
        STRING objectName;
        objectName.Read(res);

        VID loaded_vid;
        loaded_vid.nVid = nvid;
        loaded_vid.name = objectName;
        loaded_vid.noCadr = 32000;
        const size_t parameterBegin = res->position();
        loaded_vid.LoadParameters(res);

        STRING vidFileName;
        vidFileName.Read(res);
        const std::string normalizedVidFileName = lowerCopy(vidFileName.str());
        vidFileName = STRING(normalizedVidFileName);
        const std::string sourceVidName = normalizeGamePath(normalizedVidFileName);
        std::string loadVidName = sourceVidName;

        res->seek(parameterBegin);

        {
            core::ApplicationVidTable& table = core::GlobalApplicationVidTable();
            const int rawCount = table.count();
            const std::string normalizedVidName = normalizedVidFileName;

            for (int index = 0; index < rawCount; ++index)
            {
                VID* const existing = table.slot(index);
                if (!existing)
                    continue;

                if (existing->sourceVidPath().str() != normalizedVidName)
                    continue;

                const DWORD existingClass = existing->spriteClassId();
                if (loaded_vid.spriteClassId() == B_BUILDEDTERRAIN)
                {
                    if (existingClass != B_BUILDEDTERRAIN)
                        continue;
                }
                else if (existingClass == B_BUILDEDTERRAIN)
                {
                    continue;
                }

                if ((existing->formatFlags() & VID_TYPE_HARDWARE) == 0u)
                {
                    if (existing->gammaRaw.first != loaded_vid.gammaRaw.first ||
                        existing->gammaRaw.second != loaded_vid.gammaRaw.second)
                    {
                        continue;
                    }
                    if (((existing->properties() ^ loaded_vid.properties()) & P_GAMMA) != 0u)
                        continue;
                }

                std::unique_ptr<VID> mirror(existing->CreateMirror());

                mirror->nVid = nvid;
                mirror->name = objectName;
                mirror->vidName = vidFileName;
                mirror->LoadParameters(res);
                mirror->SetLayer();

                VID* const rawMirror = mirror.get();
                (void)mirror.release();
                return rawMirror;
            }

            res->seek(parameterBegin);
        }

        std::unique_ptr<VID> vid;
        WORD realType = 0;
        bool realHeadLoaded = false;
        RESOURCE resourceForLoad;
        bool generatedTempVid = false;
        const bool fontSource = hasExtension(loadVidName, ".fon") || hasExtension(loadVidName, ".ttf");
        const bool pictureSource = hasExtension(loadVidName, ".tga") ||
                                   hasExtension(loadVidName, ".bmp") ||
                                   hasExtension(loadVidName, ".flc") ||
                                   hasExtension(loadVidName, ".jpg");

        if (!fontSource && pictureSource)
        {
            const GamePath sourcePath = resolveGameFile(STRING(loadVidName));
            const STRING colorPath(gamePathString(sourcePath));


            const DWORD pictureVidOptions =
                (loadVidName.find("32bit") != std::string::npos) ? 1u : 0u;
            int pictureOpenResult = 1;

            if (loaded_vid.spriteClass != 19)
            {
                images::PICTURE_COMPOSITE_RESOURCE composite;
                pictureOpenResult = composite.openFilenames(colorPath, STRING(""), STRING(""));
                if (pictureOpenResult == 0)
                {
                    char* const tempName = _tempnam("c:\\tmp", "vid");
                    if (tempName)
                    {
                        loadVidName = tempName;
                        std::free(tempName);
                        composite.writeVidResource(STRING(loadVidName), pictureVidOptions);
                        generatedTempVid = true;
                    }
                }
            }
            else
            {
                images::PICTURE_SCROLL_COMPOSITE_RESOURCE composite;
                pictureOpenResult = composite.openFilenames(colorPath, STRING(""), STRING(""));
                if (pictureOpenResult == 0)
                {
                    char* const tempName = _tempnam("c:\\tmp", "vid");
                    if (tempName)
                    {
                        loadVidName = tempName;
                        std::free(tempName);
                        composite.writeVidResource(STRING(loadVidName), pictureVidOptions);
                        generatedTempVid = true;
                    }
                }
            }

            if (pictureOpenResult != 0 || !generatedTempVid)
                LOG::Write("LOAD::Can't open file %s", sourceVidName.c_str());
        }

        if (fontSource)
        {
            realType = static_cast<WORD>(VID_TYPE_FONT);
            loaded_vid.type = realType;
            vid.reset(new (std::nothrow) VID_FONT());
        }
        else
        {
            const GamePath resolvedVidPath = generatedTempVid
                ? STRING(loadVidName)
                : resolveGameFile(STRING(loadVidName));
            const STRING resolvedVidFilePath = gamePathString(resolvedVidPath);
            if (resourceForLoad.openFile(&resolvedVidFilePath, RESOURCE::ResTypes::VID) != 0)
            {
                LOG::ResourceError("VID [%i-%s]", 7, loadVidName.c_str(), 0, nvid, objectName.c_str());
                if (generatedTempVid)
                    std::remove(loadVidName.c_str());
                return nullptr;
            }
            if (resourceForLoad.GoBegin(RESOURCE::ResTypes::HEAD))
                LOG::Write("!!!ERROR!!!VID '%s': Load() not HEAD ", loadVidName.c_str());
            (void)resourceForLoad.read(&realType, sizeof(realType));
            loaded_vid.type = realType;


            if (realType & VID_TYPE_NEW_ZBUFFER)
                vid.reset(new (std::nothrow) VID_SURFACE());
            else if (realType & 0x0080u)
                vid.reset(new (std::nothrow) VID_LIGHT());
            else if ((realType & 0x0020u) && (realType & 0x0002u) && (realType & 0x0004u))
                vid.reset(new (std::nothrow) VID_HARDWARE_Z());
            else if (realType & 0x0020u)
                vid.reset(new (std::nothrow) VID_HARDWARE());
            else if (loaded_vid.spriteClass == B_BUILDEDTERRAIN)
                vid.reset(new (std::nothrow) VID_SOFTWARE16());
            else if (Graph->uses32BitColorDepth())
                vid.reset(new (std::nothrow) VID_SOFTWARE());
            else
                vid.reset(new (std::nothrow) VID_SOFTWARE16());

            realHeadLoaded = true;
        }

        vid->type = realType;
        vid->nVid = nvid;
        vid->name = objectName;


        vid->vidName = vidFileName;
        if (realHeadLoaded)
            vid->loadBasicParameters(&resourceForLoad);

        vid->LoadParameters(res);

        if (fontSource)
            vid->Load(nullptr);
        else if (realHeadLoaded)
            vid->Load(&resourceForLoad);

        if (generatedTempVid)
        {
            resourceForLoad.close();
            std::remove(loadVidName.c_str());
        }

        vid->SetLayer();

        VID* raw = vid.get();

        (void)vid.release();
        return raw;
    }


    VID* MAP::Vid(int nvid) const
    {


        if (nvid < 0)
            return EmptyVid;


        const BYTE* const owner = reinterpret_cast<const BYTE*>(this);
        const int count = *reinterpret_cast<const int*>(owner + core::application_layout::VidCount);
        if (nvid >= count)
            return EmptyVid;

        VID* const vid = *reinterpret_cast<VID* const*>(
            owner + core::application_layout::VidTable + static_cast<std::size_t>(nvid) * 4u);
        return vid ? vid : EmptyVid;
    }


    int MAP::ValidateVid(int nvid) const
    {
        const BYTE* const owner = reinterpret_cast<const BYTE*>(this);
        const int count = *reinterpret_cast<const int*>(owner + core::application_layout::VidCount);
        if (nvid < 0 || nvid >= count)
            return 0;
        return *reinterpret_cast<VID* const*>(
            owner + core::application_layout::VidTable + static_cast<std::size_t>(nvid) * 4u) != nullptr ? 1 : 0;
    }

    void MAP::ExchangeVid(VID* first, VID* second)
    {


        VID* const nullVid = EmptyVid;
        if (!first || !second || first == second || first == nullVid || second == nullVid)
            return;

        core::ApplicationVidTable& appVidTable = core::GlobalApplicationVidTable();
        const int rawCount = appVidTable.count();

        for (int index = 0; index < rawCount; ++index)
        {
            VID* vid = appVidTable.slot(index);
            if (!vid)
                continue;

            if (vid->linkVid == first)
                vid->linkVid = second;
            else if (vid->linkVid == second)
                vid->linkVid = first;

            for (int i = 0; i < VID::NO_ANIMATION; ++i)
            {
                if (vid->childVid[i] == first)
                    vid->childVid[i] = second;
                else if (vid->childVid[i] == second)
                    vid->childVid[i] = first;
            }
        }

        const int firstSlot = first->nVid;
        const int secondSlot = second->nVid;
        appVidTable.setSlotCell(firstSlot, second);
        appVidTable.setSlotCell(secondSlot, first);


        std::swap(first->exchangedVid, second->exchangedVid);
        std::swap(first->nVid, second->nVid);


        for (int i = 0; i < VID::ScriptFunctionSlotCount; ++i)
            std::swap(first->scriptFunction[i], second->scriptFunction[i]);
    }

    GamePath MAP::resolveGameFile(const STRING& gamePath) const
    {
        const std::string normalized = normalizeGamePath(gamePath.str());


        return STRING(normalized);
    }


    void MAP::DeleteExtraVid()
    {
        core::ApplicationDrawDispatcherState& drawState = core::GlobalApplicationDrawDispatcherState();
        for (int pass = 0; pass < core::ApplicationDrawDispatcherState::PassCount; ++pass)
        {
            core::ApplicationDrawPassBucket& bucket = drawState.drawPassBucket(pass);
            int cursor = bucket.count() - 1;
            while (cursor >= 0)
            {
                SPRITE* sprite = bucket.spriteAt(cursor);
                while (!sprite && --cursor >= 0)
                    sprite = bucket.spriteAt(cursor);
                if (!sprite)
                    break;

                VID* const vid = sprite->Vid();
                if ((vid->formatFlags() & VID_TYPE_EXTRA) != 0)
                {

                    DeleteSpriteThroughVirtualDeletingDestructor(sprite);
                }
                --cursor;
            }
        }

        core::ApplicationVidTable& appVidTable = core::GlobalApplicationVidTable();
        const int originalCount = appVidTable.count();
        for (int index = originalCount - 1; index >= 0; --index)
        {
            VID* const vid = appVidTable.slot(index);
            if (!vid || (vid->formatFlags() & VID_TYPE_EXTRA) == 0)
                continue;

            deleteVidThroughDeletingDestructor(vid);
            appVidTable.setSlotCell(index, nullptr);
        }

        int trimmedCount = originalCount;
        while (trimmedCount > 0 && appVidTable.slot(trimmedCount - 1) == nullptr)
            --trimmedCount;
        appVidTable.setStoredCount(trimmedCount);

    }

    void MAP::DeletePointerToSprite(SPRITE* sprite)
    {
        BYTE* const owner = reinterpret_cast<BYTE*>(this);
        PLAYER* const players[4] = {
            *reinterpret_cast<PLAYER**>(owner + core::application_layout::PlayerSlots),
            *reinterpret_cast<PLAYER**>(owner + core::application_layout::PlayerSlots + core::application_layout::PlayerSlotStride),
            *reinterpret_cast<PLAYER**>(owner + core::application_layout::PlayerSlots + core::application_layout::PlayerSlotStride * 2u),
            *reinterpret_cast<PLAYER**>(owner + core::application_layout::PlayerSlots + core::application_layout::PlayerSlotStride * 3u)
        };
        for (PLAYER* const player : players)
        {
            using PlayerDeletePointerFn = std::intptr_t (__thiscall*)(as1::PLAYER*, as1::SPRITE*);
            void** const playerVtable = *reinterpret_cast<void***>(player);
            (void)reinterpret_cast<PlayerDeletePointerFn>(playerVtable[1])(player, sprite);
        }

        scriptRuntime().DeletePointerToObject(sprite);
        groupOwner().DeletePointerToSprite(sprite);

        if (sprite->listReferenceCount() > 1)
        {
            SPRITE_COLLECTOR* hash = GlobalSpriteCollector();
            int cursor = static_cast<int>(hash->overflowList().count()) - 1;
            while (cursor >= 0)
            {
                const core::List<SPRITE*>& overflow = hash->overflowList();
                SPRITE* current = overflow.at(static_cast<std::size_t>(cursor));
                while (!current && --cursor >= 0)
                    current = overflow.at(static_cast<std::size_t>(cursor));
                if (!current)
                    break;

                current->DeletePointerToSprite(sprite);
                hash = GlobalSpriteCollector();
                if (cursor > static_cast<int>(hash->overflowList().count()))
                    cursor = static_cast<int>(hash->overflowList().count());
                --cursor;
            }
        }

        for (int pass = 0; pass < core::ApplicationDrawDispatcherState::PassCount; ++pass)
        {
            if (sprite->listReferenceCount() <= 1)
                continue;

            const core::ApplicationDrawPassBucket& bucket = *reinterpret_cast<const core::ApplicationDrawPassBucket*>(
                owner + core::application_layout::DrawLayerOwners +
                static_cast<std::size_t>(pass) * core::application_layout::DrawLayerStride);
            int cursor = bucket.count() - 1;
            while (cursor >= 0)
            {
                SPRITE* current = bucket.spriteAt(cursor);
                while (!current && --cursor >= 0)
                    current = bucket.spriteAt(cursor);
                if (!current)
                    break;
                current->DeletePointerToSprite(sprite);
                --cursor;
            }
        }
    }



    void MAP::release()
    {


        const core::ApplicationVidTable& releaseVidTable = core::GlobalApplicationVidTable();
        for (int index = 0; index < releaseVidTable.count(); ++index)
        {
            VID* const vid = releaseVidTable.slot(index);
            if (!vid)
                continue;
            const int count0 = static_cast<int>(vid->NoSprites(0));
            const int count1 = static_cast<int>(vid->NoSprites(1));
            const int count2 = static_cast<int>(vid->NoSprites(2));
            const int count3 = static_cast<int>(vid->NoSprites(3));
            if ((count0 + count1 + count2 + count3) == 0)
                continue;
            LOG::Write("NoVid[%3i]=%i %i %i %i %s Layer=%i %s",
                       index, count0, count1, count2, count3,
                       vid->name.c_str(), vid->renderLayer(), vid->sourceVidPath().c_str());
        }

        relationTable().clear();

        core::SetApplicationTickScale(1.0f);
        core::DisplayedFramesPerSecond() = 0u;
        core::AccumulatedFpsFrameCount() = 0u;

        data().graph->m_movie.Release();
        data().graph->SetWind(25, ANGLE(static_cast<unsigned char>(200)));
        data().graph->SetEnvironment(0xFFFFFFFFu);

        std::uint32_t rawFlags = core::ApplicationFlags();
        if ((rawFlags & application_flags::DemoUseResource) != 0u)
        {
            Mouse->Enable();
            demoResource().close();
        }
        if ((rawFlags & application_flags::DemoWriteToResource) != 0u)
        {
            const std::int32_t endMarker = -1;
            demoResource().write(&endMarker, sizeof(endMarker));
            demoResource().EndSection();
            demoResource().close();
        }

        rawFlags &= ~(application_flags::DemoUseResource | application_flags::DemoWriteToResource);
        core::SetApplicationFlags(rawFlags);
        core::SetApplicationScrollType(1u);

        LOG::Write("Player release");

        if (win::ApplicationWin* const app = win::applicationWinInstance())
        {
            PLAYER* const players[4] = {
                app->playerSlotByIndex(0),
                app->playerSlotByIndex(1),
                app->playerSlotByIndex(2),
                app->playerSlotByIndex(3)
            };
            for (PLAYER* const player : players)
            {
                if (!player)
                    continue;

                void** const vtable = *reinterpret_cast<void***>(player);
                typedef void (__thiscall *ReleasePlayerMethod)(PLAYER*);
                reinterpret_cast<ReleasePlayerMethod>(vtable[4])(player);
            }
        }

        LOG::Write("Sprite release");

        core::ApplicationDrawDispatcherState& drawState = core::GlobalApplicationDrawDispatcherState();
        ENGINE::globaldeleting = 1;
        for (int pass = 0; pass < core::ApplicationDrawDispatcherState::PassCount; ++pass)
        {
            core::ApplicationDrawPassBucket& bucket = drawState.drawPassBucket(pass);
            int cursor = bucket.count() - 1;
            while (cursor >= 0)
            {
                SPRITE* sprite = bucket.spriteAt(cursor);
                while (!sprite && --cursor >= 0)
                    sprite = bucket.spriteAt(cursor);
                if (!sprite)
                    break;

                DeleteSpriteThroughVirtualDeletingDestructor(sprite);

                --cursor;
            }
        }

        ENGINE::globaldeleting = 0;
        for (int pass = 0; pass < core::ApplicationDrawDispatcherState::PassCount; ++pass)
        {
            const core::ApplicationDrawPassBucket& bucket = drawState.drawPassBucket(pass);
            for (int cursor = bucket.count() - 1; cursor >= 0; --cursor)
            {
                SPRITE* const survivor = bucket.spriteAt(cursor);
                if (!survivor)
                    continue;
                const int nvid = survivor->Vid() ? survivor->Vid()->nVid : -1;
                LOG::ResourceError("SPRITE %i", 10, "Sprite exist after delete", cursor, nvid);
                break;
            }
        }

        if (!groupOwner().empty())
            LOG::ResourceError("%s", 10, "Incorrect delete groups in DeleteAll()", 0, "");

        LOG::Write("Menu   release");

        BaseSpriteList<0>& frameList = applicationFrameSpriteList();
        if (frameList.activeCount() != 0)
        {
            SPRITE* const first = frameList.at(0);
            const int nvid = first->Vid() ? first->Vid()->nVid : -1;
            LOG::ResourceError("SPRITE %i", 10, "Menu sprite exist after delete", 0, nvid);
            frameList.deleteAllSprites();
        }

        const std::uint32_t now = core::CurrentTimeMilliseconds();
        const std::uint32_t start = core::ApplicationWorldStartTime();
        const std::uint32_t elapsed = now - start;
        const std::uint32_t averageFps = elapsed != 0u
            ? (1000u * core::ApplicationWorldFrameCounter()) / elapsed
            : 0u;
        LOG::Write("Average fps=%i", static_cast<int>(averageFps));

        LOG::Write("Script release");

        scriptRuntime().resetScriptVmState();
        core::SetApplicationWorldFrameCounter(0u);

        DeleteExtraVid();

        const core::ApplicationVidTable& vidTable = core::GlobalApplicationVidTable();
        const int vidCount = vidTable.count();
        for (int index = 0; index < vidCount; ++index)
        {
            if (VID* const vid = vidTable.slot(index))
                vid->ResetSprites();
        }

        for (std::size_t i = 0; i < core::kScriptCallbackSlotCount; ++i)
            core::EvFunctionNumber[i] = 1000000 + static_cast<int>(i);

    }










    namespace
    {
        constexpr int END_SPRITE_INT = -1;
        SPRITE* const END_SPRITE_PTR = reinterpret_cast<SPRITE*>(~uintptr_t(0));

    }










    float MAP::FromScreenX(float x) const
    {
        const BYTE* const owner = reinterpret_cast<const BYTE*>(this);
        return x + *reinterpret_cast<const float*>(owner + core::application_layout::CameraShiftX);
    }


    float MAP::FromScreenY(float y) const
    {
        const BYTE* const owner = reinterpret_cast<const BYTE*>(this);
        return y + *reinterpret_cast<const float*>(owner + core::application_layout::CameraShiftY);
    }


    float MAP::ToScreenX(float x) const
    {
        const BYTE* const owner = reinterpret_cast<const BYTE*>(this);
        const float shiftX = *reinterpret_cast<const float*>(owner + core::application_layout::CameraShiftX);
        return x - shiftX;
    }


    float MAP::ToScreenY(float y) const
    {
        const BYTE* const owner = reinterpret_cast<const BYTE*>(this);
        return y - *reinterpret_cast<const float*>(owner + core::application_layout::CameraShiftY);
    }

    float MAP::ToScreenY(float y, float z) const
    {
        return ToScreenY(y - z);
    }


    SPRITE* MAP::FirstSprite(int layer, int* index)
    {
        BYTE* const owner = reinterpret_cast<BYTE*>(this);
        auto* const sprites = reinterpret_cast<core::List<SPRITE*>*>(
            owner + core::application_layout::DrawLayerOwners +
            static_cast<std::size_t>(layer) * core::application_layout::DrawLayerStride);
        *index = sprites->activeCount() - 1;
        SPRITE* const* const data = sprites->data();
        if (*index < 0)
            return nullptr;
        while (!data[*index])
        {
            --*index;
            if (*index < 0)
                return nullptr;
        }
        return data[*index];
    }


    SPRITE* MAP::NextSprite(int layer, int* index)
    {
        --*index;
        if (*index < 0)
            return nullptr;

        BYTE* const owner = reinterpret_cast<BYTE*>(this);
        auto* const sprites = reinterpret_cast<core::List<SPRITE*>*>(
            owner + core::application_layout::DrawLayerOwners +
            static_cast<std::size_t>(layer) * core::application_layout::DrawLayerStride);
        SPRITE* const* const data = sprites->data();
        while (!data[*index])
        {
            --*index;
            if (*index < 0)
                return nullptr;
        }
        return data[*index];
    }

    void MAP::SetScrollBox(float minX, float minY, float maxX, float maxY)
    {
        data().scrollMinXY.x = minX;
        data().scrollMinXY.y = minY;
        data().scrollMaxXY.x = maxX;
        data().scrollMaxXY.y = maxY;
        BYTE* const owner = reinterpret_cast<BYTE*>(this);
        *reinterpret_cast<float*>(owner + core::application_layout::ScrollMinX) = minX;
        *reinterpret_cast<float*>(owner + core::application_layout::ScrollMaxX) = maxX;
        *reinterpret_cast<float*>(owner + core::application_layout::ScrollMinY) = minY;
        *reinterpret_cast<float*>(owner + core::application_layout::ScrollMaxY) = maxY;
    }

    void MAP::SetShiftCoor(float centerX, float centerY, int effect)
    {
        BYTE* const owner = reinterpret_cast<BYTE*>(this);
        const VECTOR2 scrollMin{
            *reinterpret_cast<const float*>(owner + core::application_layout::ScrollMinX),
            *reinterpret_cast<const float*>(owner + core::application_layout::ScrollMinY)};
        const VECTOR2 scrollMax{
            *reinterpret_cast<const float*>(owner + core::application_layout::ScrollMaxX),
            *reinterpret_cast<const float*>(owner + core::application_layout::ScrollMaxY)};
        GRAPH* const graph = Graph;
        if (!graph)
            return;
        const float screenW = graph->screenWidth();
        const float screenH = graph->screenHeight();
                const float clampMinX = scrollMin.x - graph->viewportLeft();
        const float clampMinY = scrollMin.y - graph->viewportTop();
        const float clampMaxX = scrollMax.x - graph->viewportRight();
        const float clampMaxY = scrollMax.y - graph->viewportBottom();


        float shiftX = centerX - screenW * 0.5f;
        float shiftY = centerY - screenH * 0.5f;


        if ((static_cast<std::uint32_t>(effect) & 0x10000000u) == 0u)
        {

            shiftX = maxssForMap(clampMinX, shiftX);
            shiftX = minssForMap(clampMaxX, shiftX);
            shiftY = maxssForMap(clampMinY, shiftY);
            shiftY = minssForMap(clampMaxY, shiftY);
        }

        const float currentCameraX = *reinterpret_cast<const float*>(owner + core::application_layout::CameraShiftX);
        const float currentCameraY = *reinterpret_cast<const float*>(owner + core::application_layout::CameraShiftY);

        if (currentCameraX == shiftX && currentCameraY == shiftY)
            return;

        if (effect == 2)
        {
            graph->Effect(2,
                               truncateFloatToInt32ForMap(centerX),
                               truncateFloatToInt32ForMap(centerY),
                               0);
            return;
        }

        const float deltaX = shiftX - currentCameraX;
        const float deltaY = shiftY - currentCameraY;
        *reinterpret_cast<float*>(owner + core::application_layout::CameraShiftX) = shiftX;
        *reinterpret_cast<float*>(owner + core::application_layout::CameraShiftY) = shiftY;
        data().shiftXY.x = shiftX;
        data().shiftXY.y = shiftY;

        BaseSpriteList<0>& frameList = applicationFrameSpriteList();
        const int persistentCount = frameList.activeCount();
        for (int i = 0; i < persistentCount; ++i)
        {
            SPRITE* sprite = frameList.at(static_cast<std::size_t>(i));
            if (sprite)
                sprite->ChangeCoor(sprite->X() + deltaX, sprite->Y() + deltaY, sprite->Z());
        }

        SPRITE* currentMouseSprite = mouseSprite();
        currentMouseSprite->ChangeCoor(currentMouseSprite->X() + deltaX,
                                       currentMouseSprite->Y() + deltaY,
                                       currentMouseSprite->Z());

        data().shiftDeltaXY.x += deltaX;
        data().shiftDeltaXY.y += deltaY;


        graph->SetCamera(shiftX, shiftY);

        D3DMATRIX view{};
        view._11 = 1.0f;
        view._22 = 1.0f;
        view._32 = -1.0f;
        view._33 = 1.0f;


        const float viewportCenterX = (graph->viewportRight() + graph->viewportLeft()) * 0.5f;
        const float viewportCenterY = (graph->viewportBottom() + graph->viewportTop()) * 0.5f;
        view._41 = -shiftX - viewportCenterX;
        view._42 = -shiftY - viewportCenterY;
        view._44 = 1.0f;

        IDirect3DDevice8* device = static_cast<IDirect3DDevice8*>(graph->deviceHandle());
        const HRESULT result = device->SetTransform(D3DTS_VIEW, &view);
        if (FAILED(result))
            LOG::ResourceError("%s", 8, "Transform view", static_cast<int>(result), "GRAPH");
    }


    SPRITE* MAP::OldLoadSprite(BaseStream* res)
    {
        int oldAddr = 0;
        std::int16_t nvid16 = 0;
        std::int16_t x16 = 0;
        std::int16_t y16 = 0;
        std::int16_t z16 = 0;
        BYTE directionByte = 0;
        BYTE legacyTailByte = 0;

        res->read(&oldAddr, 4);
        if (oldAddr == END_SPRITE_INT)
            return END_SPRITE_PTR;

        res->read(&nvid16, 2);
        res->read(&x16, 2);
        res->read(&y16, 2);
        res->read(&z16, 2);
        res->read(&directionByte, 1);
        res->read(&legacyTailByte, 1);
        (void)legacyTailByte;

        const int nvid = static_cast<int>(nvid16);
        SPRITE* newSprite = nullptr;
        const bool isNullRecord = (nvid == -1);
        const bool hasVid = !isNullRecord && ValidateVid(nvid);
        if (hasVid)
        {
            newSprite = CreateSprite(Vid(nvid),
                                     VECTOR(static_cast<float>(x16), static_cast<float>(y16), static_cast<float>(z16)),
                                     ANGLE(static_cast<int>(directionByte)));
            BindLoadedSpriteHandle(oldAddr, newSprite);
        }
        else
        {
            BindLoadedSpriteHandle(oldAddr, nullptr);
            LOG::ResourceError("%s", 3, "sprite, this vid not exist", nvid, "");
        }
        return newSprite;
    }


    SPRITE* MAP::LoadSprite(BaseStream* res, int version)
    {
        int oldAddr = 0;
        int nvid = 0;
        int army = 0;
        float x = 0.0f, y = 0.0f, z = 0.0f;
        ANGLE direct;

        res->read(&oldAddr, 4);
        if (oldAddr == END_SPRITE_INT)
            return END_SPRITE_PTR;

        res->read(&nvid, 4);
        if (version > 9)
        {
            res->read(&x, 4);
            res->read(&y, 4);
            res->read(&z, 4);
        }
        else
        {
            int ix = 0, iy = 0, iz = 0;
            res->read(&ix, 4);
            res->read(&iy, 4);
            res->read(&iz, 4);
            x = static_cast<float>(ix);
            y = static_cast<float>(iy);
            z = static_cast<float>(iz);
        }

        int directionRaw = 0;
        res->read(&directionRaw, 4);
        direct = ANGLE(static_cast<unsigned char>(directionRaw));
        res->read(&army, 4);

        SPRITE* newSprite = nullptr;
        const bool isNullRecord = (nvid == -1);
        const bool hasVid = !isNullRecord && ValidateVid(nvid);
        if (hasVid)
        {
            newSprite = CreateSprite(Vid(nvid), VECTOR(x, y, z), direct);
            if (newSprite)
            {
                BindLoadedSpriteHandle(oldAddr, newSprite);
                newSprite->ChangeArmy(army);
            }
        }
        else
        {
            LOG::ResourceError("%s", 3, "sprite, this vid not exist", nvid, "");
        }
        if (!newSprite)
            BindLoadedSpriteHandle(oldAddr, nullptr);
        return newSprite;
    }



    void MAP::CreateEmptyHardwareGround()
    {
        core::ApplicationVidTable& appVidTable = core::GlobalApplicationVidTable();
        if (appVidTable.count() < 1025)
            appVidTable.setStoredCount(1025);

        if (VID* const oldGround = appVidTable.slot(1024))
        {
            deleteVidThroughDeletingDestructor(oldGround);
        }

        const int groundSizeY = mapConvertFloatToInt32(SizeY());
        const int groundSizeX = mapConvertFloatToInt32(SizeX());
        auto ground = std::unique_ptr<VID_HARDWARE>(
            new (std::nothrow) VID_HARDWARE(1024, groundSizeX, groundSizeY));
        VID_HARDWARE* const rawGround = ground.get();
        rawGround->weapon = weaponTable();

        (void)ground.release();

        appVidTable.setSlotCell(1024, rawGround);

        CreateSprite(rawGround,
                     VECTOR(SizeX() * 0.5f, SizeY() * 0.5f, 0.0f),
                     ANGLE(static_cast<unsigned char>(0)),
                     nullptr,
                     false,
                     false);
        (void)writeLogLine(g_fileLogger, "Create Empty Hardware Ground");
    }

    SPRITE* MAP::CreateSprite(VID* vid, const VECTOR& v, const ANGLE& direction, SPRITE* parent, bool remoteControlled, bool createChildRoute)
    {
        if (!vid)
            return nullptr;

        (void)remoteControlled;
        (void)createChildRoute;
        if (as1::win::ApplicationWin* const application = as1::win::applicationWinInstance())
            return application->CreateSprite(vid, v, direction, parent);
        return nullptr;
    }


    PLAYER* MAP::Player(int playerIndex) const noexcept
    {

        return *reinterpret_cast<PLAYER* const*>(
            reinterpret_cast<const BYTE*>(this) +
            core::application_layout::PlayerSlots +
            static_cast<std::size_t>(playerIndex & 3) * sizeof(std::uint32_t));
    }


    void MAP::SetFlagman(int playerIndex, SPRITE* sprite) noexcept
    {


        PLAYER* const player = *reinterpret_cast<PLAYER**>(
            reinterpret_cast<BYTE*>(this) +
            core::application_layout::PlayerSlots +
            static_cast<std::size_t>(playerIndex & 3) * sizeof(std::uint32_t));
        using SetFlagmanFn = void (__thiscall*)(PLAYER*, SPRITE*);
        void** const vtable = *reinterpret_cast<void***>(player);
        reinterpret_cast<SetFlagmanFn>(vtable[5])(player, sprite);
    }


    SPRITE* MAP::flagmanSpriteForPlayer(int playerIndex) const noexcept
    {
        PLAYER* const player = *reinterpret_cast<PLAYER* const*>(
            reinterpret_cast<const BYTE*>(this) +
            core::application_layout::PlayerSlots +
            static_cast<std::size_t>(playerIndex & 3) * sizeof(std::uint32_t));

        return *reinterpret_cast<SPRITE* const*>(
            reinterpret_cast<const BYTE*>(player) + 0x10u);
    }


    SPRITE* MAP::ReadPointer(BaseStream* stream)
    {
        std::int32_t handle = 0;
        stream->read(&handle, 4u);
        if (handle == -1)
            return END_SPRITE_PTR;
        return relationTable().getPointer(handle);
    }


    void MAP::SetGroundZ(float x, float y, float z)
    {
        const float mapSizeX = SizeX();
        const float mapSizeY = SizeY();
        if (x87LessOrUnorderedForMap(x, 0.0f))
            return;
        if (!x87LessOrUnorderedForMap(x, mapSizeX))
            return;
        if (x87LessOrUnorderedForMap(y, 0.0f))
            return;
        if (!x87LessOrUnorderedForMap(y, mapSizeY))
            return;

        const int zInt = mapConvertFloatToInt32(z);
        const int negativeGridY = mapMultiplyAndConvertToInt32(y, -0.125f);
        const int yProduct = static_cast<int>(
            static_cast<std::uint32_t>(negativeGridY) *
            static_cast<std::uint32_t>(terrainGridWidth()));
        const int xInt = mapConvertFloatToInt32(x);
        const int xDiv8 = (xInt + (xInt < 0 ? 7 : 0)) >> 3;
        const int index = static_cast<int>(
            static_cast<std::uint32_t>(xDiv8) - static_cast<std::uint32_t>(yProduct));
        short* const grid = terrainGrid();
        if (static_cast<int>(grid[index]) < zInt)
            grid[index] = static_cast<short>(static_cast<unsigned int>(zInt) & 0xFFFFu);
    }


    void MAP::SetTempGroundZ(float x, float y, float z)
    {
        const float mapSizeX = SizeX();
        const float mapSizeY = SizeY();
        if (x < 0.0f || x >= mapSizeX || y < 0.0f || y >= mapSizeY)
            return;
        const int zInt = static_cast<int>(z);
        const int index = static_cast<int>(x) / 8 - static_cast<int>(y * -0.125f) * terrainGridWidth();
        const BYTE* const owner = reinterpret_cast<const BYTE*>(this);
        short* const temp = *reinterpret_cast<short* const*>(owner + core::application_layout::TempTerrainGrid);
        if (static_cast<int>(temp[index]) < zInt)
            temp[index] = static_cast<short>(zInt);
    }


    void MAP::ClearTempGroundZ(float x, float y, float z)
    {
        const float mapSizeX = SizeX();
        const float mapSizeY = SizeY();
        if (x < 0.0f || x >= mapSizeX || y < 0.0f || y >= mapSizeY)
            return;
        const int zInt = static_cast<int>(z);
        const int index = static_cast<int>(x) / 8 - static_cast<int>(y * -0.125f) * terrainGridWidth();
        const BYTE* const owner = reinterpret_cast<const BYTE*>(this);
        short* const temp = *reinterpret_cast<short* const*>(owner + core::application_layout::TempTerrainGrid);
        if (static_cast<int>(temp[index]) == zInt)
            temp[index] = 0;
    }


    float MAP::GetGroundZ(const VECTOR2& v) const
    {
        const float mapSizeX = SizeX();
        const float mapSizeY = SizeY();
        const int gridX = mapGridCoordinate(v.x, mapSizeX);
        const int gridY = mapGridCoordinate(v.y, mapSizeY);
        const std::size_t index = static_cast<std::size_t>(gridX + terrainGridWidth() * gridY);
        const short* const permanent = terrainGrid();
        const int permanentZ = static_cast<int>(permanent[index]);
        const BYTE* const owner = reinterpret_cast<const BYTE*>(this);
        const short* const temp = *reinterpret_cast<short* const*>(owner + core::application_layout::TempTerrainGrid);
        const int tempZ = static_cast<int>(temp[index]);
        return static_cast<float>(permanentZ > tempZ ? permanentZ : tempZ);
    }


    float MAP::GetGroundZScr(float screenX, float screenY) const noexcept
    {
        const int gridX = mapGridCoordinate(screenX, SizeX());
        const int baseY = mapGridCoordinate(screenY, SizeY());
        const int width = terrainGridWidth();
        const int height = terrainGridHeight();

        int row = baseY + 32;
        if (row >= height)
            row = height - 1;

        int index = gridX + row * width;
        const short* const permanent = terrainGrid();
        const BYTE* const owner = reinterpret_cast<const BYTE*>(this);
        const short* const temporary = *reinterpret_cast<short* const*>(
            owner + core::application_layout::TempTerrainGrid);

        for (; row >= baseY; --row, index -= width)
        {
            const int z = (row - baseY) * 8;
            if (static_cast<int>(permanent[index]) >= z ||
                static_cast<int>(temporary[index]) >= z)
                return static_cast<float>(z);
        }
        return 0.0f;
    }


    int MAP::isLineUnderGroundWithBullet(float startX, float startY, float startZ,
                                             float endX, float endY, float endZ) const noexcept
    {
        const float deltaX = endX - startX;
        const float deltaY = endY - startY;
        const float deltaZ = endZ - startZ;
        const float absX = std::fabs(deltaX);
        const float absY = std::fabs(deltaY);
        const float maxXY = (absX > absY) ? absX : absY;
        const float stepMetric = maxXY * 0.0833333358168602f;

        const auto terrainProbeBlocked = [this](float x, float y, float z) noexcept -> bool
        {


            return GetGroundZ(VECTOR2{x, y}) > z;
        };

        const int sampleCount = static_cast<int>(stepMetric);
        if (sampleCount == 0)
            return terrainProbeBlocked(startX, startY, startZ) ? 1 : 0;

        const float stepX = deltaX / stepMetric;
        const float stepY = deltaY / stepMetric;
        const float stepZ = deltaZ / stepMetric;

        const auto approximate2d = [](float a, float b) noexcept -> float
        {
            const float aa = std::fabs(a);
            const float ab = std::fabs(b);
            return aa > ab
                ? aa * 0.9610000252723694f + ab * 0.39800000190734863f
                : aa * 0.39800000190734863f + ab * 0.9610000252723694f;
        };

        float x = startX;
        float y = startY;
        float z = startZ;
        for (int remaining = sampleCount - 1; remaining >= 0; --remaining)
        {
            x += stepX;
            y += stepY;
            z += stepZ;
            if (!terrainProbeBlocked(x, y, z))
                continue;

            const float horizontal = approximate2d(x - startX, y - startY);
            const float distance = approximate2d(horizontal, z - startZ);
            const int distanceCode = static_cast<int>(distance);
            return distanceCode != 0 ? distanceCode : 1;
        }
        return 0;
    }


    float MAP::GetGroundZ(const VID* vid, VECTOR2 v) const
    {
        if (vid->spriteClass != B_MAN)
            return GetGroundZ(v);

        const float mapSizeX = SizeX();
        const float mapSizeY = SizeY();


        const long double halfXExtended =
            static_cast<long double>(vid->sizeXYZ.x) * 0.5L;
        const long double halfYExtended =
            static_cast<long double>(vid->sizeXYZ.y) * 0.5L;
        const float halfYStored = static_cast<float>(halfYExtended);
        const float maxXRaw = static_cast<float>(
            static_cast<long double>(v.x) + halfXExtended - 3.0L);
        const float maxYRaw = static_cast<float>(
            static_cast<long double>(v.y) + halfYExtended - 3.0L);
        const float minXRaw = static_cast<float>(
            static_cast<long double>(v.x) - (halfXExtended - 3.0L));
        const float minYRaw = static_cast<float>(
            static_cast<long double>(v.y) -
            (static_cast<long double>(halfYStored) - 3.0L));

        const auto scaledGridCoordinate = [](float value, float limit) noexcept -> long double
        {
            if (x87LessOrUnorderedForMap(value, 0.0f))
                return 0.0L;
            if (x87LessOrUnorderedForMap(value, limit))
                return static_cast<long double>(value) * 0.125L;
            return (static_cast<long double>(limit) - 1.0L) * 0.125L;
        };

        const long double minX = scaledGridCoordinate(minXRaw, mapSizeX);
        const long double minY = scaledGridCoordinate(minYRaw, mapSizeY);
        const long double maxX = scaledGridCoordinate(maxXRaw, mapSizeX);
        const long double maxY = scaledGridCoordinate(maxYRaw, mapSizeY);

        std::int32_t result = -16383;
        if (minY > maxY)
            return static_cast<float>(result);

        const short* const permanent = terrainGrid();
        const BYTE* const owner = reinterpret_cast<const BYTE*>(this);
        const short* const temporary = *reinterpret_cast<short* const*>(
            owner + core::application_layout::TempTerrainGrid);
        const std::int32_t gridX = terrainGridWidth();
        long double scanY = minY;
        for (;;)
        {
            if (minX <= maxX)
            {
                const std::int32_t gy = static_cast<std::int32_t>(std::trunc(scanY));
                const std::int32_t row = static_cast<std::int32_t>(
                    static_cast<std::uint32_t>(gy) *
                    static_cast<std::uint32_t>(gridX));

                long double scanX = minX;
                for (;;)
                {
                    const std::int32_t gx = static_cast<std::int32_t>(std::trunc(scanX));
                    const std::int32_t index = static_cast<std::int32_t>(
                        static_cast<std::uint32_t>(row) +
                        static_cast<std::uint32_t>(gx));
                    const std::int32_t permanentZ = static_cast<std::int16_t>(permanent[index]);
                    if (permanentZ > result)
                        result = permanentZ;
                    const std::int32_t temporaryZ = static_cast<std::int16_t>(temporary[index]);
                    if (temporaryZ > result)
                        result = temporaryZ;

                    scanX += 1.0L;
                    if (scanX > maxX)
                        break;
                }
            }

            scanY += 1.0L;
            if (scanY > maxY)
                break;
        }

        return static_cast<float>(result);
    }


    void MAP::ResetGroundZ()
    {
        releaseTerrainGridStorage();

        const int gridX = terrainGridDimension(SizeX());
        const int gridY = terrainGridDimension(SizeY());
        setTerrainGridDimensions(gridX, gridY);
        const std::size_t cells = static_cast<std::size_t>(gridX) * static_cast<std::size_t>(gridY);
        const std::size_t bytes = cells * sizeof(short);
        short* const ground = static_cast<short*>(::operator new(bytes, std::nothrow));
        short* const tempGround = static_cast<short*>(::operator new(bytes, std::nothrow));
        BYTE* const owner = reinterpret_cast<BYTE*>(this);
        *reinterpret_cast<short**>(owner + core::application_layout::TerrainGrid) = ground;
        *reinterpret_cast<short**>(owner + core::application_layout::TempTerrainGrid) = tempGround;
        std::memset(ground, 0, bytes);
        std::memset(tempGround, 0, bytes);
    }







    STRING* MAP::PopStr()
    {
        SCRIPT* const script = reinterpret_cast<SCRIPT*>(
            reinterpret_cast<std::uint8_t*>(this) + core::application_layout::ScriptRuntime);
        const int newIndex = script->m_data.stackCount - 1;
        script->m_data.stackCount = newIndex;
        script::StackObject* const top = script->mutableExecutionStackStorageAt(newIndex);

        if ((top->flags & script::STACK_OBJECT_INT) != 0)
        {
            char numericTextBuffer[0x80];
            std::memset(numericTextBuffer, 0, sizeof(numericTextBuffer));
            _itoa(top->intValue, numericTextBuffer, 10);

            STRING convertedText;
            convertedText.AssignAllocatedCopyWithoutRelease(numericTextBuffer);
            assignStringFromString(top->text, convertedText);
            convertedText.ReleaseOwnedStorage();
        }

        return &top->text;
    }


    int MAP::PopInt()
    {
        SCRIPT* const script = reinterpret_cast<SCRIPT*>(
            reinterpret_cast<std::uint8_t*>(this) + core::application_layout::ScriptRuntime);
        const int oldIndex = script->m_data.stackCount - 1;
        script::StackObject* const top = script->mutableExecutionStackStorageAt(oldIndex);
        script->m_data.stackCount = oldIndex;
        return mapStackValueToInteger(*top);
    }


    VID* MAP::PopVid(const char* errorContext)
    {
        const int nvid = PopInt();
        auto* const owner = reinterpret_cast<std::uint8_t*>(this);
        const int count = *reinterpret_cast<const int*>(
            owner + core::application_layout::VidCount);
        VID* vid = EmptyVid;
        if (nvid >= 0 && nvid < count)
        {
            VID* const slot = *reinterpret_cast<VID**>(
                owner + core::application_layout::VidTable +
                static_cast<std::size_t>(nvid) * sizeof(VID*));
            if (slot)
                vid = slot;
        }
        if (vid == EmptyVid && errorContext && *errorContext)
            LOG::Write("!!!ERROR!!!SCRIPT: Invalid nvid %s %i", errorContext, nvid);
        return vid;
    }


    void MAP::PushInt(int value)
    {
        SCRIPT* const script = reinterpret_cast<SCRIPT*>(
            reinterpret_cast<std::uint8_t*>(this) + core::application_layout::ScriptRuntime);
        script::StackObject obj;
        obj.assignFields(static_cast<std::uint8_t>(script::STACK_OBJECT_INT), value, STRING());
        reinterpret_cast<script::StackObjectList*>(&script->m_data.stackListVtable)->appendFields(
            obj.flags, obj.intValue, obj.text);
    }


    void MAP::PushStr(const STRING& value)
    {
        SCRIPT* const script = reinterpret_cast<SCRIPT*>(
            reinterpret_cast<std::uint8_t*>(this) + core::application_layout::ScriptRuntime);

        STRING copiedText;
        copiedText.AssignAllocatedCopyWithoutRelease(value.c_str());

        reinterpret_cast<script::StackObjectList*>(&script->m_data.stackListVtable)->appendFields(
            static_cast<std::uint8_t>(script::STACK_OBJECT_STRING), 0, copiedText);
        copiedText.ReleaseOwnedStorage();
    }


}

