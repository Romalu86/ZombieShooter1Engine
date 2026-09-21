#include "menu.h"

#include <cmath>
#include <new>

#include "core/application.h"
#include "core/log.h"
#include "core/resource.h"
#include "graph.h"
#include "input.h"
#include "map.h"
#include "sprite.h"
#include "vid/vid.h"

namespace as1
{

    MENU::MENU() noexcept
    {
        m_items = nullptr;
        m_count = 0;
        m_capacity = 0;
        m_selectedSprite = nullptr;
        m_controlFlags &= ~0x3u;
        setVtableToken(core::List<SPRITE*>::CurrentImageCoreListVtable());
    }

    MENU::~MENU() = default;


    int MENU::Load(const STRING& path)
    {

        RESOURCE resource;
        if (resource.openFile(&path, RESOURCE::ResTypes::MENU) != 0)
        {
            LOG::ResourceError("MENU", 7, path.c_str(), 0, 0);
            return 1;
        }

        if (resource.GoBegin(RESOURCE::ResTypes::HEAD) != 0)
        {
            LOG::ResourceError("MENU", 11, "'HEAD'in menu", 0, 0);
            return 1;
        }

        int version = 0;
        int sizeX = 0;
        int sizeY = 0;
        int shiftX = 0;
        int shiftY = 0;
        resource.read(&version, 4);
        resource.read(&sizeX, 4);
        resource.read(&sizeY, 4);
        resource.read(&shiftX, 4);
        resource.read(&shiftY, 4);

        MAP* const map = Map;
        GRAPH* const graph = Graph;


        SPRITE* const endSprite = reinterpret_cast<SPRITE*>(~static_cast<std::uintptr_t>(0));

        if (resource.GoNext(RESOURCE::ResTypes::SPRITE) == 0)
        {
            for (;;)
            {
                SPRITE* const sprite = map->LoadSprite(&resource, version);
                if (sprite == endSprite)
                    break;
                if (sprite)
                {
                    const float centeredX = sprite->X()
                        - static_cast<float>(shiftX)
                        - static_cast<float>(sizeX / 2)
                        + static_cast<float>(graph->SizeX()) * 0.5f;
                    const float centeredY = sprite->Y()
                        - static_cast<float>(shiftY)
                        - static_cast<float>(sizeY / 2)
                        + static_cast<float>(graph->SizeY()) * 0.5f;
                    sprite->ChangeCoor(centeredX, centeredY, sprite->Z());
                    sprite->dispatchVirtualAction(ActionCode::ACT_RESTORE,
                        static_cast<int>(reinterpret_cast<std::uintptr_t>(&resource)),
                        version,
                        0);
                }

                if (resource.GoNextSub(RESOURCE::ResTypes::SPRITE) != 0)
                    break;
            }
            return 0;
        }

        if (resource.GoBegin(RESOURCE::ResTypes::SPRI) == 0)
        {
            for (;;)
            {
                SPRITE* const sprite = map->LoadSprite(&resource, version);
                if (sprite == endSprite)
                    break;
                if (sprite)
                {
                    const float centeredX = sprite->X()
                        - static_cast<float>(shiftX)
                        - static_cast<float>(sizeX / 2)
                        + static_cast<float>(graph->SizeX()) * 0.5f;
                    const float centeredY = sprite->Y()
                        - static_cast<float>(shiftY)
                        - static_cast<float>(sizeY / 2)
                        + static_cast<float>(graph->SizeY()) * 0.5f;
                    sprite->ChangeCoor(centeredX, centeredY, sprite->Z());
                }
            }
            return 0;
        }

        LOG::ResourceError("MENU", 11, "'SPR ' or 'SPRI' in menu", 0, 0);
        return 1;
    }


    SPRITE* MENU::SpriteWithName(const STRING& name) const noexcept
    {
        const char* const wanted = name.c_str();
        for (int i = 0; i < m_count; ++i)
        {
            SPRITE* const item = m_items[i];
            if (!item)
                continue;


            const char* const source = item->m_exData
                ? item->m_exData->name.c_str()
                : STRING::SharedEmptyText();
            STRING candidate(source);
            const unsigned char* left = reinterpret_cast<const unsigned char*>(candidate.c_str());
            const unsigned char* right = reinterpret_cast<const unsigned char*>(wanted);
            while (*left == *right)
            {
                if (*left == 0u)
                    return item;
                ++left;
                ++right;
            }
        }
        return nullptr;
    }


