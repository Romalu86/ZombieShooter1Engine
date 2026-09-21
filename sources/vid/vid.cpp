#include "vid.h"
#include "vid_software.h"
#include "vid_software16.h"
#include "../core/resource.h"
#include "../core/log.h"
#include "../core/file_logger.h"
#include "../map.h"
#include "../constant.h"
#include "../sprite.h"
#include "../mouse.h"
#include "../graph.h"
#include "../core/application.h"
#include "../sound/sound_engine.h"
#include "../script/vid_data_codes.h"
#include <algorithm>
#include <stdexcept>
#include <cstring>
#include <cstdlib>
#include <memory>
#include <array>
#include <new>
#include <cmath>
#include <limits>
#include <cstdint>

namespace as1
{


    void VID::AddVidToVid(SPRITE*) {}
    void VID::Draw(const SPRITE*) {}
    int VID::DrawShadow(const SPRITE*) const { return 0; }
    void VID::DrawToVid(SPRITE*, void*, BASE_TEXTURE*, BASE_TEXTURE*) {}
    void VID::Load(RESOURCE*) {}
    void VID::SetReColorForArmy(int) {}
    int VID::HaveShadow() const { return 0; }


    int g_vidMemoryInUse = 0;


    VID* EmptyVid = new VID;

    namespace
    {

        constexpr float UNLIMITED = 999999.0f;


        template <class T>
        void readExact(RESOURCE* res, T& value, const char* field)
        {
            if (res->read(&value, sizeof(value)) != 0)
                throw std::runtime_error(std::string("VID::LoadParameters: failed to read ") + field);
        }

        void readIntArray(RESOURCE* res, int* dst, int count, const char* field)
        {
            for (int i = 0; i < count; ++i)
                readExact(res, dst[i], field);
        }

        std::int32_t multiplyWrap32(std::int32_t lhs, std::int32_t rhs) noexcept
        {
            return static_cast<std::int32_t>(
                static_cast<std::uint32_t>(lhs) * static_cast<std::uint32_t>(rhs));
        }

        std::int32_t addWrap32(std::int32_t lhs, std::int32_t rhs) noexcept
        {
            return static_cast<std::int32_t>(
                static_cast<std::uint32_t>(lhs) + static_cast<std::uint32_t>(rhs));
        }

        int signedScaleShift(int numerator, int shift) noexcept
        {
            const int mask = (1 << shift) - 1;
            if (numerator < 0)
                numerator += mask;
            return numerator >> shift;
        }

        int signedDivide32(std::int32_t numerator, std::int32_t denominator) noexcept
        {
            if (denominator == 0)
                std::abort();
            return numerator / denominator;
        }

        int scaleHpBySixteenth(int hp, int nextMaxHp, int oldMaxHp) noexcept
        {
            const std::uint32_t product = static_cast<std::uint32_t>(multiplyWrap32(hp, nextMaxHp));
            const std::int32_t shifted = static_cast<std::int32_t>(product << 4u);
            return signedScaleShift(signedDivide32(shifted, oldMaxHp), 4);
        }

        int scaleHpByByteFraction(int hp, int nextMaxHp, int oldMaxHp) noexcept
        {
            const std::uint32_t product = static_cast<std::uint32_t>(multiplyWrap32(hp, nextMaxHp));
            const std::int32_t shifted = static_cast<std::int32_t>(product << 8u);
            return signedScaleShift(signedDivide32(shifted, oldMaxHp), 8);
        }

        int percentOfBaseHp(int maxHp, int percentValue) noexcept
        {
            return multiplyWrap32(maxHp, percentValue) / 100;
        }

        int divideBy1000SignedMagic(std::int32_t value) noexcept
        {

            const std::int64_t product = static_cast<std::int64_t>(0x10624DD3) * static_cast<std::int64_t>(value);
            std::int32_t high = static_cast<std::int32_t>(product >> 32);
            std::int32_t result = high >> 6;
            result += static_cast<std::uint32_t>(result) >> 31;
            return result;
        }


    }


