#include "win/sound/sound_engine_p_win.h"
#include "sound/sound_engine.h"
#include "vorbis/vorbisfile.h"
#include "core/application.h"
#include "core/as_string.h"
#include "core/log.h"
#include "core/file_logger.h"

#include <mmsystem.h>
#include <dsound.h>
#include <dshow.h>

#include <algorithm>
#include <cstring>
#include <cstdint>
#include <cstdio>
#include <array>
#include <new>
#include <cmath>
#include <limits>
#include <xmmintrin.h>

namespace as1 { namespace win { namespace sound
{
    namespace
    {
        constexpr const char* kSoundLogSection = "SOUND";
        constexpr const char* kMusicLogContext = "MUSIC '%s'";
        constexpr const char* kDirectSoundText = "DirectSound";
        constexpr const char* kPriorityLevelText = "PriorityLevel";
        constexpr const char* kCooperativeLevelText = "CooperativeLevel";
        constexpr const char* kPrimarySoundBufferText = "Primary sound buffer";
        constexpr const char* kUnablePlayPrimaryText = "unable play Primary";
        constexpr const char* kFormatPrimaryBufferText = "format primary buffer";
        constexpr const char* kSoundBufferText = "SoundBuffer";
        constexpr const char* kSoundBufferForOggText = "SoundBuffer for ogg";
        constexpr const char* kGraphBuilderText = "GraphBuilder";
        constexpr const char* kMediaControlText = "MediaControl";
        constexpr const char* kMediaSeekingText = "MediaSeeking";
        constexpr const char* kRenderFileText = "RenderFile";
        constexpr const char* kBasicAudioText = "BasicAudio";
        constexpr const char* kVolumeText = "Volume";
        constexpr std::uint32_t kOggStreamBufferSize = 0x40000u;
        constexpr std::uint32_t kOggStreamChunkSize = 0x1000u;
        constexpr std::uint32_t kOggRingBufferMask = kOggStreamBufferSize - 1u;
        constexpr std::uint32_t kOggSoundBufferFlags = 65666u;
        constexpr std::uint16_t kOggPcmFormatTag = 1u;
        constexpr std::uint16_t kOggPcmBitsPerSample = 16u;
        constexpr std::uint16_t kOggPcmCbSize = 18u;
        constexpr const char* kDuplicateBufferErrorText = "!!!ERROR!!!SFX:'%s' %X Couldn't duplicate buffer";
        constexpr const char* kWaveFormatText = "fmt ";
        constexpr const char* kWaveDataText = "data";
        constexpr std::uint32_t kDirectSoundStaticBufferFlags = 194u;
        constexpr const char* kOggExtensions[] = {".ogg", ".OGG", ".Ogg"};
        constexpr char kStopMusicCommand[] = "stop FWMUSIC";
        constexpr char kCloseMusicCommand[] = "close FWMUSIC";
        constexpr char kOpenCdMusicCommand[] = "open cdaudio alias FWMUSIC shareable";
        constexpr char kSetCdMusicTimeFormatCommand[] = "set FWMUSIC time format tmsf";
        constexpr char kPlayCdMusicFromToNotifyFormat[] = "play FWMUSIC from %i to %i notify";
        constexpr char kPlayMusicNotifyCommand[] = "play FWMUSIC notify";
        constexpr char kPlayMusicToNotifyFormat[] = "play FWMUSIC to %i notify";


        int soundTruncateFloatToInt32(float value) noexcept
        {


            return _mm_cvtt_ss2si(_mm_set_ss(value));
        }

        int negDoubleAbs32(int value) noexcept
        {
            std::uint32_t raw = static_cast<std::uint32_t>(value);
            if (value < 0)
                raw = 0u - raw;
            raw = 0u - raw;
            raw <<= 1u;
            return static_cast<int>(raw);
        }

        int mul4Wrap32(int value) noexcept
        {
            return static_cast<int>(static_cast<std::uint32_t>(value) << 2u);
        }

        __forceinline
        char* duplicateSoundName(const char* text)
        {
            if (*text == '\0')
                return as1::STRING::SharedEmptyText();
            const std::size_t length = std::strlen(text);
            auto* copy = static_cast<char*>(::operator new(length + 1));
            std::memcpy(copy, text, length + 1);
            return copy;
        }

        void releaseSoundName(char*& text)
        {
            if (text != as1::STRING::SharedEmptyText())
                ::operator delete(text);
            text = as1::STRING::SharedEmptyText();
        }


        bool isOggFileName(const char* name)
        {
            for (const char* extension : kOggExtensions)
            {
                if (std::strstr(name, extension))
                    return true;
            }
            return false;
        }

        __forceinline
        std::intptr_t logMusicError(int errorCode, const char* detailText, int detailValue, const char* path)
        {
            return as1::logFileLoggerResourceError(g_fileLogger, kMusicLogContext, errorCode, detailText, detailValue, path);
        }

        void logSharedSoundError(int errorCode, const char* detailText, int detailValue)
        {
            as1::LOG::ResourceError(kSoundLogSection, errorCode, detailText, detailValue);
        }

        std::uint32_t soundLoadClockMilliseconds()
        {
            return static_cast<std::uint32_t>(::timeGetTime());
        }



        struct MusicStreamOwnerVTable
        {
            void* (__fastcall *destroy)(void*, void*, unsigned char);
            int (__fastcall *isPlaying)(void*, void*);
            int (__fastcall *update)(void*, void*);
            int (__fastcall *pause)(void*, void*);
            int (__fastcall *resume)(void*, void*);
            int (__fastcall *stop)(void*, void*);
            int (__fastcall *start)(void*, void*);
            int (__fastcall *setVolume)(void*, void*, int);
        };

        struct EmptyMusicStreamOwner
        {
            const MusicStreamOwnerVTable* vtable;
            char* path;
        };

        using OggDecodeState = OggVorbis_File;

        struct OggPcmInfo
        {
            int channels;
            int sampleRate;
        };

        struct OggMusicStreamOwner
        {
            explicit OggMusicStreamOwner(const as1::STRING* pathOwner);

            const MusicStreamOwnerVTable* vtable;
            char* path;
            void* buffer;
            int active;
            int endState;
            std::uint32_t reserved20;
            OggDecodeState decoderState;
            std::FILE* file;
            std::uint32_t writePosition;
        };

        struct DirectShowMusicStreamOwner
        {
            explicit DirectShowMusicStreamOwner(const as1::STRING* pathOwner);

            const MusicStreamOwnerVTable* vtable;
            char* path;
            void* graphBuilder;
            void* mediaControl;
            void* mediaSeeking;
        };

std::size_t oggReadFileCallback(void* ptr, std::size_t size, std::size_t count, void* datasource)
        {
            return std::fread(ptr, size, count, static_cast<std::FILE*>(datasource));
        }

        int oggSeekFileCallback(void* datasource, ogg_int64_t offset, int whence)
        {
            return ::_fseeki64(static_cast<std::FILE*>(datasource), static_cast<__int64>(offset), whence);
        }

        int oggNoCloseFileCallback(void*)
        {


            return 0;
        }

        long oggTellFileCallback(void* datasource)
        {
            return std::ftell(static_cast<std::FILE*>(datasource));
        }

        int openOggDecodeStateFromFile(std::FILE* file, OggDecodeState& state)
        {
            ov_callbacks callbacks{};
            callbacks.read_func = &oggReadFileCallback;
            callbacks.seek_func = &oggSeekFileCallback;
            callbacks.close_func = &oggNoCloseFileCallback;
            callbacks.tell_func = &oggTellFileCallback;
            return ov_open_callbacks(file, &state, nullptr, 0, callbacks);
        }

        void closeOggDecodeState(OggDecodeState& state)
        {
            ov_clear(&state);
        }

        int seekOggDecodeState(OggDecodeState& state, std::int64_t position)
        {
            return ov_raw_seek(&state, static_cast<ogg_int64_t>(position));
        }

        void readOggPcmInfo(OggDecodeState& state, OggPcmInfo& info)
        {

            vorbis_info* const vorbisInfo = ov_info(&state, -1);
            info.channels = vorbisInfo->channels;
            info.sampleRate = static_cast<int>(vorbisInfo->rate);
        }

        int oggPcmTotalSamples(OggDecodeState& state)
        {
            return static_cast<int>(ov_pcm_total(&state, -1));
        }

        int readOggPcmChunk(OggDecodeState& state, unsigned char* output, std::uint32_t outputSize, int& decodeFlag)
        {
            return static_cast<int>(ov_read(
                &state,
                reinterpret_cast<char*>(output),
                static_cast<int>(outputSize),
                0,
                2,
                1,
                &decodeFlag));
        }