    int MENU::DeleteFromFile(const STRING& path)
    {
        RESOURCE resource;
        if (resource.openFile(&path, RESOURCE::ResTypes::MENU) != 0)
        {
            LOG::ResourceError("MENU", 7, path.c_str(), 0, 0);
            return 1;
        }

        if (resource.GoBegin(RESOURCE::ResTypes::HEAD) != 0)
        {
            LOG::ResourceError("MENU", 11, "'HEAD'in menu", 0, 0);
            return 1;
        }

        int version = 0;
        int sizeX = 0;
        int sizeY = 0;
        int shiftX = 0;
        int shiftY = 0;
        resource.read(&version, 4);
        resource.read(&sizeX, 4);
        resource.read(&sizeY, 4);
        resource.read(&shiftX, 4);
        resource.read(&shiftY, 4);
        (void)version;

        GRAPH* const graph = Graph;
        if (!graph)
            return 1;

        if (resource.GoNext(RESOURCE::ResTypes::SPRITE) != 0)
        {
            LOG::ResourceError("MENU", 11, "'SPR ' in MENU::DeleteFromFile", 0, 0);
            return 1;
        }

        constexpr double kPositionEpsilon = 0.001;
        const float menuOffsetX = static_cast<float>(graph->SizeX()) * 0.5f
            - static_cast<float>(shiftX + sizeX / 2);
        const float menuOffsetY = static_cast<float>(graph->SizeY()) * 0.5f
            - static_cast<float>(shiftY + sizeY / 2);
        const core::ApplicationDrawDispatcherState& drawState =
            core::GlobalApplicationDrawDispatcherState();

        for (;;)
        {
            int oldAddress = 0;
            resource.read(&oldAddress, 4);
            if (oldAddress == -1)
                break;

            int nvid = -1;
            float authoredX = 0.0f;
            float authoredY = 0.0f;
            float authoredZ = 0.0f;
            resource.read(&nvid, 4);
            resource.read(&authoredX, 4);
            resource.read(&authoredY, 4);
            resource.read(&authoredZ, 4);

            const float centeredAuthoredX = authoredX + menuOffsetX;
            const float centeredAuthoredY = authoredY + menuOffsetY - authoredZ;


            for (int i = 0; i < m_count; ++i)
            {
                SPRITE* const sprite = m_items[i];
                VID* const vid = sprite ? sprite->Vid() : nullptr;
                if (!sprite || !vid || vid->nvid() != nvid)
                    continue;


                const float deltaX =
                    (sprite->X() - drawState.cameraShiftX()) - centeredAuthoredX;
                const float deltaY =
                    (sprite->Y() - sprite->Z() - drawState.cameraShiftY()) - centeredAuthoredY;
                if (std::fabs(static_cast<double>(deltaX)) >= kPositionEpsilon ||
                    std::fabs(static_cast<double>(deltaY)) >= kPositionEpsilon)
                    continue;

                (void)DeleteSpriteNumber(i);
                --i;
            }

            if (resource.GoNextSub(RESOURCE::ResTypes::SPRITE) != 0)
                break;
        }
        return 0;
    }


    int MENU::Control(input::InputMessageState* input)
    {


        m_selectedSprite = nullptr;
        m_controlFlags &= ~0x3u;

        for (int index = 0; index < m_count; ++index)
        {
            SPRITE* const sprite = m_items[index];
            if (!sprite)
                continue;

            VID* const vid = sprite->Vid();
            if (vid->movementMask() == 0u)
                continue;

            const int animation = sprite->Animation();


            if (animation == 14 || animation >= 15 || animation == 9 || animation == 8)
                continue;


            const float halfX = vid->halfSizeX();
            const float baseY = sprite->Y() - sprite->Z();
            const float halfY = vid->halfSizeY();
            const bool inside =
                input->worldX >= sprite->X() - halfX &&
                sprite->X() + halfX >= input->worldX &&
                input->worldY > baseY - vid->sizeZ() - halfY &&
                baseY + halfY > input->worldY;

            if (!inside ||
                (m_selectedSprite && sprite->Z() <= m_selectedSprite->Z()))
            {
                sprite->ChangeAnimation(animation & 1);
                continue;
            }

            m_selectedSprite = sprite;
        }

        if (!m_selectedSprite)
            return 0;

        if ((input->flags & 0x1u) != 0u)
        {
            m_controlFlags |= 0x1u;
            (void)input->clearLeftButtonState();
            m_selectedSprite->ChangeAnimation((m_selectedSprite->Animation() & 1) | 4);
            return 1;
        }

        if ((input->flags & 0x4u) != 0u)
        {
            m_controlFlags |= 0x2u;
            (void)input->clearRightButtonState();
        }

        const int animation = m_selectedSprite->Animation();
        const int animationPair = animation & ~1;
        if (animationPair == 4 || animationPair == 2 || animationPair == 6)
            return 0;

        VID* const selectedVid = m_selectedSprite->Vid();
        const int transitionAnimation = (animation & 1) | 6;
        if (selectedVid->declaredAnimationFrameCount(7) != 0 ||
            selectedVid->declaredAnimationFrameCount(6) != 0)
        {
            m_selectedSprite->ChangeAnimation(transitionAnimation);
            return 0;
        }

        (void)m_selectedSprite->CreateChildFor(transitionAnimation, nullptr);
        m_selectedSprite->ChangeAnimation((m_selectedSprite->Animation() & 1) | 2);
        return 0;
    }


    int MENU::NVidUnderCursor() const noexcept
    {

        const SPRITE* selected = m_selectedSprite;
        if (!selected)
            return 0;

        const VID* vid = selected->Vid();
        return vid->nvid();
    }


    int MENU::NDirUnderCursor() const noexcept
    {

        const SPRITE* selected = m_selectedSprite;
        if (!selected)
            return 0;

        VID* const vid = selected->Vid();
        return vid->RealDirection(ANGLE(static_cast<unsigned char>(selected->directionIndex())));
    }


}