    float VID::calculateMoveUpZ(float verticalDelta, float projectedXYLength) const noexcept
    {

        const DWORD propertyFlags = properties();
        float result = 0.0f;

        if ((propertyFlags & 0x00000002u) != 0)
        {
            const CONSTANT* const constants = g_baseConstants;
            float c08 = 0.0f;
            std::memcpy(&c08, &constants->raw[2], sizeof(c08));
            result = projectedXYLength * c08 / maxSpeedValue() * 0.5f + verticalDelta * maxSpeedValue() / projectedXYLength;
            result *= (result > 0.0f) ? 1.1f : 0.89999998f;
        }
        else if ((propertyFlags & 0x00000004u) != 0)
        {
            const CONSTANT* const constants = g_baseConstants;
            float c0C = 0.0f;
            std::memcpy(&c0C, &constants->raw[3], sizeof(c0C));
            result = projectedXYLength * c0C / maxSpeedValue() * 0.5f + verticalDelta * maxSpeedValue() / projectedXYLength;
            result *= (result > 0.0f) ? 1.1f : 0.89999998f;
        }
        else if ((propertyFlags & 0x08000000u) != 0)
        {
            result = maximumZSpeed();
        }
        else
        {
            const float topZGate = topZValue();

            if (topZGate == 0.0f || std::isnan(topZGate))
                result = verticalDelta * maxSpeedValue() / projectedXYLength;
            else
                result = 0.0f;
        }


        if (result > maximumZSpeed())
            result = maximumZSpeed();
        else
        {
            const float negativeMaxZSpeed = -maximumZSpeed();

            if (result < negativeMaxZSpeed || std::isnan(result))
                result = negativeMaxZSpeed;
        }
        return result;
    }


    VID::VID()
    {


        gammaRaw = Gamma{};
        for (Gamma& gamma : altGammaRaw)
            gamma = Gamma{};
        scaleXYZ = VECTOR{1.0f, 1.0f, 1.0f};


        reinterpret_cast<unsigned char*>(this)[0x488] &= 0x80u;

        spriteClass = 6;
        spriteType = 0;
        property = 0;
        sizeXYZ = VECTOR{24.0f, 16.0f, 20.0f};
        halfSizeXY = VECTOR2{12.0f, 8.0f};
        nextMirror = this;
        type = 0;
        exchangedVid = this;
        layer = 19;
        nVid = -1;
        maxHp = 0;
        noDir = 1;
        directionQuantizationOffsetValue = 0;
        frameSpeedDefault = 71;
        nLinkVid = 0;
        linkVid = nullptr;
        nWeapon = 0;
        weapon = nullptr;
        noGridZ = 0;
        gridZ = nullptr;
        gridCadrShift = nullptr;
        actionAuxStateRequiredValue = 0;
        movementTactEnabledValue = 0;
        notCreateAsChildFlag = 0;

        for (int i = 0; i < NO_ANIMATION; ++i)
        {
            sfx[i] = 0;
            nChildVid[i] = 0;
            childVid[i] = nullptr;
            noAnimCadr[i] = 0;
            animationBaseFrame[i] = 0;
            animationFrameCount[i] = 0;
            frameSpeed[i] = 71;
        }

        ResetSprites();
    }


    VID* VID::CreateMirror()
    {

        return new (std::nothrow) VID();
    }


    VID::~VID()
    {

        const DWORD liveSpriteSum = NoSprites();
        if (liveSpriteSum != 0)
            logVidResourceError(10, "Not all sprites with this VID deleted", static_cast<int>(liveSpriteSum));

        VID* const mirrorNext = nextMirrorVid();
        if (mirrorNext != this)
        {
            VID* mirrorPrevious = mirrorNext;
            while (mirrorPrevious->nextMirror != this)
                mirrorPrevious = mirrorPrevious->nextMirror;
            mirrorPrevious->nextMirror = mirrorNext;
        }

        if (gridZ)
        {

            ::operator delete(gridZ);
            gridZ = nullptr;
        }
        if (gridCadrShift)
        {
            ::operator delete(gridCadrShift);
            gridCadrShift = nullptr;
        }
        noGridZ = 0;

    }