        void* createDirectSoundOggPcmBuffer(void* directSound, const OggPcmInfo& info, std::uint32_t bufferBytes, int& resultCode)
        {
            resultCode = 0;
            WAVEFORMATEX format;
            std::memset(&format, 0, sizeof(format));
            format.wFormatTag = kOggPcmFormatTag;
            format.nChannels = static_cast<unsigned short>(info.channels);
            format.nSamplesPerSec = static_cast<DWORD>(info.sampleRate);
            format.nAvgBytesPerSec = static_cast<DWORD>(2 * info.sampleRate * info.channels);
            format.nBlockAlign = static_cast<unsigned short>(2 * info.channels);
            format.wBitsPerSample = kOggPcmBitsPerSample;
            format.cbSize = kOggPcmCbSize;

            DSBUFFERDESC description;
            std::memset(&description, 0, sizeof(description));
            description.dwSize = sizeof(description);
            description.dwFlags = kOggSoundBufferFlags;
            description.dwBufferBytes = static_cast<DWORD>(bufferBytes);
            description.lpwfxFormat = &format;

            IDirectSoundBuffer* buffer = nullptr;
            const HRESULT createResult = static_cast<IDirectSound8*>(directSound)->CreateSoundBuffer(&description, &buffer, nullptr);
            resultCode = static_cast<int>(createResult);
            return SUCCEEDED(createResult) ? buffer : nullptr;
        }

        __forceinline
        bool writeDecodedOggChunkToDirectSoundBuffer(
            IDirectSoundBuffer* buffer,
            std::uint32_t writeOffset,
            const unsigned char* decodedBlock,
            std::uint32_t decodedBytes)
        {
            void* first = nullptr;
            void* second = nullptr;
            DWORD firstSize = 0;
            DWORD secondSize = 0;
            if (FAILED(buffer->Lock(
                    static_cast<DWORD>(writeOffset),
                    static_cast<DWORD>(decodedBytes),
                    &first,
                    &firstSize,
                    &second,
                    &secondSize,
                    0)))
            {
                return false;
            }

            if (decodedBytes > firstSize)
                secondSize = decodedBytes - firstSize;
            else
            {
                firstSize = decodedBytes;
                secondSize = 0;
            }
            std::memcpy(first, decodedBlock, firstSize);
            if (secondSize != 0)
                std::memcpy(second, decodedBlock + firstSize, secondSize);
            buffer->Unlock(first, firstSize, second, secondSize);
            return true;
        }

int __fastcall emptyMusicStreamNoOp(void*, void*)
        {
            return 0;
        }

        int __fastcall emptyMusicStreamSetVolume(void*, void*, int)
        {
            return 0;
        }

        void* __fastcall emptyMusicStreamDestroy(void* object, void*, unsigned char flags)
        {
            auto* owner = static_cast<EmptyMusicStreamOwner*>(object);
            EmptyMusicStreamOwner* const self = owner;
            releaseSoundName(owner->path);
            if (flags & 1)
                ::operator delete(static_cast<void*>(self));
            return self;
        }

        int __fastcall oggMusicStreamStop(void* object, void*);
        int __fastcall oggMusicStreamUpdate(void* object, void*);

        void* __fastcall oggMusicStreamDestroy(void* object, void*, unsigned char flags)
        {
            auto* owner = static_cast<OggMusicStreamOwner*>(object);
            OggMusicStreamOwner* const self = owner;
            oggMusicStreamStop(owner, nullptr);
            if (owner->buffer)
                static_cast<IDirectSoundBuffer*>(owner->buffer)->Release();
            owner->buffer = nullptr;
            if (owner->file)
            {
                std::FILE* const file = owner->file;


                closeOggDecodeState(owner->decoderState);
                std::fclose(file);
                owner->file = nullptr;
            }
            releaseSoundName(owner->path);
            if (flags & 1)
                ::operator delete(static_cast<void*>(self));
            return self;
        }

        int __fastcall oggMusicStreamStart(void* object, void*)
        {
            auto* owner = static_cast<OggMusicStreamOwner*>(object);
            if (!owner->buffer)
                return 0;

            auto* buffer = static_cast<IDirectSoundBuffer*>(owner->buffer);
            if (owner->file)
            {
                buffer->Stop();
                std::rewind(owner->file);
                owner->writePosition = 16;
                owner->endState = 0;
                buffer->SetCurrentPosition(0);
                seekOggDecodeState(owner->decoderState, 0);
                oggMusicStreamUpdate(owner, nullptr);
                const int result = static_cast<int>(buffer->Play(0, 0, DSBPLAY_LOOPING));
                owner->active = 1;
                return result;
            }
            return static_cast<int>(reinterpret_cast<std::uintptr_t>(owner->buffer));
        }

        int __fastcall oggMusicStreamStop(void* object, void*)
        {
            auto* owner = static_cast<OggMusicStreamOwner*>(object);
            owner->active = 0;
            if (owner->buffer)
                return static_cast<int>(static_cast<IDirectSoundBuffer*>(owner->buffer)->Stop());
            return 0;
        }

        int __fastcall oggMusicStreamPause(void* object, void*)
        {
            auto* owner = static_cast<OggMusicStreamOwner*>(object);
            if (owner->active == 0 || !owner->buffer)
                return 0;
            return static_cast<int>(static_cast<IDirectSoundBuffer*>(owner->buffer)->Stop());
        }

        int __fastcall oggMusicStreamResume(void* object, void*)
        {
            auto* owner = static_cast<OggMusicStreamOwner*>(object);
            if (owner->active == 0 || !owner->buffer)
                return 0;
            return static_cast<int>(static_cast<IDirectSoundBuffer*>(owner->buffer)->Play(0, 0, DSBPLAY_LOOPING));
        }

        int __fastcall oggMusicStreamIsPlaying(void* object, void*)
        {
            auto* owner = static_cast<OggMusicStreamOwner*>(object);
            if (!owner->buffer || owner->active == 0)
                return 0;
            DWORD status = 0;
            static_cast<IDirectSoundBuffer*>(owner->buffer)->GetStatus(&status);
            if ((status & DSBSTATUS_PLAYING) == 0)
                owner->active = 0;
            return owner->active;
        }

        int __fastcall oggMusicStreamSetVolume(void* object, void*, int volume)
        {
            auto* owner = static_cast<OggMusicStreamOwner*>(object);
            if (!owner->buffer)
                return 0;
            return static_cast<int>(static_cast<IDirectSoundBuffer*>(owner->buffer)->SetVolume(static_cast<LONG>(volume)));
        }

        int decodeOggStreamChunk(OggMusicStreamOwner& owner, unsigned char* output, std::uint32_t outputSize, int& decodeFlag)
        {
            return readOggPcmChunk(owner.decoderState, output, outputSize, decodeFlag);
        }

        int __fastcall oggMusicStreamUpdate(void* object, void*)
        {
            auto* owner = static_cast<OggMusicStreamOwner*>(object);
            if (!owner->buffer)
                return 0;

            DWORD playPosition = 0;
            DWORD writeCursor = 0;
            auto* buffer = static_cast<IDirectSoundBuffer*>(owner->buffer);
            buffer->GetCurrentPosition(&playPosition, &writeCursor);

            std::array<unsigned char, kOggStreamChunkSize> decodedBlock{};
            while (owner->endState == 0)
            {
                const std::uint32_t pendingDistance = playPosition >= owner->writePosition
                    ? playPosition - owner->writePosition
                    : playPosition - owner->writePosition + kOggStreamBufferSize;
                if (pendingDistance < kOggStreamChunkSize)
                    break;

                int decodeFlag = 0;
                const int decodedBytes = decodeOggStreamChunk(
                    *owner,
                    decodedBlock.data(),
                    static_cast<std::uint32_t>(decodedBlock.size()),
                    decodeFlag);

                if (decodedBytes > 0)
                {
                    writeDecodedOggChunkToDirectSoundBuffer(
                        buffer,
                        owner->writePosition,
                        decodedBlock.data(),
                        static_cast<std::uint32_t>(decodedBytes));
                    owner->writePosition = (owner->writePosition + static_cast<std::uint32_t>(decodedBytes)) & kOggRingBufferMask;
                }
                else if (decodedBytes < 0)
                {
                    logMusicError(10, "decode", 0, owner->path);
                }
                else
                {
                    owner->endState = (owner->writePosition < playPosition) ? 2 : 1;
                }
            }

            if (owner->endState == 2 && playPosition < owner->writePosition)
                owner->endState = 1;
            if (owner->endState == 1 && playPosition >= owner->writePosition)
                return oggMusicStreamStop(owner, nullptr);
            return owner->endState;
        }

        void releaseDirectShowInterface(void*& interfacePointer)
        {
            if (interfacePointer)
            {
                static_cast<IUnknown*>(interfacePointer)->Release();
                interfacePointer = nullptr;
            }
        }

        int __fastcall directShowMusicStreamStop(void* object, void*);


