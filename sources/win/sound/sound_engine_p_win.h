#pragma once

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "core/resource.h"
#include "core/types.h"
#include "core/as_string.h"
#include <array>
#include <cstdint>
#include <cstddef>

namespace as1 { namespace win { namespace sound
{
    class SoundEngineWin
    {
    public:
#pragma warning(suppress : 26495)
        SoundEngineWin() = default;

        SoundEngineWin* initializeSoundState(HWND window, RESOURCE* resource, int highQuality);
        void Destroy();

        int initializationState() const noexcept { return m_initializationState; }
        int masterVolumePercent() const noexcept { return m_masterVolumePercent; }
        int musicVolumePercent() const noexcept { return m_musicVolumePercent; }
        int loadedSoundCount() const noexcept { return m_loadedSoundCount; }
        int playingSoundCount() const noexcept;
        int prepareSoundPlaybackSlot(int soundNumber);
        int stopSoundNumber(int soundNumber);
        int enqueueSoundRequest(int soundNumber, int pan, int volume);
        int enqueueSoundRequestFromCoordinates(int soundNumber, float x, float y);
        void updateMusicStreamState();
        int updateSoundRequestQueue();
        int stopMusic(int fadeTime);
        int playMusicFile(const char* path, int loop, int fadeTime);
        int pauseMusicAndStopActiveBuffers();
        int resumeMusicAndRestoreActiveBuffers();
        int applyMasterVolumePercent(int volumePercent);
        int applyMusicVolumePercent(int volumePercent);


        void* createOggSoundBufferFromFile(const as1::STRING* path, void* decodeState,
                                           void** fileOut, int requestedBufferBytes);

    private:
        struct SoundRequestSlot
        {
            std::int32_t soundNumber;
            std::int32_t owner;
            std::int32_t pan;
            std::int32_t volume;
        };

        struct Sfx
        {


            Sfx() noexcept;
            ~Sfx();
            void releaseBuffers();
            void load(const as1::STRING* names, std::uint32_t property, unsigned char priority,
                      int volumeBias, const as1::STRING* ffbNames, SoundEngineWin* soundOwner);
            void* acquirePlayableBuffer(void* directSound);

            char* names[8];
            char* ffbNames[8];
            std::uint32_t property;
            unsigned char priority;
            unsigned char reserved[3];
            int volumeBias;
            void* buffers[8];
            int loadedBufferCount;
        };

        struct SfxBuffer
        {
            __forceinline
            SfxBuffer() noexcept : buffer(nullptr), active(0), soundNumber(-1) {}
            virtual ~SfxBuffer();
            int reset();
            int start(int pan, int volume);
            int restore();
            int updateStatus();
            void assign(int soundNumber, void* buffer);

            void* buffer;
            std::int32_t active;
            std::int32_t soundNumber;
            std::int32_t volume;
            std::int32_t pan;
            std::uint32_t startTime;
        };

        enum InitializationState
        {
            Initialized = 0,
            PendingDirectSound = 1,
            DirectSoundCreateFailed = 2,
            CooperativeLevelFailed = 3,
        };

        void initializeDirectSoundDevice();
        void shutdownDirectSoundDevice();
        void stopDirectSoundAndRestoreWaveVolume();
        int closeMusicStreamAndRestoreAuxVolume();
        int pauseMusicStreamAndRestoreAuxVolume();
        int resumeMusicStreamAndRestoreAuxVolume();
        int stopActivePlayingBuffers();
        int restoreActivePlayingBuffers();
        int openMusicStreamFromOwnedPath(char* path, std::uint32_t loopToken);
        void stopSoundSystem();
        void loadSoundEffectsFromResource(RESOURCE* resource);
        int stopAllPlayingBuffers();
        void rebuildSoundBuffersAfterLoss();
        void* createWaveSoundBufferFromResourceFile(const as1::STRING* name, RESOURCE* resource, int reserved);

        static constexpr std::size_t kSoundRequestSlotCount = 32;
        static constexpr std::size_t kSfxBufferCount = 16;
        static constexpr std::uint32_t kHighQualityFlag = 0x00000001u;

        std::uint32_t m_flags;
        int m_loadedSoundCount;
        Sfx* m_soundTable;
        std::array<SoundRequestSlot, kSoundRequestSlotCount> m_soundRequestSlots;

        HWND m_window;
        void* m_directSound;
        std::array<SfxBuffer, kSfxBufferCount> m_playingSlots;

        int m_musicTrackIndex;
        int m_cdTrack0;
        int m_cdTrack1;
        int m_cdTrack2;
        int m_cdTrack3;
        int m_initializationState;
        int m_auxDeviceId;
        int m_waveOutDeviceId;
        unsigned int m_auxVolume;
        unsigned int m_waveOutVolume;
        unsigned int m_auxMusicVolume;
        int m_lastPlaybackStatus;
        int m_masterVolumePercent;
        int m_musicVolumePercent;
        std::uint32_t m_pendingMusicLoopToken;
        void* m_streamOwner;
        as1::STRING m_musicPath;
        int m_musicFadeVolume;
        int m_musicFadeTime;
    };
} } }