    int VID::CanFight() const noexcept
    {

        return (hasWeaponChildDescriptor() != 0u && weaponCount() != 0u) ? 1 : 0;
    }



    int VID::PropNotCreateAsChild() const noexcept
    {

        return notCreateAsChildFlag;
    }


    int VID::SetPropNotCreateAsChild(int value) noexcept
    {

        notCreateAsChildFlag = value;
        return notCreateAsChildFlag;
    }


    int VID::PropBirthAsSmoke() const noexcept
    {

        return static_cast<int>(properties() & P_BIRTHASSMOKE);
    }


    int VID::PropHide() const noexcept
    {
        return static_cast<int>((vidRuntimeFlags >> 6u) & 1u);
    }




    DWORD VID::NoSprites(int army) const
    {
        return spriteCountsByArmy[army];
    }



    int VID::recolorUnitCounterValue(int bucket) const noexcept
    {
        return recolorUnitCounters[bucket & 3];
    }



    void VID::ResetSprites() noexcept
    {


        reinterpret_cast<unsigned char*>(this)[0x488] &= 0xEFu;
        std::fill(std::begin(scriptFunction), std::end(scriptFunction), -1);
        unitLimits.fill(-1);
        std::fill(std::begin(spriteCountsByArmy), std::end(spriteCountsByArmy), 0u);
        std::fill(std::begin(killedUnitCounters), std::end(killedUnitCounters), 0);
        std::fill(std::begin(recolorUnitCounters), std::end(recolorUnitCounters), 0);
        std::fill(std::begin(maxHpByArmy), std::end(maxHpByArmy), maxHp);
        lastSpriteCountChangeTimestampMs = 0;
    }


    int VID::GetFireDamage() const noexcept
    {

        std::int32_t linkContribution = 0;
        if (const VID* link = linkedVid())
            linkContribution = static_cast<std::int32_t>(link->GetFireDamage());

        std::int32_t deathChildContribution = 0;
        if (const VID* deathChild = deathChildVid())
            deathChildContribution = multiplyWrap32(
                static_cast<std::int32_t>(deathChild->GetFireDamage()),
                static_cast<std::int32_t>(deathNoChildValue()));

        std::int32_t birthChildContribution = 0;
        if (const VID* birthChild = birthChildVid())
            birthChildContribution = multiplyWrap32(
                static_cast<std::int32_t>(birthChild->GetFireDamage()),
                static_cast<std::int32_t>(birthNoChildValue()));

        std::int32_t fightChildContribution = 0;
        if (const VID* fightChild = fightChildVid())
            fightChildContribution = multiplyWrap32(
                static_cast<std::int32_t>(fightChild->GetFireDamage()),
                static_cast<std::int32_t>(fightNoChildValue()));

        std::int32_t contribution = static_cast<std::int32_t>(deathDamageMinimumRawBits());
        contribution = addWrap32(contribution, fightChildContribution);
        contribution = addWrap32(contribution, birthChildContribution);
        contribution = addWrap32(contribution, deathChildContribution);
        contribution = addWrap32(contribution, linkContribution);
        return contribution;
    }


    int VID::GetBuildTime() const noexcept
    {
        const VID* owner = this;
        if (const VID* link = linkedVid())
        {
            if (link->weaponCount() != 0u)
                owner = link;
        }


        const WEAPON* const weapon = owner->weaponRecord();
        std::int32_t value = 0;
        std::memcpy(&value, weapon->raw.data() + 0x34, sizeof(value));
        return divideBy1000SignedMagic(value);
    }


    void VID::SetPropHide(int enabled) noexcept
    {
        constexpr unsigned int mask = 0x00000040u;
        const unsigned int bit = enabled != 0 ? mask : 0u;
        for (VID* vid = this; vid; vid = vid->linkedVid())
            vid->vidRuntimeFlags = (vid->vidRuntimeFlags & ~mask) | bit;
    }