        void* __fastcall directShowMusicStreamDestroy(void* object, void*, unsigned char flags)
        {
            auto* owner = static_cast<DirectShowMusicStreamOwner*>(object);
            DirectShowMusicStreamOwner* const self = owner;
            directShowMusicStreamStop(owner, nullptr);
            releaseDirectShowInterface(owner->mediaSeeking);
            releaseDirectShowInterface(owner->mediaControl);
            releaseDirectShowInterface(owner->graphBuilder);
            releaseSoundName(owner->path);
            if (flags & 1)
                ::operator delete(static_cast<void*>(self));
            return self;
        }

        int __fastcall directShowMusicStreamStart(void* object, void*)
        {
            auto* owner = static_cast<DirectShowMusicStreamOwner*>(object);

            if (owner->mediaSeeking)
            {
                LONGLONG start = 0;
                const HRESULT seekResult = static_cast<IMediaSeeking*>(owner->mediaSeeking)->SetPositions(
                    &start,
                    AM_SEEKING_AbsolutePositioning,
                    nullptr,
                    0);
                if (FAILED(seekResult) && owner->mediaControl)
                    static_cast<IMediaControl*>(owner->mediaControl)->Stop();
            }
            if (owner->mediaControl)
                return static_cast<int>(static_cast<IMediaControl*>(owner->mediaControl)->Run());
            return 0;
        }

        int __fastcall directShowMusicStreamStop(void* object, void*)
        {
            auto* owner = static_cast<DirectShowMusicStreamOwner*>(object);
            if (owner->mediaControl)
                return static_cast<int>(static_cast<IMediaControl*>(owner->mediaControl)->Stop());
            return 0;
        }

        int __fastcall directShowMusicStreamPause(void* object, void*)
        {
            auto* owner = static_cast<DirectShowMusicStreamOwner*>(object);
            if (owner->mediaControl)
                return static_cast<int>(static_cast<IMediaControl*>(owner->mediaControl)->Pause());
            return 0;
        }

        int __fastcall directShowMusicStreamResume(void* object, void*)
        {
            auto* owner = static_cast<DirectShowMusicStreamOwner*>(object);
            if (owner->mediaControl)
                return static_cast<int>(static_cast<IMediaControl*>(owner->mediaControl)->Run());
            return 0;
        }

        int __fastcall directShowMusicStreamIsPlaying(void* object, void*)
        {
            auto* owner = static_cast<DirectShowMusicStreamOwner*>(object);
            int result = 0;
            if (owner->mediaControl)
            {
                OAFilterState state = State_Stopped;
                static_cast<IMediaControl*>(owner->mediaControl)->GetState(3000, &state);
                if (state == State_Running)
                {
                    LONGLONG current = 0;
                    LONGLONG stop = 0;
                    static_cast<IMediaSeeking*>(owner->mediaSeeking)->GetPositions(&current, &stop);
                    if (current != stop)
                        result = 1;
                }
            }
            return result;
        }

        int __fastcall directShowMusicStreamSetVolume(void* object, void*, int volume)
        {
            auto* owner = static_cast<DirectShowMusicStreamOwner*>(object);
            if (!owner->graphBuilder)
                return 0;

            IBasicAudio* basicAudio = nullptr;
            const HRESULT queryResult = static_cast<IGraphBuilder*>(owner->graphBuilder)->QueryInterface(
                IID_IBasicAudio,
                reinterpret_cast<void**>(&basicAudio));
            if (FAILED(queryResult))
            {
                return static_cast<int>(logMusicError(9, kBasicAudioText, static_cast<int>(queryResult), owner->path));
            }

            long oldVolume = 0;
            const HRESULT getResult = basicAudio->get_Volume(&oldVolume);
            if (getResult != E_NOTIMPL)
            {
                if (FAILED(getResult))
                {
                    logMusicError(9, kVolumeText, static_cast<int>(getResult), owner->path);
                }
                else
                {
                    const HRESULT setResult = basicAudio->put_Volume(static_cast<long>(volume));
                    if (FAILED(setResult))
                        logMusicError(8, kVolumeText, static_cast<int>(setResult), owner->path);
                }
            }
            return static_cast<int>(basicAudio->Release());
        }

        const MusicStreamOwnerVTable kEmptyMusicStreamOwnerVTable = {
            emptyMusicStreamDestroy,
            emptyMusicStreamNoOp,
            emptyMusicStreamNoOp,
            emptyMusicStreamNoOp,
            emptyMusicStreamNoOp,
            emptyMusicStreamNoOp,
            emptyMusicStreamNoOp,
            emptyMusicStreamSetVolume,
        };

        const MusicStreamOwnerVTable kOggMusicStreamOwnerVTable = {
            oggMusicStreamDestroy,
            oggMusicStreamIsPlaying,
            oggMusicStreamUpdate,
            oggMusicStreamPause,
            oggMusicStreamResume,
            oggMusicStreamStop,
            oggMusicStreamStart,
            oggMusicStreamSetVolume,
        };

        const MusicStreamOwnerVTable kDirectShowMusicStreamOwnerVTable = {
            directShowMusicStreamDestroy,
            directShowMusicStreamIsPlaying,
            emptyMusicStreamNoOp,
            directShowMusicStreamPause,
            directShowMusicStreamResume,
            directShowMusicStreamStop,
            directShowMusicStreamStart,
            directShowMusicStreamSetVolume,
        };


        OggMusicStreamOwner::OggMusicStreamOwner(const as1::STRING* pathOwner)
        {
            path = duplicateSoundName(pathOwner->c_str());
            vtable = &kOggMusicStreamOwnerVTable;
            active = 0;
            file = nullptr;
            SoundEngineWin* const soundOwner = as1::sound::g_globalSoundEngine;
            buffer = soundOwner->createOggSoundBufferFromFile(
                pathOwner, &decoderState, reinterpret_cast<void**>(&file),
                static_cast<int>(kOggStreamBufferSize));
            if (!buffer)
                logMusicError(3, kSoundBufferText, 0, path);
        }


        DirectShowMusicStreamOwner::DirectShowMusicStreamOwner(const as1::STRING* pathOwner)
        {
            vtable = &kDirectShowMusicStreamOwnerVTable;
            path = duplicateSoundName(pathOwner->c_str());
            graphBuilder = nullptr;
            mediaControl = nullptr;
            mediaSeeking = nullptr;

            IGraphBuilder* graph = nullptr;
            HRESULT result = ::CoCreateInstance(
                CLSID_FilterGraph, nullptr, 3u, IID_IGraphBuilder,
                reinterpret_cast<void**>(&graph));
            const HRESULT graphCreateResult = result;
            graphBuilder = graph;
            if (FAILED(result))
            {
                logMusicError(3, kGraphBuilderText, static_cast<int>(result), path);
                return;
            }

            IMediaControl* control = nullptr;
            result = graph->QueryInterface(IID_IMediaControl, reinterpret_cast<void**>(&control));
            mediaControl = control;
            if (FAILED(result))
            {
                logMusicError(3, kMediaControlText, static_cast<int>(graphCreateResult), path);
                return;
            }

            IMediaSeeking* seeking = nullptr;
            result = graph->QueryInterface(IID_IMediaSeeking, reinterpret_cast<void**>(&seeking));
            mediaSeeking = seeking;
            if (FAILED(result))
            {
                logMusicError(3, kMediaSeekingText, 0, path);
                return;
            }

            std::FILE* file = nullptr;
            if (pathOwner->c_str()[0] != '\0')
                file = std::fopen(pathOwner->c_str(), "rb");
            if (file)
                std::fclose(file);
            if (!file)
            {
                logMusicError(7, pathOwner->c_str(), 0, path);
                return;
            }

            WCHAR widePath[1024];
            widePath[0] = L'\0';
            ::MultiByteToWideChar(0, 0, pathOwner->c_str(), -1, widePath, 1024);
            result = graph->RenderFile(widePath, nullptr);
            if (FAILED(result))
                logMusicError(4, kRenderFileText, static_cast<int>(result), path);
        }

        void* streamVtableSlot(void* owner, std::size_t slot)
        {
            return (*static_cast<void***>(owner))[slot];
        }

        void callStreamDestroy(void* owner)
        {
            using Call = void* (__fastcall *)(void*, void*, unsigned char);
            (void)reinterpret_cast<Call>(streamVtableSlot(owner, 0))(owner, nullptr, 1);
        }

        int callStreamInt(void* owner, std::size_t slot)
        {
            using Call = int (__fastcall *)(void*, void*);
            return reinterpret_cast<Call>(streamVtableSlot(owner, slot))(owner, nullptr);
        }

        void callStreamVoid(void* owner, std::size_t slot)
        {
            using Call = int (__fastcall *)(void*, void*);
            (void)reinterpret_cast<Call>(streamVtableSlot(owner, slot))(owner, nullptr);
        }

        int callStreamVolume(void* owner, int volume)
        {
            using Call = int (__fastcall *)(void*, void*, int);
            return reinterpret_cast<Call>(streamVtableSlot(owner, 7))(owner, nullptr, volume);
        }

    }