    void VID::SetHpCoeff(int army, int hpPercent) noexcept
    {

        const int bucket = army & 3;
        for (VID* vid = this; vid; vid = vid->linkedVid())
        {
            const int oldMaxHp = vid->GetMaxHp(bucket);
            if (hpPercent >= 0)
                vid->maxHpByArmy[bucket] = percentOfBaseHp(vid->maxHp, hpPercent);

            if (vid->maxHp == 0)
                continue;

            const int layer = vid->renderLayer();
            const core::ApplicationDrawPassBucket& passBucket =
                core::GlobalApplicationDrawDispatcherState().drawPassBucket(layer);
            int cursor = passBucket.count() - 1;
            if (cursor < 0)
                continue;

            SPRITE* sprite = nullptr;
            for (;;)
            {
                SPRITE* const* slots = passBucket.data();
                while (cursor >= 0 && slots[cursor] == nullptr)
                    --cursor;
                if (cursor < 0)
                    break;

                sprite = slots[cursor];
                if (sprite->Vid() == vid)
                {
                    if (static_cast<int>(sprite->armyIndex()) == bucket)
                    {
                        const int newMaxHp = vid->GetMaxHp(bucket);
                        const int hp = scaleHpBySixteenth(
                            sprite->Hp(), newMaxHp, oldMaxHp);
                        sprite->ChangeHp(hp);
                    }
                }

                --cursor;
                if (cursor < 0)
                    break;
            }
        }
    }


    void VID::SetMaxHp(int army, int newHp) noexcept
    {

        const int bucket = army & 3;
        for (VID* vid = this; vid; vid = vid->linkedVid())
        {
            const int oldMaxHp = vid->GetMaxHp(bucket);
            if (newHp >= 0)
                vid->maxHpByArmy[bucket] = newHp;

            if (vid->maxHp == 0)
                continue;

            const int layer = vid->renderLayer();
            const core::ApplicationDrawPassBucket& passBucket =
                core::GlobalApplicationDrawDispatcherState().drawPassBucket(layer);
            int cursor = passBucket.count() - 1;
            if (cursor < 0)
                continue;

            for (;;)
            {
                SPRITE* const* slots = passBucket.data();
                while (cursor >= 0 && slots[cursor] == nullptr)
                    --cursor;
                if (cursor < 0)
                    break;
                SPRITE* const sprite = slots[cursor];
                if (sprite->Vid() == vid &&
                    sprite->armyIndex() == bucket)
                {
                    const int newMaxHp = vid->GetMaxHp(bucket);
                    const int hp = scaleHpByByteFraction(
                        sprite->Hp(), newMaxHp, oldMaxHp);
                    sprite->ChangeHp(hp);
                }
                --cursor;
                if (cursor < 0)
                    break;
            }
        }
    }


    void VID::loadBasicParameters(RESOURCE* globalRes)
    {
        if (!globalRes)
            return;

        short sx = 0;
        short sy = 0;
        (void)globalRes->read(&frameSpeedDefault, 2);
        (void)globalRes->read(&noCadr, 2);
        (void)globalRes->read(&sx, 2);
        (void)globalRes->read(&sy, 2);
        setVidWidth(sx);
        setVidHeight(sy);
    }


    void VID::LoadParameters(RESOURCE* res)
    {


        BYTE* const raw = reinterpret_cast<BYTE*>(this);
        for (std::size_t off = 0x0Cu; off <= 0x60u; off += 4u)
            (void)res->read(raw + off, 4u);
        (void)res->read(raw + 0x68u, 4u);
        (void)res->read(raw + 0x6Cu, 4u);
        (void)res->read(raw + 0x70u, 4u);
        (void)res->read(raw + 0x74u, 4u);

        res->shiftCurrentUnchecked(16);
        (void)res->read(&noDir, 4);

        (void)res->read(noAnimCadr, sizeof(noAnimCadr));
        (void)res->read(sfx, sizeof(sfx));
        (void)res->read(frameSpeed, sizeof(frameSpeed));
        (void)res->read(childX, sizeof(childX));
        (void)res->read(childY, sizeof(childY));
        (void)res->read(childZ, sizeof(childZ));
        (void)res->read(nChildVid, sizeof(nChildVid));
        (void)res->read(noChild, sizeof(noChild));

        int red = 0;
        int green = 0;
        int blue = 0;
        int alpha = 0;
        (void)res->read(&red, 4);
        (void)res->read(&green, 4);
        (void)res->read(&blue, 4);
        (void)res->read(&alpha, 4);

        gammaRaw = Gamma(alpha, red, green, blue);


        for (Gamma& armyGamma : altGammaRaw)
            armyGamma = gammaRaw;

        (void)res->read(&scaleXYZ.x, 4);
        (void)res->read(&scaleXYZ.y, 4);
        (void)res->read(&scaleXYZ.z, 4);

        if ((formatFlags() & VID_TYPE_ZBUFFER) != 0u &&
            (formatFlags() & VID_TYPE_HARDWARE) != 0u)
            scaleXYZ = VECTOR{1.0f, 1.0f, 1.0f};

        if (rotationSpeed == UNLIMITED)
            rotationSpeed = 0.0f;
        else if (rotationSpeed == 0.0f)
            rotationSpeed = UNLIMITED;
        else
            rotationSpeed = 256.0f / rotationSpeed;

        if (maxSpeed != UNLIMITED)
            maxSpeed /= 1000.0f;
        if (randomSpeed != UNLIMITED)
            randomSpeed /= 1000.0f;
        if (maxZSpeed != UNLIMITED)
            maxZSpeed /= 1000.0f;
        if (randomZSpeed != UNLIMITED)
            randomZSpeed /= 1000.0f;
        if (acceleration != UNLIMITED)
            acceleration /= 1000000.0f;
        if (slow != UNLIMITED)
            slow /= 1000000.0f;

        setMovementTactEnabled(
            (maxSpeed != 0.0f || randomSpeed != 0.0f ||
             maxZSpeed != 0.0f || randomZSpeed != 0.0f ||
             (property & 0x00001006u) != 0u) ? 1 : 0);

        if (noDir == 0)
        {
            logVidResourceError(4, "NoDir==0", 0);
            std::exit(1);
        }
        for (int i = 0; i < NO_ANIMATION; ++i)
            if (frameSpeed[i] == 0)
                frameSpeed[i] = static_cast<int>(frameSpeedDefault);

        halfSizeXY = { sizeXYZ.x * 0.5f, sizeXYZ.y * 0.5f };
        setDirectionQuantizationOffset(128 / noDir);


        for (int& armyMaxHp : maxHpByArmy)
            armyMaxHp = maxHp;

        if (noCadr != 0)
        {
            if (noCadr < noDir)
            {
                logVidResourceError(4, "noCadr < noDir", 0);
                noDir = noCadr;
            }
        }
        else
        {
            logVidResourceError(4, "noCadr==0", 0);
        }

        const int noCadrCount = static_cast<int>(static_cast<std::uint16_t>(noCadr));

        int requestedFrames = 0;
        for (int i = 0; i < NO_ANIMATION; ++i)
            requestedFrames += static_cast<int>(noAnimCadr[i]) * static_cast<int>(noDir);

        if (requestedFrames > noCadrCount)
        {
            logVidResourceError(13, "noCadr for noAnimCadr and noDir", 0);
            for (int i = NO_ANIMATION - 1; i >= 0 && requestedFrames > noCadrCount; --i)
            {
                const int currentAnimFrames = static_cast<int>(noAnimCadr[i]) * static_cast<int>(noDir);
                const int withoutCurrent = requestedFrames - currentAnimFrames;
                if (withoutCurrent > noCadrCount)
                {
                    noAnimCadr[i] = 0;
                    requestedFrames = withoutCurrent;
                    continue;
                }

                const int overrun = requestedFrames - noCadrCount;
                noAnimCadr[i] -= overrun / noDir;
                break;
            }
        }

        const std::uint8_t* const soundOwner =
            reinterpret_cast<const std::uint8_t*>(sound::g_globalSoundEngine);
        const std::uint8_t* const soundTable =
            *reinterpret_cast<const std::uint8_t* const*>(soundOwner + 0x08u);
        if (soundTable != nullptr)
        {
            const int loadedSfxCount =
                *reinterpret_cast<const int*>(soundOwner + 0x04u);
            for (int i = 0; i < NO_ANIMATION; ++i)
            {
                const int nsfx = sfx[i];
                bool sfxLoaded = false;
                if (nsfx >= 0 && nsfx <= loadedSfxCount)
                {


                    constexpr std::size_t kSfxEntrySize = 0x70u;
                    constexpr std::size_t kLoadedFileCountOffset = 0x6Cu;
                    const std::uint8_t* const entry =
                        soundTable + static_cast<std::size_t>(nsfx) * kSfxEntrySize;
                    sfxLoaded = *reinterpret_cast<const int*>(
                        entry + kLoadedFileCountOffset) != 0;
                }

                if (!sfxLoaded && nVid != -1)
                {
                    logVidResourceError(4, "sfx", nsfx);
                    sfx[i] = 0;
                }
            }
        }

        int totalCadr = 0;
        int firstRealAnim = -1;
        for (int i = 0; i < NO_ANIMATION; ++i)
        {
            const int animCadr = static_cast<int>(noAnimCadr[i]);
            if (animCadr != 0)
            {
                animationBaseFrame[i] = totalCadr;
                animationFrameCount[i] = animCadr;

                if (firstRealAnim < 0)
                {
                    firstRealAnim = i;
                    for (int j = 0; j < i; ++j)
                    {
                        if (animationFrameCount[j] == 0)
                            animationFrameCount[j] = animCadr;
                    }
                }
            }
            else
            {
                bool copiedClass10OddFallback = false;
                if (spriteClass == 0x0Au && (i & 1) != 0 && i <= 9 &&
                    noAnimCadr[firstRealAnim + 1] != 0)
                {

                    animationBaseFrame[i] = animationBaseFrame[firstRealAnim + 1];
                    animationFrameCount[i] = animationFrameCount[firstRealAnim + 1];
                    copiedClass10OddFallback = true;
                }

                if (!copiedClass10OddFallback)
                {
                    animationBaseFrame[i] = 0;
                    animationFrameCount[i] = animationFrameCount[firstRealAnim];
                }
            }

            totalCadr += animCadr * static_cast<int>(noDir);
            if (totalCadr > noCadrCount)
            {
                logVidResourceError(10, "noCadr and noAnimCadr and noDir", i);
                animationBaseFrame[i] = 0;
                animationFrameCount[i] = animationFrameCount[firstRealAnim];
            }
        }

        if (spriteClass == 8)
        {
            vidRuntimeFlags |= 0x20u;
            const std::uint8_t* const appOwner =
                static_cast<const std::uint8_t*>(core::ApplicationOwner());
            const DWORD appFlags = *reinterpret_cast<const DWORD*>(
                appOwner + core::application_layout::Flags);
            if ((appFlags & 0x00000001u) != 0u)
                spriteClass = 0;
        }


        if ((property & 0x00000008u) != 0u && noGridZ == 0)
        {
            const int allocationExtent = static_cast<int>(sizeXYZ.x) + 17;
            const int allocationRows = allocationExtent / 8;
            const int scratchCount = (allocationRows * allocationExtent) / 8 + 1;
            const std::size_t scratchBytes =
                static_cast<std::size_t>(scratchCount) * sizeof(VECTOR);
            VECTOR* const scratch =
                static_cast<VECTOR*>(::operator new[](scratchBytes));

            noGridZ = 0;
            for (int y = 0; static_cast<float>(y) < sizeXYZ.y; y += 8)
            {
                for (int x = 0; static_cast<float>(x) < sizeXYZ.x; x += 8)
                {
                    VECTOR& dot = scratch[noGridZ++];
                    dot.x = static_cast<float>(x) - halfSizeXY.x;
                    dot.y = static_cast<float>(y) - halfSizeXY.y;
                    dot.z = sizeXYZ.z;
                }

                VECTOR& rightDot = scratch[noGridZ++];
                rightDot.x = halfSizeXY.x;
                rightDot.y = static_cast<float>(y) - halfSizeXY.y;
                rightDot.z = sizeXYZ.z;
            }

            for (int x = 0; static_cast<float>(x) < sizeXYZ.x; x += 8)
            {
                VECTOR& bottomDot = scratch[noGridZ++];
                bottomDot.x = static_cast<float>(x) - halfSizeXY.x;
                bottomDot.y = halfSizeXY.y;
                bottomDot.z = sizeXYZ.z;
            }

            VECTOR& corner = scratch[noGridZ++];
            corner.x = halfSizeXY.x;
            corner.y = halfSizeXY.y;
            corner.z = sizeXYZ.z;

            if (gridZ)
                ::operator delete[](gridZ);

            const std::size_t finalBytes =
                static_cast<std::size_t>(noGridZ) * sizeof(VECTOR);
            gridZ = static_cast<VECTOR*>(::operator new(finalBytes));
            for (int i = 0; i < noGridZ; ++i)
                gridZ[i] = scratch[i];

            ::operator delete[](scratch);
        }
    }