    SoundEngineWin::SfxBuffer::~SfxBuffer()
    {
        if (!buffer)
            return;
        const ULONG releaseResult = static_cast<IDirectSoundBuffer*>(buffer)->Release();
        if (releaseResult != 0 && as1::g_fileLogger)
        {
            as1::g_fileLogger->ResourceError(
                "SFXBUFFER[%i]",
                10,
                "SoundBuffer release !=0",
                static_cast<int>(releaseResult),
                soundNumber);
        }
        buffer = nullptr;
    }


    int SoundEngineWin::SfxBuffer::reset()
    {
        int result = static_cast<int>(static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(buffer)));
        if (buffer)
        {
            IDirectSoundBuffer* const soundBuffer = static_cast<IDirectSoundBuffer*>(buffer);
            soundBuffer->Stop();
            result = static_cast<int>(soundBuffer->Release());
        }
        buffer = nullptr;
        active = 0;
        soundNumber = -1;
        startTime = 0;
        return result;
    }


    int SoundEngineWin::SfxBuffer::start(int newPan, int newVolume)
    {
        int result = soundNumber;
        if (soundNumber < 0)
            return result;
        if (!buffer)
            return 0;

        volume = newVolume;
        pan = newPan;
        IDirectSoundBuffer* const soundBuffer = static_cast<IDirectSoundBuffer*>(buffer);
        soundBuffer->SetPan(static_cast<LONG>(newPan));
        soundBuffer->SetVolume(static_cast<LONG>(newVolume));
        if (active == 0)
        {
            SoundEngineWin* const soundOwner = as1::sound::g_globalSoundEngine;
            bool loop = false;
            if (soundOwner && soundOwner->m_soundTable && soundNumber >= 0 && soundNumber <= soundOwner->m_loadedSoundCount)
            {
                const Sfx& entry = soundOwner->m_soundTable[static_cast<std::size_t>(soundNumber)];
                loop = entry.loadedBufferCount != 0 && (entry.property & 1u) != 0u;
            }
            const DWORD playFlags = loop ? DSBPLAY_LOOPING : 0u;
            const HRESULT playResult = soundBuffer->Play(0, 0, playFlags);
            if (playResult == DSERR_BUFFERLOST)
            {
                if (as1::g_fileLogger)
                {
                    as1::g_fileLogger->ResourceError(
                        "SFXBUFFER[%i]", 10, "buffer is lost", 0, soundNumber);
                }
                soundOwner->rebuildSoundBuffersAfterLoss();
            }
        }
        active = 1;
        const std::uint32_t currentTime = as1::core::CurrentTimeMilliseconds();
        startTime = currentTime;
        return static_cast<int>(currentTime);
    }


    int SoundEngineWin::SfxBuffer::restore()
    {
        int result = soundNumber;
        if (soundNumber < 0 || !buffer || active == 0)
            return result;
        SoundEngineWin* const soundOwner = as1::sound::g_globalSoundEngine;
        bool loop = false;
        if (soundOwner && soundOwner->m_soundTable && soundNumber >= 0 && soundNumber <= soundOwner->m_loadedSoundCount)
        {
            const Sfx& entry = soundOwner->m_soundTable[static_cast<std::size_t>(soundNumber)];
            loop = entry.loadedBufferCount != 0 && (entry.property & 1u) != 0u;
        }
        const DWORD playFlags = loop ? DSBPLAY_LOOPING : 0u;
        result = static_cast<int>(static_cast<IDirectSoundBuffer*>(buffer)->Play(0, 0, playFlags));
        if (result == DSERR_BUFFERLOST)
        {
            if (as1::g_fileLogger)
                as1::g_fileLogger->WriteLine("!!!ERROR!!! SFXBUFFER::buffer is lost");
            soundOwner->rebuildSoundBuffersAfterLoss();
        }
        return result;
    }


    int SoundEngineWin::SfxBuffer::updateStatus()
    {
        if (soundNumber < 0 || !buffer || active == 0)
            return 0;
        DWORD status = 0;
        IDirectSoundBuffer* const soundBuffer = static_cast<IDirectSoundBuffer*>(buffer);
        soundBuffer->GetStatus(&status);
        if ((status & DSBSTATUS_PLAYING) == 0)
            active = 0;

        SoundEngineWin* const soundOwner = as1::sound::g_globalSoundEngine;
        bool loop = false;
        if (soundOwner && soundOwner->m_soundTable && soundNumber >= 0 && soundNumber <= soundOwner->m_loadedSoundCount)
        {
            const Sfx& entry = soundOwner->m_soundTable[static_cast<std::size_t>(soundNumber)];
            loop = entry.loadedBufferCount != 0 && (entry.property & 1u) != 0u;
        }
        if (loop && as1::core::CurrentTimeMilliseconds() - startTime > 0xC8u)
        {
            active = 0;
            soundBuffer->Stop();
        }
        return active;
    }


    void SoundEngineWin::SfxBuffer::assign(int newSoundNumber, void* newBuffer)
    {
        soundNumber = newSoundNumber;
        if (buffer)
        {
            const ULONG releaseResult = static_cast<IDirectSoundBuffer*>(buffer)->Release();
            if (releaseResult != 0 && as1::g_fileLogger)
            {
                as1::g_fileLogger->ResourceError(
                    "SFXBUFFER[%i]", 10, "SoundBuffer(Load()) release !=0",
                    static_cast<int>(releaseResult), newSoundNumber);
            }
        }
        active = 0;
        buffer = newBuffer;
        startTime = 0;
    }


    SoundEngineWin* SoundEngineWin::initializeSoundState(
        HWND window,
        RESOURCE* resource, int highQuality)
    {
        for (SoundRequestSlot& slot : m_soundRequestSlots)
            slot.soundNumber = -1;


        for (SfxBuffer& slot : m_playingSlots)
            ::new (static_cast<void*>(&slot)) SfxBuffer();

        m_pendingMusicLoopToken = 0;
        m_loadedSoundCount = 0;
        m_soundTable = nullptr;
        m_directSound = nullptr;
        m_streamOwner = nullptr;


        m_musicFadeTime = 0;
        m_cdTrack0 = -1;
        m_initializationState = PendingDirectSound;
        m_waveOutDeviceId = -1;
        m_auxDeviceId = -1;
        m_musicVolumePercent = -1;
        m_masterVolumePercent = 100;
        m_window = window;


        m_musicPath.ResetSharedEmptyWithoutRelease();

        unsigned char* const flagsByte = reinterpret_cast<unsigned char*>(&m_flags);
        *flagsByte = static_cast<unsigned char>((*flagsByte & 0xFEu) | (highQuality != 0 ? 1u : 0u));

        initializeDirectSoundDevice();
        loadSoundEffectsFromResource(resource);
        return this;
    }


    void SoundEngineWin::Destroy()
    {
        if (m_streamOwner)
            callStreamDestroy(m_streamOwner);
        m_streamOwner = nullptr;

        stopSoundSystem();
        if (m_soundTable)
            delete[] m_soundTable;
        m_soundTable = nullptr;
        m_loadedSoundCount = 0;

        m_musicPath.ReleaseOwnedStorage();
        for (SfxBuffer& slot : m_playingSlots)
            slot.~SfxBuffer();
    }



    int SoundEngineWin::playingSoundCount() const noexcept
    {
        if (m_initializationState != Initialized)
            return 0;

        int count = 0;
        for (const SfxBuffer& slot : m_playingSlots)
        {
            if (slot.active != 0)
                ++count;
        }
        return count;
    }


    int SoundEngineWin::prepareSoundPlaybackSlot(int soundNumber)
    {
        if (m_initializationState != Initialized)
            return -1;

        int requestedSoundNumber = soundNumber;
        if (!m_soundTable || soundNumber < 0 || soundNumber > m_loadedSoundCount)
        {
            logSharedSoundError(4, "nsfx", soundNumber);
            return -1;
        }

        Sfx& entry = m_soundTable[static_cast<std::size_t>(soundNumber)];
        if (entry.loadedBufferCount == 0)
        {
            logSharedSoundError(4, "nsfx", soundNumber);
            return -1;
        }

        const std::uint32_t currentTime = as1::core::CurrentTimeMilliseconds();
        for (std::size_t index = 0; index < kSfxBufferCount; ++index)
        {
            SfxBuffer& slot = m_playingSlots[index];
            if (slot.soundNumber == requestedSoundNumber)
            {
                if (currentTime - slot.startTime <= 0x28u)
                    return -1;

                if (entry.loadedBufferCount == 1 && slot.active == 0)
                    return static_cast<int>(index);
                requestedSoundNumber = soundNumber;
            }
        }

        std::size_t selectedSlot = 0;
        while (selectedSlot < kSfxBufferCount && m_playingSlots[selectedSlot].soundNumber >= 0)
            ++selectedSlot;

        if (selectedSlot >= kSfxBufferCount)
        {
            selectedSlot = 0;
            while (selectedSlot < kSfxBufferCount && m_playingSlots[selectedSlot].active != 0)
                ++selectedSlot;

            if (selectedSlot >= kSfxBufferCount)
                return -1;

            m_playingSlots[selectedSlot].reset();
        }

        void* buffer = entry.acquirePlayableBuffer(m_directSound);


        if (!buffer)
            return -1;

        m_playingSlots[selectedSlot].assign(requestedSoundNumber, buffer);
        return static_cast<int>(selectedSlot);
    }


    int SoundEngineWin::stopSoundNumber(int soundNumber)
    {
        int result = m_initializationState;
        if (m_initializationState != Initialized)
            return result;
        if (soundNumber == -1)
            return stopAllPlayingBuffers();
        for (SfxBuffer& slot : m_playingSlots)
        {
            if (slot.soundNumber == soundNumber)
                result = slot.reset();
        }
        return result;
    }


    int SoundEngineWin::enqueueSoundRequestFromCoordinates(int soundNumber, float x, float y)
    {


        if (m_initializationState != Initialized)
            return m_initializationState;

        if (!m_soundTable || soundNumber < 0 || soundNumber > m_loadedSoundCount)
            return enqueueSoundRequest(soundNumber, 0, 0);

        const Sfx& entry = m_soundTable[static_cast<std::size_t>(soundNumber)];
        if (entry.loadedBufferCount == 0)
            return enqueueSoundRequest(soundNumber, 0, 0);

        const std::uint32_t property = entry.property;
        if ((property & 0x06u) == 0x06u)
            return enqueueSoundRequest(soundNumber, 0, 0);

        const int integerX = soundTruncateFloatToInt32(x);
        const int integerY = soundTruncateFloatToInt32(y);
        const int xAttenuation = negDoubleAbs32(integerX);
        const int yAttenuation = negDoubleAbs32(integerY);
        const int attenuatedVolume = xAttenuation < yAttenuation ? xAttenuation : yAttenuation;

        const int pan = (property & 0x04u) != 0u ? 0 : mul4Wrap32(integerX);
        const int volume = (property & 0x02u) != 0u ? 0 : attenuatedVolume;
        return enqueueSoundRequest(soundNumber, pan, volume);
    }


    int SoundEngineWin::enqueueSoundRequest(int soundNumber, int pan, int volume)
    {

        int result = m_initializationState;
        if (m_initializationState != Initialized)
            return result;

        if (!m_soundTable || soundNumber < 0 || soundNumber > m_loadedSoundCount ||
            m_soundTable[static_cast<std::size_t>(soundNumber)].loadedBufferCount == 0)
        {
            return static_cast<int>(static_cast<std::uint32_t>(
                logFileLoggerResourceError(g_fileLogger, "SOUND", 4, "nsfx", soundNumber)));
        }

        const Sfx& soundEntry =
            m_soundTable[static_cast<std::size_t>(soundNumber)];
        const std::uint32_t masterBits =
            (static_cast<std::uint32_t>(m_masterVolumePercent) - 100u) << 5u;
        const int baseVolume = static_cast<int>(
            static_cast<std::uint32_t>(masterBits) +
            static_cast<std::uint32_t>(soundEntry.volumeBias));
        int queuedVolume = static_cast<int>(
            static_cast<std::uint32_t>(baseVolume) +
            static_cast<std::uint32_t>(volume));
        if (queuedVolume < -3000)
            return baseVolume;

        int queuedPan = pan;
        if (queuedPan > 10000)
            queuedPan = 10000;
        else if (queuedPan < -10000)
            queuedPan = -10000;


        if ((soundEntry.property & 0x08u) != 0u)
            queuedVolume = baseVolume;

        if (queuedVolume > 10000)
            queuedVolume = 10000;
        else if (queuedVolume < -10000)
            queuedVolume = -10000;

        for (std::size_t index = 0; index < kSoundRequestSlotCount; ++index)
        {
            SoundRequestSlot& slot = m_soundRequestSlots[index];
            if (slot.soundNumber < 0)
            {
                slot.soundNumber = soundNumber;
                slot.volume = queuedVolume;
                slot.pan = queuedPan;
                return static_cast<int>(index);
            }
        }

        int selectedSlot = 0;
        int sameSoundAlreadyQueued = 0;
        for (int index = 1; index < static_cast<int>(kSoundRequestSlotCount); ++index)
        {
            const SoundRequestSlot& slot = m_soundRequestSlots[static_cast<std::size_t>(index)];
            if (slot.soundNumber == soundNumber)
                sameSoundAlreadyQueued = 1;

            const int currentPriority = static_cast<int>(
                m_soundTable[static_cast<std::size_t>(slot.soundNumber)].priority);
            const int selectedPriority = static_cast<int>(m_soundTable[static_cast<std::size_t>(
                m_soundRequestSlots[static_cast<std::size_t>(selectedSlot)].soundNumber)].priority);
            if (currentPriority < selectedPriority)
                selectedSlot = index;
        }

        if (selectedSlot == 0 && sameSoundAlreadyQueued != 0)
            return sameSoundAlreadyQueued;

        SoundRequestSlot& slot = m_soundRequestSlots[static_cast<std::size_t>(selectedSlot)];
        slot.soundNumber = soundNumber;
        slot.volume = queuedVolume;
        slot.pan = queuedPan;
        return static_cast<int>(static_cast<std::uint32_t>(
            reinterpret_cast<std::uintptr_t>(&slot)));
    }


    void SoundEngineWin::updateMusicStreamState()
    {
        if (!m_streamOwner)
            return;

        callStreamVoid(m_streamOwner, 2);
        if (m_musicFadeTime != 0)
        {
            const std::uint32_t realTime = as1::core::RealCurrentTime;
            const std::uint32_t elapsed =
                realTime - static_cast<std::uint32_t>(m_musicFadeTime);
            const std::uint32_t duration =
                static_cast<std::uint32_t>(m_musicFadeVolume);

            if (elapsed >= duration)
            {
                m_musicFadeTime = 0;
                if (!m_musicPath.isEmpty())
                {
                    char* path = duplicateSoundName(m_musicPath.c_str());
                    openMusicStreamFromOwnedPath(path, m_pendingMusicLoopToken);
                }
                else
                {
                    stopMusic(0);
                }
            }
            else
            {


                const int fade = -3000 * static_cast<int>(elapsed) /
                    static_cast<int>(duration);
                const int baseVolume = m_musicVolumePercent >= 0
                    ? 32 * (m_musicVolumePercent - 100)
                    : 0;
                callStreamVolume(m_streamOwner, baseVolume + fade);
            }
            return;
        }

        if ((std::rand() & 0x0F) != 0 || callStreamInt(m_streamOwner, 1) != 0)
            return;

        if (!m_musicPath.isEmpty())
        {
            char* path = duplicateSoundName(m_musicPath.c_str());
            openMusicStreamFromOwnedPath(path, m_pendingMusicLoopToken);
        }
        else
        {
            stopMusic(0);
        }
    }