    std::intptr_t VID::logVidResourceError(int errorCode, const char* detailText, int detailValue) const
    {

        return logFileLoggerResourceError(
            g_fileLogger, "VID [%i-%s]", errorCode, detailText, detailValue, nVid, name.c_str());
    }


    void VID::SetChildAndLink()
    {
        std::uint8_t* const appOwner =
            static_cast<std::uint8_t*>(core::ApplicationOwner());
        const int vidCount =
            *reinterpret_cast<const int*>(
                appOwner + core::application_layout::VidCount);
        VID* const* const vidTable =
            reinterpret_cast<VID* const*>(
                appOwner + core::application_layout::VidTable);

        const int linkedVidId = nLinkVid;
        if (linkedVidId)
        {
            if (linkedVidId >= 0 && linkedVidId < vidCount && vidTable[linkedVidId])
                linkVid = vidTable[linkedVidId]->exchangedVid;
            else
                logVidResourceError(4, "LinkVid", linkedVidId);
        }


        int chainedLinkIndex = nLinkVid;
        while (chainedLinkIndex >= 0 &&
               chainedLinkIndex < vidCount &&
               vidTable[chainedLinkIndex] &&
               chainedLinkIndex != 0)
        {
            chainedLinkIndex = vidTable[chainedLinkIndex]->nLinkVid;
        }


        const std::uint8_t* const ex = weapon->raw.data();
        for (int i = 0; i < 8; ++i)
        {
            const std::size_t step = static_cast<std::size_t>(i) * 4u;
            if (*reinterpret_cast<const std::uint32_t*>(ex + 0x080u + step) != 0u ||
                *reinterpret_cast<const std::uint32_t*>(ex + 0x0A0u + step) != 0u ||
                *reinterpret_cast<const std::uint32_t*>(ex + 0x0C0u + step) != 0u ||
                *reinterpret_cast<const std::uint32_t*>(ex + 0x0E0u + step) != 0u)
            {
                vidRuntimeFlags |= 0x01u;
            }

            if (*reinterpret_cast<const float*>(ex + 0x100u + step) != 1.0f ||
                *reinterpret_cast<const float*>(ex + 0x120u + step) != 1.0f ||
                *reinterpret_cast<const float*>(ex + 0x140u + step) != 1.0f)
            {
                vidRuntimeFlags |= 0x02u;
            }

            if (*reinterpret_cast<const float*>(ex + 0x160u + step) != 0.0f ||
                *reinterpret_cast<const float*>(ex + 0x180u + step) != 0.0f ||
                *reinterpret_cast<const float*>(ex + 0x1A0u + step) != 0.0f)
            {
                vidRuntimeFlags |= 0x04u;
            }

            if (ex[0x1C0u + static_cast<std::size_t>(i)] != 0u ||
                ex[0x1C8u + static_cast<std::size_t>(i)] != 0u ||
                ex[0x1D0u + static_cast<std::size_t>(i)] != 0u)
            {
                vidRuntimeFlags |= 0x08u;
            }
        }


        if (lifeTime != 999999 ||
            (property & 0x00200000u) != 0u ||
            maxSpeed != randomSpeed ||
            (vidRuntimeFlags & 0x0Fu) != 0u ||
            (property & 0x00080000u) != 0u ||
            (property & 0x00000028u) != 0u)
        {
            actionAuxStateRequiredValue = 1;
        }

        for (int i = 0; i < NO_ANIMATION; ++i)
        {
            const int sourceChild = nChildVid[i];
            if (!sourceChild)
                continue;

            const std::uint32_t sourceRaw = static_cast<std::uint32_t>(sourceChild);
            const std::uint32_t signMask = 0u - (sourceRaw >> 31u);
            const int childIndex = static_cast<std::int32_t>((sourceRaw ^ signMask) - signMask);
            if (childIndex >= 0 && childIndex < vidCount && vidTable[childIndex])
            {
                VID* const mirror = vidTable[childIndex]->exchangedVid;
                childVid[i] = mirror;
                if (mirror && (mirror->property & 0x00080080u) != 0u)
                    actionAuxStateRequiredValue = 1;
            }
            else
            {
                logVidResourceError(4, "child", sourceChild);
            }
        }
    }