    int SoundEngineWin::updateSoundRequestQueue()
    {


        updateMusicStreamState();

        int result = m_initializationState;
        if (m_initializationState != Initialized)
            return result;

        int queuedRequestCount = 0;
        for (SoundRequestSlot& request : m_soundRequestSlots)
        {
            const int soundNumber = request.soundNumber;
            int matchingPlayingCount = 0;
            if (soundNumber >= 0)
            {
                for (SfxBuffer& playing : m_playingSlots)
                {
                    if (playing.active != 0 && playing.soundNumber == soundNumber)
                    {
                        if (soundNumber >= 0 && soundNumber <= m_loadedSoundCount && m_soundTable)
                        {
                            const Sfx& entry = m_soundTable[static_cast<std::size_t>(soundNumber)];
                            if (entry.loadedBufferCount != 0 && (entry.property & 0x01u) != 0u)
                            {
                                const int volumeDelta = playing.volume - request.volume;
                                if (std::abs(volumeDelta) < 500)
                                {
                                    const int panDelta = playing.pan - request.pan;
                                    if (std::abs(panDelta) < 500)
                                    {
                                        request.soundNumber = -1;
                                        playing.start(request.pan, request.volume);
                                        break;
                                    }
                                }
                            }
                        }

                        ++matchingPlayingCount;
                        const int priority = static_cast<int>(m_soundTable[static_cast<std::size_t>(soundNumber)].priority);
                        const Sfx& entry =
                            m_soundTable[static_cast<std::size_t>(soundNumber)];
                        const int allowedMatchingCount =
                            ((entry.property & 0x08u) != 0u) ? 999999 : ((priority + 10) / 10);
                        if (matchingPlayingCount > allowedMatchingCount)
                        {
                            request.soundNumber = -1;
                            break;
                        }
                    }
                }

                if (request.soundNumber >= 0)
                    ++queuedRequestCount;
            }
        }

        int freeSfxBufferCount = 0;
        for (SfxBuffer& slot : m_playingSlots)
        {
            if (!slot.updateStatus())
                ++freeSfxBufferCount;
        }

        if (queuedRequestCount > freeSfxBufferCount)
        {
            do
            {
                int foundSlot = 0;
                int selectedSlot = 0;
                if (m_soundRequestSlots[0].soundNumber < 0)
                    m_soundRequestSlots[0].volume = 0;

                for (std::size_t index = 0; index < kSoundRequestSlotCount; ++index)
                {
                    const SoundRequestSlot& slot = m_soundRequestSlots[index];
                    if (slot.soundNumber >= 0)
                    {
                        const Sfx& entry =
                            m_soundTable[static_cast<std::size_t>(slot.soundNumber)];
                        if ((entry.property & 0x08u) == 0u
                            && slot.volume <= m_soundRequestSlots[static_cast<std::size_t>(selectedSlot)].volume)
                        {
                            foundSlot = 1;
                            selectedSlot = static_cast<int>(index);
                        }
                    }
                }

                if (!foundSlot)
                    break;

                m_soundRequestSlots[static_cast<std::size_t>(selectedSlot)].soundNumber = -1;
                --queuedRequestCount;
            }
            while (queuedRequestCount > freeSfxBufferCount);
        }

        if (queuedRequestCount > freeSfxBufferCount)
        {
            for (SfxBuffer& slot : m_playingSlots)
            {
                if (queuedRequestCount <= freeSfxBufferCount)
                    break;
                if (slot.active != 0 &&
                    (m_soundTable[static_cast<std::size_t>(slot.soundNumber)].property & 0x08u) == 0u)
                {
                    slot.active = 0;
                    if (slot.buffer)
                    {
                        static_cast<IDirectSoundBuffer*>(slot.buffer)->Stop();
                    }
                    ++freeSfxBufferCount;
                }
            }
        }

        for (SoundRequestSlot& request : m_soundRequestSlots)
        {
            result = request.soundNumber;
            if (request.soundNumber >= 0)
            {
                result = prepareSoundPlaybackSlot(request.soundNumber);
                if (result >= 0)
                {
                    SfxBuffer& slot = m_playingSlots[static_cast<std::size_t>(result)];
                    result = slot.start(request.pan, request.volume);
                }
                request.soundNumber = -1;
            }
        }
        return result;
    }


    void SoundEngineWin::initializeDirectSoundDevice()
    {
        if (m_initializationState == Initialized)
            return;
        if (m_directSound)
            return;

        IDirectSound8* directSound = nullptr;
        HRESULT result = ::DirectSoundCreate8(nullptr, &directSound, nullptr);
        if (FAILED(result))
        {
            m_initializationState = DirectSoundCreateFailed;
            logSharedSoundError(3, kDirectSoundText, static_cast<int>(result));
            return;
        }

        m_directSound = directSound;

        HRESULT cooperativeResult = DS_OK;
        if (((m_flags & kHighQualityFlag) != 0u))
        {
            cooperativeResult = directSound->SetCooperativeLevel(m_window, DSSCL_PRIORITY);
            if (FAILED(cooperativeResult))
            {
                m_flags &= ~kHighQualityFlag;
                logSharedSoundError(8, kPriorityLevelText, static_cast<int>(cooperativeResult));
            }
        }

        if (!((m_flags & kHighQualityFlag) != 0u))
            cooperativeResult = directSound->SetCooperativeLevel(m_window, DSSCL_NORMAL);

        if (FAILED(cooperativeResult))
        {
            directSound->Release();
            m_directSound = nullptr;
            m_initializationState = CooperativeLevelFailed;
            logSharedSoundError(8, kCooperativeLevelText, static_cast<int>(cooperativeResult));
            return;
        }

        DSBUFFERDESC primaryDescription;
        std::memset(&primaryDescription, 0, sizeof(primaryDescription));
        primaryDescription.dwSize = sizeof(primaryDescription);
        primaryDescription.dwFlags = DSBCAPS_PRIMARYBUFFER;

        IDirectSoundBuffer* primaryBuffer = nullptr;
        result = directSound->CreateSoundBuffer(&primaryDescription, &primaryBuffer, nullptr);
        if (FAILED(result))
        {
            logSharedSoundError(3, kPrimarySoundBufferText, static_cast<int>(result));
        }
        else
        {
            result = primaryBuffer->Play(0, 0, DSBPLAY_LOOPING);
            if (FAILED(result))
                logSharedSoundError(4, kUnablePlayPrimaryText, 0);

            if (((m_flags & kHighQualityFlag) != 0u))
            {
                WAVEFORMATEX format;
                std::memset(&format, 0, sizeof(format));
                format.wFormatTag = WAVE_FORMAT_PCM;
                format.nChannels = 2;
                format.nSamplesPerSec = 44100;
                format.nAvgBytesPerSec = 176400;
                format.nBlockAlign = 4;
                format.wBitsPerSample = 16;
                format.cbSize = 0;

                result = primaryBuffer->SetFormat(&format);
                if (FAILED(result))
                    logSharedSoundError(8, kFormatPrimaryBufferText, static_cast<int>(result));
            }

            primaryBuffer->Release();
        }

        m_initializationState = Initialized;
        rebuildSoundBuffersAfterLoss();
    }


    void SoundEngineWin::shutdownDirectSoundDevice()
    {
        if (m_initializationState != Initialized)
            return;


        for (SfxBuffer& slot : m_playingSlots)
            slot.reset();
        if (m_soundTable)
        {
            for (int i = 0; i < m_loadedSoundCount; ++i)
                m_soundTable[i].releaseBuffers();
        }

        const ULONG releaseResult = static_cast<IDirectSound8*>(m_directSound)->Release();
        if (releaseResult != 0 && as1::g_fileLogger)
            as1::g_fileLogger->ResourceError("SOUND", 10, "DirectSound release !=0", static_cast<int>(releaseResult));
        m_directSound = nullptr;

        for (SoundRequestSlot& request : m_soundRequestSlots)
            request.soundNumber = -1;
        m_initializationState = PendingDirectSound;
    }


    void SoundEngineWin::stopDirectSoundAndRestoreWaveVolume()
    {
        if (m_initializationState == Initialized)
        {
            shutdownDirectSoundDevice();
            if (m_waveOutDeviceId >= 0)
                ::waveOutSetVolume(reinterpret_cast<HWAVEOUT>(static_cast<std::intptr_t>(m_waveOutDeviceId)), m_waveOutVolume);
        }
    }


    int SoundEngineWin::closeMusicStreamAndRestoreAuxVolume()
    {
        int result = 0;
        if (m_streamOwner)
        {
            callStreamDestroy(m_streamOwner);
            m_streamOwner = nullptr;
        }

        ::mciSendStringA(kStopMusicCommand, nullptr, 0, nullptr);
        ::mciSendStringA(kCloseMusicCommand, nullptr, 0, nullptr);
        if (m_auxDeviceId >= 0)
            result = static_cast<int>(::auxSetVolume(static_cast<UINT>(m_auxDeviceId), m_auxVolume));


        m_musicTrackIndex = -1;
        m_musicFadeTime = 0;
        return result;
    }


    int SoundEngineWin::pauseMusicStreamAndRestoreAuxVolume()
    {
        if (m_streamOwner)
            callStreamVoid(m_streamOwner, 3);

        if (m_auxDeviceId >= 0)
            ::auxSetVolume(static_cast<UINT>(m_auxDeviceId), m_auxVolume);
        return ::mciSendStringA(kStopMusicCommand, nullptr, 0, m_window) != 0 ? 1 : 0;
    }


    int SoundEngineWin::resumeMusicStreamAndRestoreAuxVolume()
    {
        if (m_streamOwner)
            callStreamVoid(m_streamOwner, 4);

        if (m_auxDeviceId >= 0)
            ::auxSetVolume(static_cast<UINT>(m_auxDeviceId), m_auxMusicVolume);

        char command[256];
        const char* commandText = kPlayMusicNotifyCommand;
        if (m_cdTrack0 >= 0)
        {
            std::snprintf(command, sizeof(command), kPlayMusicToNotifyFormat, m_musicTrackIndex + 1);
            commandText = command;
        }
        return ::mciSendStringA(commandText, nullptr, 0, m_window) != 0 ? 1 : 0;
    }


    int SoundEngineWin::stopMusic(int fadeTime)
    {


        if (m_streamOwner)
        {
            if (fadeTime != 0)
            {
                m_musicFadeTime = static_cast<int>(as1::core::RealCurrentTime);
                m_musicFadeVolume = fadeTime;
            }
            else
            {
                m_musicFadeTime = 0;
                m_musicFadeVolume = 0;
                callStreamVoid(m_streamOwner, 5);
            }
        }

        m_musicPath.Assign(as1::STRING::SharedEmptyText());
        m_pendingMusicLoopToken = 0;
        m_cdTrack0 = -1;
        ::mciSendStringA(kStopMusicCommand, nullptr, 0, nullptr);
        return ::mciSendStringA(kCloseMusicCommand, nullptr, 0, nullptr) != 0 ? 1 : 0;
    }



    int SoundEngineWin::stopActivePlayingBuffers()
    {
        int result = m_initializationState;
        if (m_initializationState != Initialized)
            return result;

        for (SfxBuffer& slot : m_playingSlots)
        {
            result = slot.active;
            if (result != 0)
            {


                result = slot.buffer
                    ? static_cast<int>(static_cast<IDirectSoundBuffer*>(slot.buffer)->Stop())
                    : 0;
            }
        }
        return result;
    }


    int SoundEngineWin::restoreActivePlayingBuffers()
    {
        int result = m_initializationState;
        if (m_initializationState != Initialized)
            return result;

        for (SfxBuffer& slot : m_playingSlots)
            result = slot.restore();
        return result;
    }


    int SoundEngineWin::pauseMusicAndStopActiveBuffers()
    {
        pauseMusicStreamAndRestoreAuxVolume();
        return stopActivePlayingBuffers();
    }


    int SoundEngineWin::resumeMusicAndRestoreActiveBuffers()
    {
        resumeMusicStreamAndRestoreAuxVolume();
        return restoreActivePlayingBuffers();
    }


    int SoundEngineWin::applyMasterVolumePercent(int volumePercent)
    {

        int result = volumePercent;
        if (volumePercent < 0)
            result = 0;
        else if (volumePercent > 100)
            result = 100;
        m_masterVolumePercent = result;
        return result;
    }


    int SoundEngineWin::applyMusicVolumePercent(int volumePercent)
    {

        int value = volumePercent;
        if (value < 0)
            value = 0;
        else if (value > 100)
            value = 100;

        if (value == 0)
        {
            if (m_streamOwner)
                closeMusicStreamAndRestoreAuxVolume();
        }
        else if (!m_streamOwner)
        {

            const int deviceVolume = 0xFFFF * value / 100;
            m_auxMusicVolume = static_cast<unsigned int>(deviceVolume | (deviceVolume << 16));
            if (m_auxDeviceId >= 0)
                ::auxSetVolume(static_cast<UINT>(m_auxDeviceId), m_auxMusicVolume);
        }

        m_musicVolumePercent = value;
        int result = 0;
        if (!m_streamOwner && value != 0)
        {
            char* path = duplicateSoundName(m_musicPath.c_str());
            result = openMusicStreamFromOwnedPath(path, m_pendingMusicLoopToken);
        }

        if (m_streamOwner)
            return callStreamVolume(m_streamOwner, 32 * (value - 100));
        return result;
    }


    int SoundEngineWin::playMusicFile(const char* path, int loop, int fadeTime)
    {
        const std::uint32_t loopToken = static_cast<std::uint32_t>(loop);
        if (!m_streamOwner)
        {
            char* ownedPath = duplicateSoundName(path);
            return openMusicStreamFromOwnedPath(ownedPath, loopToken);
        }


        m_musicPath.Assign(path);
        m_pendingMusicLoopToken = loopToken;
        m_musicFadeVolume = fadeTime;
        m_musicFadeTime = static_cast<int>(as1::core::RealCurrentTime);
        return 0;
    }


    int SoundEngineWin::openMusicStreamFromOwnedPath(char* path, std::uint32_t loopToken)
    {
        char* ownedPath = path;
        if (std::strcmp(ownedPath, as1::STRING::SharedEmptyText()) == 0)
        {
            releaseSoundName(ownedPath);
            return 1;
        }

        m_pendingMusicLoopToken = loopToken;


        m_musicFadeTime = 0;
        if (loopToken != 0)
            m_musicPath.Assign(ownedPath);
        else
            m_musicPath.Assign(as1::STRING::SharedEmptyText());

        const char* activePath = nullptr;
        if (m_streamOwner)
            activePath = *(static_cast<char**>(m_streamOwner) + 1);

        if (!m_streamOwner || std::strcmp(ownedPath, activePath) != 0)
        {
            if (m_streamOwner)
            {
                callStreamDestroy(m_streamOwner);
                m_streamOwner = nullptr;
            }

            if (m_musicVolumePercent == 0)
            {
                releaseSoundName(ownedPath);
                return 0;
            }

            const as1::STRING* const pathOwner = reinterpret_cast<const as1::STRING*>(&ownedPath);
            m_streamOwner = isOggFileName(ownedPath)
                ? static_cast<void*>(new OggMusicStreamOwner(pathOwner))
                : static_cast<void*>(new DirectShowMusicStreamOwner(pathOwner));
        }

        if (m_streamOwner)
        {
            if (m_musicVolumePercent > 0)
                callStreamVolume(m_streamOwner, 32 * (m_musicVolumePercent - 100));
            callStreamVoid(m_streamOwner, 6);
        }

        releaseSoundName(ownedPath);
        return 0;
    }


    void SoundEngineWin::stopSoundSystem()
    {
        closeMusicStreamAndRestoreAuxVolume();
        stopDirectSoundAndRestoreWaveVolume();
    }


    void SoundEngineWin::loadSoundEffectsFromResource(RESOURCE* resource)
    {
        as1::STRING temporaryNames[8];
        as1::STRING temporaryFfbNames[8];
        const std::uint32_t startClock = soundLoadClockMilliseconds();

        if (m_initializationState != Initialized)
            return;

        if (!resource->isOpen())
        {
            logSharedSoundError(7, "res", 0);
            return;
        }

        if (m_soundTable)
            delete[] m_soundTable;
        m_soundTable = nullptr;
        m_loadedSoundCount = 0;

        m_loadedSoundCount = resource->GetNoSubRes(as1::RESOURCE::ResTypes::SFX);
        if (m_loadedSoundCount == 0)
        {
            logSharedSoundError(11, "SFX ", 0);
            return;
        }

        m_soundTable = new (std::nothrow) Sfx[static_cast<std::size_t>(m_loadedSoundCount + 1)];
        if (!m_soundTable)
        {
            logSharedSoundError(2, "LoadSfx", m_loadedSoundCount);
            return;
        }
        if (resource->GoBegin(as1::RESOURCE::ResTypes::SFX) != 0)
            return;

        int entryIndex = 0;
        do
        {


            std::uint32_t property = 0;
            unsigned char priority = 0;
            std::uint32_t volumeBiasRaw = 0;
            resource->read(&property, 4u);
            resource->read(&priority, 1u);
            resource->read(&volumeBiasRaw, 4u);
            for (as1::STRING& name : temporaryNames)
                name.Read(resource);
            for (as1::STRING& name : temporaryFfbNames)
                name.Read(resource);
            m_soundTable[entryIndex].load(
                temporaryNames, property, priority, static_cast<int>(volumeBiasRaw * 10u),
                temporaryFfbNames, this);
            ++entryIndex;
        }
        while (resource->GoNextSub(as1::RESOURCE::ResTypes::SFX) == 0);

        as1::LOG::Write(
            "LoadSFX::No   =%-15i   sizeof(SFX)   =%-5i    load time     =%ims Quality=%i",
            m_loadedSoundCount,
            0x70,
            static_cast<unsigned>(soundLoadClockMilliseconds() - startClock),
            ((m_flags & kHighQualityFlag) != 0u) ? 1 : 0);
    }


    SoundEngineWin::Sfx::Sfx() noexcept
    {
        for (char*& name : names)
            name = as1::STRING::SharedEmptyText();
        for (char*& name : ffbNames)
            name = as1::STRING::SharedEmptyText();

        for (void*& buffer : buffers)
            buffer = nullptr;
        loadedBufferCount = 0;
    }


    SoundEngineWin::Sfx::~Sfx()
    {
        releaseBuffers();
        for (int i = 7; i >= 0; --i)
            releaseSoundName(ffbNames[static_cast<std::size_t>(i)]);
        for (int i = 7; i >= 0; --i)
            releaseSoundName(names[static_cast<std::size_t>(i)]);
    }


    void SoundEngineWin::Sfx::releaseBuffers()
    {
        for (void*& buffer : buffers)
        {
            if (!buffer)
                continue;
            static_cast<IDirectSoundBuffer*>(buffer)->Release();
            buffer = nullptr;
        }
    }