    void VID::SetLayer()
    {

        layer = 0;
    }

    int VID::RealDirection(const ANGLE& dir) const { return noDir > 0 ? (((dir.Int() + directionQuantizationOffset()) & 255) * noDir) / 256 : 0; }

    ANGLE VID::SteppedDirection(const ANGLE& dir) const
    {

        if (noDir <= 1)
            return ANGLE(static_cast<unsigned char>(dir.Int()));
        const int real = RealDirection(dir);
        return ANGLE(static_cast<unsigned char>((real << 8) / noDir));
    }

    void VID::SetGamma(const Gamma& rawGamma, unsigned n_gamma)
    {

        if (n_gamma >= 4)
        {
            if (n_gamma != 4)
                logVidResourceError(4, "n_gamma in VID::SetGamma", static_cast<int>(n_gamma));
            return;
        }

        altGammaRaw[n_gamma] = rawGamma;
    }

    void VID::SetGridZ(const SPRITE* sprite)
    {


        if (!sprite || sprite == Mouse || noGridZ <= 0 || !gridZ)
            return;

        const int frame = sprite->currentFrame();
        const_cast<SPRITE*>(sprite)->setGroundGridFrame(frame);

        int begin = 0;
        int end = noGridZ;
        if (gridCadrShift)
        {
            const int frameCount = static_cast<int>(static_cast<std::uint16_t>(noCadr));
            begin = (frame >= 0 && frame < frameCount) ? gridCadrShift[frame] : 0;
            end = (frame >= 0 && frame < frameCount - 1) ? gridCadrShift[frame + 1] : noGridZ;
        }

        MAP* const map = sprite->mapOwner();
        if (!map)
            return;


        const bool permanent = sprite->Vid()->spriteClassId() == 8u;
        for (int i = begin; i < end; ++i)
        {
            const VECTOR& dot = gridZ[i];
            const float x = sprite->X() + dot.x;
            const float y = sprite->Y() + dot.y;
            const float z = sprite->Z() + dot.z;
            if (permanent)
                map->SetGroundZ(x, y, z);
            else
                map->SetTempGroundZ(x, y, z);
        }
    }

    void VID::ResetGridZ(const SPRITE* sprite)
    {

        if (!sprite || sprite == Mouse || noGridZ <= 0 || !gridZ)
            return;

        const int frame = sprite->groundGridFrame();
        if (frame < 0)
            return;
        if (sprite->Vid()->spriteClassId() == 8u)
            return;

        int begin = 0;
        int end = noGridZ;
        if (gridCadrShift)
        {
            const int frameCount = static_cast<int>(static_cast<std::uint16_t>(noCadr));
            begin = frame < frameCount ? gridCadrShift[frame] : 0;
            end = frame < frameCount - 1 ? gridCadrShift[frame + 1] : noGridZ;
        }

        MAP* const map = sprite->mapOwner();
        if (!map)
            return;
        for (int i = begin; i < end; ++i)
        {
            const VECTOR& dot = gridZ[i];
            map->ClearTempGroundZ(sprite->X() + dot.x, sprite->Y() + dot.y, sprite->Z() + dot.z);
        }
    }

}