    void SoundEngineWin::Sfx::load(
        const as1::STRING* sourceNames, std::uint32_t newProperty, unsigned char newPriority,
        int newVolumeBias, const as1::STRING* sourceFfbNames, SoundEngineWin* soundOwner)
    {
        releaseBuffers();
        for (std::size_t i = 0; i < 8; ++i)
        {
            reinterpret_cast<as1::STRING*>(&names[i])->Assign(sourceNames[i]);
            reinterpret_cast<as1::STRING*>(&ffbNames[i])->Assign(sourceFfbNames[i]);
        }
        property = newProperty;
        priority = newPriority;
        volumeBias = newVolumeBias;
        loadedBufferCount = 0;

        as1::RESOURCE waveResource;
        for (std::size_t index = 0; index < 8; ++index)
        {
            const char* const name = names[index];
            if (name[0] == '\0')
                return;

            if (isOggFileName(name))
            {
                OggDecodeState decoder{};
                std::FILE* openedFile = nullptr;
                const as1::STRING* const nameOwner =
                    reinterpret_cast<const as1::STRING*>(&names[index]);
                void* const openedBuffer = soundOwner->createOggSoundBufferFromFile(
                    nameOwner, &decoder, reinterpret_cast<void**>(&openedFile), 0);
                buffers[index] = openedBuffer;

                if (openedBuffer)
                {
                    std::array<unsigned char, 0x1000> decoded{};
                    std::uint32_t position = 0u;
                    for (;;)
                    {
                        int currentSection = 0;
                        const int decodedBytes = readOggPcmChunk(
                            decoder, decoded.data(), static_cast<std::uint32_t>(decoded.size()), currentSection);
                        if (decodedBytes == 0)
                            break;
                        if (decodedBytes < 0)
                        {
                            as1::LOG::ResourceError("SFX", 10, "decode ogg", decodedBytes);
                            continue;
                        }
                        (void)writeDecodedOggChunkToDirectSoundBuffer(
                            static_cast<IDirectSoundBuffer*>(openedBuffer), position, decoded.data(),
                            static_cast<std::uint32_t>(decodedBytes));
                        position += static_cast<std::uint32_t>(decodedBytes);
                    }
                }

                if (openedFile)
                {
                    std::fclose(openedFile);
                    closeOggDecodeState(decoder);
                    openedFile = nullptr;
                }
                loadedBufferCount = static_cast<int>(index + 1);
                continue;
            }

            const as1::STRING* const nameOwner =
                reinterpret_cast<const as1::STRING*>(&names[index]);
            void* const buffer = soundOwner->createWaveSoundBufferFromResourceFile(
                nameOwner, &waveResource, 0);
            buffers[index] = buffer;
            if (buffer)
            {
                void* first = nullptr;
                void* second = nullptr;
                DWORD firstSize = 0;
                DWORD secondSize = 0;
                IDirectSoundBuffer* const soundBuffer = static_cast<IDirectSoundBuffer*>(buffer);
                const DWORD totalSize = static_cast<DWORD>(waveResource.SubSize());
                const HRESULT lockResult = soundBuffer->Lock(
                    0, totalSize, &first, &firstSize, &second, &secondSize, 0);
                if (SUCCEEDED(lockResult))
                {
                    waveResource.ReadPacked(first, firstSize, nullptr);
                    if (secondSize != 0)
                        waveResource.ReadPacked(second, secondSize, nullptr);
                    soundBuffer->Unlock(first, firstSize, second, secondSize);
                }
            }
            waveResource.close();
            loadedBufferCount = static_cast<int>(index + 1);
        }
    }


    void* SoundEngineWin::Sfx::acquirePlayableBuffer(void* directSound)
    {
        int selectedIndex = 0;
        if (loadedBufferCount > 1)
            selectedIndex = std::rand() % loadedBufferCount;

        void* selectedBuffer = buffers[static_cast<std::size_t>(selectedIndex)];
        if (!selectedBuffer)
            return nullptr;
        IDirectSoundBuffer* const sourceBuffer = static_cast<IDirectSoundBuffer*>(selectedBuffer);
        const unsigned long referenceCountAfterAdd = sourceBuffer->AddRef();
        if (referenceCountAfterAdd <= 2)
        {
            sourceBuffer->SetCurrentPosition(0);
            return sourceBuffer;
        }

        sourceBuffer->Release();
        IDirectSoundBuffer* duplicatedBuffer = nullptr;
        const HRESULT result = static_cast<IDirectSound8*>(directSound)->DuplicateSoundBuffer(
            sourceBuffer, &duplicatedBuffer);
        if (FAILED(result))
        {
            as1::LOG::Write(
                kDuplicateBufferErrorText, reinterpret_cast<const char*>(this),
                static_cast<unsigned>(result));
            return nullptr;
        }
        if (duplicatedBuffer)
            duplicatedBuffer->SetCurrentPosition(0);
        return duplicatedBuffer;
    }

    void SoundEngineWin::rebuildSoundBuffersAfterLoss()
    {


        if (m_loadedSoundCount <= 0 || !m_soundTable)
            return;

        for (int i = 0; i < m_loadedSoundCount; ++i)
        {
            Sfx& entry = m_soundTable[i];
            entry.load(
                reinterpret_cast<const as1::STRING*>(entry.names), entry.property, entry.priority,
                entry.volumeBias, reinterpret_cast<const as1::STRING*>(entry.ffbNames), this);
        }
    }


    int SoundEngineWin::stopAllPlayingBuffers()
    {
        int result = m_initializationState;
        if (m_initializationState != Initialized)
            return result;

        result = 0;
        for (SfxBuffer& slot : m_playingSlots)
        {
            slot.active = 0;


            result = slot.buffer
                ? static_cast<int>(static_cast<IDirectSoundBuffer*>(slot.buffer)->Stop())
                : 0;
        }
        return result;
    }


    void* SoundEngineWin::createOggSoundBufferFromFile(
        const as1::STRING* pathOwner, void* decodeStateOwner, void** fileOut, int requestedBufferBytes)
    {
        auto& state = *static_cast<OggDecodeState*>(decodeStateOwner);
        auto** const fileOwner = reinterpret_cast<std::FILE**>(fileOut);
        *fileOwner = nullptr;
        if (!m_directSound)
            return nullptr;

        const char* const path = pathOwner->c_str();
        if (path[0] == '\0')
        {
            logSharedSoundError(7, path, 0);
            return nullptr;
        }

        std::FILE* file = std::fopen(path, "rb");
        *fileOwner = file;
        if (!file)
        {
            logSharedSoundError(7, path, 0);
            return nullptr;
        }

        if (openOggDecodeStateFromFile(file, state) < 0)
        {
            logSharedSoundError(4, path, 0);
            std::fclose(file);
            *fileOwner = nullptr;
            return nullptr;
        }

        OggPcmInfo info{};
        readOggPcmInfo(state, info);
        std::uint32_t bufferBytes = static_cast<std::uint32_t>(requestedBufferBytes);
        if (requestedBufferBytes == 0)
            bufferBytes = static_cast<std::uint32_t>(2 * info.channels * oggPcmTotalSamples(state));

        int createResult = 0;
        void* const buffer = createDirectSoundOggPcmBuffer(m_directSound, info, bufferBytes, createResult);
        if (!buffer)
            logSharedSoundError(3, kSoundBufferForOggText, createResult);
        return buffer;
    }


    void* SoundEngineWin::createWaveSoundBufferFromResourceFile(
        const as1::STRING* nameOwner, as1::RESOURCE* waveResourceOwner, int reserved)
    {
        (void)reserved;

        if (!m_directSound)
            return nullptr;
        const char* const name = nameOwner->c_str();
        if (std::strcmp(name, "wav\\null.wav") == 0 ||
            std::strcmp(name, "null.wav") == 0 ||
            std::strcmp(name, "null") == 0)
            return nullptr;

        as1::RESOURCE& waveResource = *waveResourceOwner;
        if (waveResource.openFile(nameOwner, as1::RESOURCE::ResTypes::WAVE) != 0)
            return nullptr;

        if (waveResource.GoBegin(as1::RESOURCE::ResTypes::WAVE_FMT) != 0)
        {
            as1::LOG::Write("!!!ERROR!!!SFX:'%s' 'fmt ' not found", name);
            return nullptr;
        }

        if (waveResource.SubSize() < 14)
        {
            as1::LOG::Write("!!!ERROR!!!SFX:'%s' incorrect size %i", name, waveResource.SubSize());
            return nullptr;
        }

        WAVEFORMATEX format;
        waveResource.read(&format, static_cast<unsigned>(sizeof(format)));

        if (waveResource.GoNext(as1::RESOURCE::ResTypes::WAVE_DATA) != 0 &&
            waveResource.GoBegin(as1::RESOURCE::ResTypes::WAVE_DATA) != 0)
        {
            as1::LOG::Write("!!!ERROR!!!SFX:'%s' 'data' not found", name);
            return nullptr;
        }

        DSBUFFERDESC description;
        std::memset(&description, 0, sizeof(description));
        description.dwSize = sizeof(description);
        description.dwFlags = kDirectSoundStaticBufferFlags;
        description.dwBufferBytes = static_cast<DWORD>(waveResource.SubSize());
        description.lpwfxFormat = &format;

        IDirectSoundBuffer* buffer = nullptr;
        const HRESULT result = static_cast<IDirectSound8*>(m_directSound)->CreateSoundBuffer(&description, &buffer, nullptr);
        if (FAILED(result))
        {
            logSharedSoundError(3, kSoundBufferText, static_cast<int>(result));
            return nullptr;
        }
        return buffer;
    }


} } }
