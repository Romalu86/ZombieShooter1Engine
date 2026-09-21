#include "script.h"
#include "script/native_function_codes.h"
#include "zs1/zScriptExec.h"
#include "zs1/zScriptExec_engine.h"
#include "script/vid_data_codes.h"

#include "core/application.h"
#include "base_sprite_list.h"
#include "menu.h"
#include "core/configuration.h"
#include "core/crc32.h"
#include "core/file_stream.h"
#include "core/file_logger.h"
#include "core/log.h"
#include "core/profile_p.h"
#include "mouse.h"
#include "input.h"
#include "map.h"
#include "player_arcade.h"
#include "engine.h"
#include "graph.h"
#include "core/weak_controller.h"
#include "sprite.h"
#include "sprite_collector.h"
#include "sound/sound_engine.h"
#include "vid/vid.h"
#include "win/application_win.h"
#include <windows.h>
#include <shellapi.h>

#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <cstdint>
#include <string>
#include <io.h>

namespace as1
{

    int ScriptExecFunc(int opcode);

    namespace
    {
        std::uint32_t fileLength32(const FSTREAM& stream) noexcept
        {
            const std::FILE* file = stream.nativeFile();
            if (!file)
                return 0xFFFFFFFFu;
            const int fd = _fileno(const_cast<std::FILE*>(file));
            if (fd < 0)
                return 0xFFFFFFFFu;
            return static_cast<std::uint32_t>(_filelength(fd));
        }

        template <class T>
        T* allocateScriptRecords(int capacity)
        {
            if (capacity <= 0)
                return nullptr;
            const std::size_t bytes = sizeof(std::uint32_t) +
                static_cast<std::size_t>(capacity) * sizeof(T);
            auto* raw = static_cast<std::uint8_t*>(::operator new(bytes));
            *reinterpret_cast<std::uint32_t*>(raw) = static_cast<std::uint32_t>(capacity);
            T* const records = reinterpret_cast<T*>(raw + sizeof(std::uint32_t));
            int constructed = 0;
            try
            {
                for (; constructed < capacity; ++constructed)
                    ::new (static_cast<void*>(records + constructed)) T();
            }
            catch (...)
            {
                while (constructed > 0)
                {
                    --constructed;
                    records[constructed].~T();
                }
                ::operator delete(static_cast<void*>(raw));
                throw;
            }
            return records;
        }

        const char Class[] = "";


        int nmonster = 0;
        int typeunit = 0;
        int g_scriptSpriteIteratorPass = 0;
        int n_sprite = 0;


        int g_scriptCompileDynamicArgument = 0;

        std::FILE* scriptNativeFileFromInt(int value)
        {
            return reinterpret_cast<std::FILE*>(static_cast<std::intptr_t>(value));
        }

        int scriptNativeIntFromFile(std::FILE* file)
        {
            return static_cast<int>(reinterpret_cast<std::intptr_t>(file));
        }

        const input::InputMessageState& scriptApplicationInputState() noexcept
        {


            const auto* const owner = static_cast<const std::uint8_t*>(core::ApplicationOwner());
            return *reinterpret_cast<const input::InputMessageState*>(
                owner + core::application_layout::InputState);
        }


        void scriptNativeDecodeGammaIndex(int value, std::uint32_t& diffuse, std::uint32_t& specular)
        {
            diffuse = 0;
            specular = 0;
            const std::uint32_t packed = static_cast<std::uint32_t>(value);
            for (int shift = 0; shift < 32; shift += 8)
            {
                const std::uint32_t byteValue = (packed >> shift) & 0xFFu;
                const std::uint32_t component = ((byteValue & 0x80u) != 0)
                    ? (((~byteValue) & 0x7Fu) << 1)
                    : ((byteValue & 0x7Fu) << 1);
                if ((byteValue & 0x80u) != 0)
                    specular |= (component & 0xFFu) << shift;
                else
                    diffuse |= (component & 0xFFu) << shift;
            }
        }


        unsigned char CountByteGamma(int a1, int a2, int time)
        {
            int first = a1;
            if (first >= 0x80)
                first -= 0xFE;
            int second = a2;
            if (second >= 0x80)
                second -= 0xFE;
            const std::uint32_t rawDelta =
                static_cast<std::uint32_t>(second - first) * static_cast<std::uint32_t>(time);
            std::int32_t delta = 0;
            std::memcpy(&delta, &rawDelta, sizeof(delta));
            const int step = delta / 255;
            return (step + first) & 0xFF;
        }


        int CountGamma(int g1, int g2, int time)
        {
            const int b0 = CountByteGamma(g1 & 0xFF, g2 & 0xFF, time) & 0xFF;
            const int b1 = CountByteGamma((g1 >> 8) & 0xFF, (g2 >> 8) & 0xFF, time) & 0xFF;
            const int b2 = CountByteGamma((g1 >> 16) & 0xFF, (g2 >> 16) & 0xFF, time) & 0xFF;
            return b0 | (b1 << 8) | (b2 << 16);
        }


    }


    int SCRIPT::compileScriptSourceFile(const STRING& scriptFile, const STRING& gameRoot)
    {
        (void)gameRoot;

        FSTREAM stream(scriptFile.str(), "rb");
        resetScriptVmState();
        assignStringFromString(scriptFileStorage(), scriptFile);
        if (!stream.isOpen())
        {
            reportCompileError(7, "", 0);
            return 1;
        }

        const std::uint32_t fileLength = fileLength32(stream);
        prepareSourceCompiler(scriptFile, &stream, fileLength);

        int status = compileNextSourceItem();
        while (status == 0)
            status = compileNextSourceItem();


        if (m_data.compileError != 0)
        {

            resetScriptVmState();
            stream.close();
            return 1;
        }


        if (const script::LogicFunctionRecord* const records = functionRecordStorage())
        {
            for (int i = 0; i < m_data.functionCount; ++i)
            {
                const script::LogicFunctionRecord& rec = records[static_cast<std::size_t>(i)];
                if (rec.flags == 3 &&
                    (rec.statusFlags & 0x01u) != 0u &&
                    (rec.statusFlags & 0x02u) == 0u)
                {
                    writeLogLine(g_fileLogger,
                                 "!!!ERROR!!!SCRIPT: function %s() not return value",
                                 rec.name.c_str());
                }
            }
        }

        writeLogLine(
            g_fileLogger,
            "LoadScript::ByteCode=%i varNo=%i DefineNo=%i stackNo=%i",
            m_data.bytecodeEnd,
            functionCount(),
            defineCount(),
            executionStackCount());

        clearDefines();

        if (m_data.bytecodeEnd != 0)
        {
            if (m_data.bytecodeEnd > static_cast<int>(TemporaryBytecodeCapacity))
            {
                reportCompileError(2, "byte code size", m_data.bytecodeEnd);
            }


            try
            {
                const std::size_t finalSize = static_cast<std::size_t>(
                    static_cast<std::uint32_t>(m_data.bytecodeEnd));
                void* const finalBytecode = ::operator new(finalSize);
                if (finalSize != 0)
                    std::memcpy(finalBytecode, bytecodeStorage(), finalSize);
                ::operator delete(static_cast<void*>(bytecodeStorage()));
                m_data.bytecodeBufferToken = pointerToken(finalBytecode);
            }
            catch (...)
            {


                reportCompileError(2, "tmp", 0);
                std::exit(1);
            }
        }
        else
        {
            resetScriptVmState();
        }

        ::operator delete(static_cast<void*>(sourceStorage()));
        m_data.sourceBufferToken = 0u;
        stream.close();
        return 0;
    }


    void SCRIPT::writeExecutionStackToStream(BaseStream* stream)
    {
        for (int i = 0; i < m_data.stackCount; ++i)
            executionStackStorage()[static_cast<std::size_t>(i)].Write(stream);

        stream->write(&m_data.functionCount, 4);
        const script::LogicFunctionRecord* const records = functionRecordStorage();
        for (int i = 0; i < m_data.functionCount; ++i)
        {
            const script::LogicFunctionRecord& rec = records[static_cast<std::size_t>(i)];
            stream->write(rec.name.c_str(), static_cast<unsigned>(std::strlen(rec.name.c_str()) + 1u));
            stream->write(reinterpret_cast<const std::uint8_t*>(&rec) + 4u, 20u);
        }
        for (int i = 0; i < m_data.functionCount; ++i)
        {
            const char* const text = records[static_cast<std::size_t>(i)].text.c_str();
            stream->write(text, static_cast<unsigned>(std::strlen(text) + 1u));
        }
    }


    void SCRIPT::readExecutionStackFromStream(BaseStream* stream)
    {
        for (int i = 0; i < m_data.stackCount; ++i)
            executionStackStorage()[static_cast<std::size_t>(i)].Read(stream);

        int serializedFunctionCount = 0;
        stream->read(&serializedFunctionCount, 4);
        if (serializedFunctionCount > m_data.functionCapacity)
        {
            script::LogicFunctionRecord* replacement = nullptr;
            try
            {
                replacement = allocateScriptRecords<script::LogicFunctionRecord>(serializedFunctionCount);
            }
            catch (...)
            {
                fatalLogError(g_fileLogger, "!!!ERROR!!!::LIST: Not enough memory %i", serializedFunctionCount);
            }
            script::LogicFunctionRecord* const oldRecords = functionRecordStorage();
            for (int i = 0; i < m_data.functionCapacity; ++i)
                replacement[i].copyFrom(oldRecords[i]);
            if (oldRecords)
                script::destroyLogicFunctionRecordStorage(oldRecords, 3);
            m_data.functionTableToken = pointerToken(replacement);
            m_data.functionCapacity = serializedFunctionCount;
        }
        m_data.functionCount = serializedFunctionCount;

        script::LogicFunctionRecord* const records = functionRecordStorage();
        for (int i = 0; i < m_data.functionCount; ++i)
        {
            script::LogicFunctionRecord& rec = records[static_cast<std::size_t>(i)];
            rec.name.Read(stream);
            stream->read(reinterpret_cast<std::uint8_t*>(&rec) + 4u, 20u);
        }
        for (int i = 0; i < m_data.functionCount; ++i)
        {
            script::LogicFunctionRecord& rec = records[static_cast<std::size_t>(i)];
            rec.text.ResetSharedEmptyWithoutRelease();
            rec.text.Read(stream);
        }
    }

    void SCRIPT::clearExecutionStack()
    {
        m_data.stackCapacity = 0;
        m_data.stackCount = 0;
        script::StackObject* const records = executionStackStorage();
        if (records)
            destroyStringRecordArray(static_cast<void*>(records), 3);
        m_data.stackTableToken = 0;
    }

    int SCRIPT::executionStackCount() const
    {
        return m_data.stackCount;
    }

    int SCRIPT::executionStackCapacity() const
    {
        return m_data.stackCapacity;
    }

    script::StackObject* SCRIPT::mutableExecutionStackStorageAt(int index)
    {
        return &executionStackStorage()[static_cast<std::size_t>(index)];
    }

    void SCRIPT::growExecutionStackForAppend()
    {
        auto* const stackList = reinterpret_cast<script::StackObjectList*>(&m_data.stackListVtable);
        if (stackList->count() >= stackList->capacity())
        {
            const int newCapacity = stackList->capacity() * 2 + 4;
            if (newCapacity > stackList->capacity())
                stackList->reserveExact(newCapacity);
        }
    }


    int SCRIPT::DeletePointerToObject(void* object)
    {
        const int target = static_cast<int>(static_cast<std::uint32_t>(
            reinterpret_cast<std::uintptr_t>(object) & 0xFFFFFFFFu));
        int cleared = 0;
        for (int i = 0; i < m_data.stackCount; ++i)
        {
            script::StackObject& value = executionStackStorage()[static_cast<std::size_t>(i)];
            if ((value.flags & script::STACK_OBJECT_REF) != 0 && value.intValue == target)
            {
                value.intValue = 0;
                value.flags = static_cast<std::uint8_t>(value.flags & ~script::STACK_OBJECT_REF);
                ++cleared;
            }
        }
        return cleared;
    }

    void SCRIPT::appendExecutionStackObject(const script::StackObject& value)
    {
        reinterpret_cast<script::StackObjectList*>(&m_data.stackListVtable)->Push(&value);
    }

    void SCRIPT::appendExecutionStackRecord(std::uint8_t flags, int value, const STRING& text)
    {
        reinterpret_cast<script::StackObjectList*>(&m_data.stackListVtable)->appendFields(
            flags, value, text);
    }

    int SCRIPT::functionCount() const
    {
        return m_data.functionCount;
    }

    int SCRIPT::functionCapacity() const
    {
        return m_data.functionCapacity;
    }

    const script::LogicFunctionRecord* SCRIPT::functionRecordAt(int index) const noexcept
    {
        if (index < 0 || index >= m_data.functionCount || functionRecordStorage() == nullptr)
            return nullptr;
        return functionRecordStorage() + index;
    }

    script::LogicFunctionRecord* SCRIPT::mutableFunctionRecordAt(int index) noexcept
    {
        if (index < 0 || index >= m_data.functionCount || functionRecordStorage() == nullptr)
            return nullptr;
        return functionRecordStorage() + index;
    }


int SCRIPT::defineCount() const
    {
        return m_data.defineCount;
    }

    int SCRIPT::defineCapacity() const
    {
        return m_data.defineCapacity;
    }

    const STRING& SCRIPT::scriptFile() const
    {
        return scriptFileStorage();
    }

    int SCRIPT::bytecodeEnd() const
    {
        return m_data.bytecodeEnd;
    }

    int SCRIPT::sourceCursorOffset() const
    {
        if (m_data.sourceBufferToken == 0u || m_data.sourceCursor == 0u)
            return 0;
        return static_cast<int>(m_data.sourceCursor - m_data.sourceBufferToken);
    }

    int SCRIPT::sourceEndOffset() const
    {
        if (m_data.sourceBufferToken == 0u || m_data.sourceEnd == 0u)
            return 0;
        return static_cast<int>(m_data.sourceEnd - m_data.sourceBufferToken);
    }

    int SCRIPT::conditionalDepth() const
    {
        return m_data.conditionalDepth;
    }

    int SCRIPT::parseMode() const
    {
        return m_data.parseMode;
    }

    void destroyScriptDefinePair(ScriptDefinePairRecord* self) noexcept
    {
        destroyStringStorage(self->value);
        destroyStringStorage(self->name);
    }

    void* scriptDefinePairDeletingDestructor(ScriptDefinePairRecord* self, unsigned char flags) noexcept
    {
        if ((flags & 0x02u) != 0)
        {
            std::uint32_t* const header = reinterpret_cast<std::uint32_t*>(self) - 1;
            const std::uint32_t count = *header;
            for (std::uint32_t i = count; i != 0; --i)
                destroyScriptDefinePair(self + (i - 1u));
            if ((flags & 0x01u) != 0)
                ::operator delete(static_cast<void*>(header));
            return header;
        }

        destroyScriptDefinePair(self);
        if ((flags & 0x01u) != 0)
            ::operator delete(static_cast<void*>(self));
        return self;
    }


    void SCRIPT::resetScriptVmState()
    {
        if (bytecodeStorage())
            ::operator delete(static_cast<void*>(bytecodeStorage()));
        m_data.bytecodeBufferToken = 0u;
        if (sourceStorage())
            ::operator delete(static_cast<void*>(sourceStorage()));
        m_data.sourceBufferToken = 0u;
        m_data.sourceCursor = 0u;

        clearExecutionStack();

        clearFunctionTable();

        clearDefines();

        m_data.bytecodeEnd = 0;
        m_data.conditionalDepth = 0;
        m_data.fallbackFunction = -1;
        m_data.sourceCursor = 0;
        m_data.parseMode = 0;

        m_data.currentSymbolToken = 0u;
        m_data.ownedBufferToken = 0u;
        m_data.compileError = 0;
        m_data.parseContext = 0;
        for (int i = 0; i < 0x100; ++i)
        {
            m_data.actionN[i] = -1;
            m_data.scriptEventFunction[i] = -1;
        }
    }

    void SCRIPT::prepareSourceCompiler(const STRING& scriptFile, BaseStream* stream, std::uint32_t sourceSize)
    {
        (void)scriptFile;
        try
        {
            m_data.bytecodeBufferToken = pointerToken(::operator new(TemporaryBytecodeCapacity));
        }
        catch (...)
        {
            reportCompileError(2, "data", 0);
            std::exit(1);
        }
        m_data.bytecodeEnd = 0;
        syncBackingPointers();

        try
        {
            const std::uint32_t allocationSize = sourceSize + static_cast<std::uint32_t>(SourceBufferPadding);
            m_data.sourceBufferToken = pointerToken(::operator new(static_cast<std::size_t>(allocationSize)));
        }
        catch (...)
        {
            reportCompileError(2, "ini", 0);
            std::exit(1);
        }
        syncBackingPointers();
        setSourceCursorOffset(static_cast<int>(SourcePayloadOffset));
        const std::uint32_t sourceEndOffset = static_cast<std::uint32_t>(SourcePayloadOffset) + sourceSize;
        setSourceEndOffset(static_cast<std::int32_t>(sourceEndOffset));
        if (sourceSize != 0)
        {
            stream->read(sourceStorage() + SourcePayloadOffset,
                static_cast<unsigned>(sourceSize));
        }
        syncSourcePointers();

        if (executionStackCapacity() < static_cast<int>(InitialListCapacity))
        {
            try
            {
                script::StackObject* const records = allocateScriptRecords<script::StackObject>(static_cast<int>(InitialListCapacity));
                m_data.stackTableToken = pointerToken(records);
            }
            catch (...)
            {
                fatalLogError(g_fileLogger, "!!!ERROR!!!::LIST: Not enough memory %i",
                    static_cast<int>(InitialListCapacity));
            }
            m_data.stackCapacity = static_cast<int>(InitialListCapacity);
        }
        m_data.stackCount = 0;

        if (functionCapacity() < static_cast<int>(InitialListCapacity))
        {
            try
            {
                script::LogicFunctionRecord* const records = allocateScriptRecords<script::LogicFunctionRecord>(static_cast<int>(InitialListCapacity));
                if (functionRecordStorage())
                    script::destroyLogicFunctionRecordStorage(functionRecordStorage(), 3);
                m_data.functionTableToken = pointerToken(records);
                m_data.functionCapacity = static_cast<int>(InitialListCapacity);
            }
            catch (...)
            {
                fatalLogError(g_fileLogger, "!!!ERROR!!!::LIST: Not enough memory %i", static_cast<int>(InitialListCapacity));
            }
        }

        m_data.sourceLine = 0;
        m_data.conditionalDepth = 0;
        m_data.parseMode = 0;
    }

    void SCRIPT::clearFunctionTable()
    {
        if (functionRecordStorage())
            script::destroyLogicFunctionRecordStorage(functionRecordStorage(), 3);
        m_data.functionCount = 0;
        m_data.functionCapacity = 0;
        m_data.functionTableToken = 0u;
    }

    void SCRIPT::appendFunctionRecord(const STRING& name, std::uint8_t flags, const STRING& text, int bytecodeStart0C, int stackBase10, int argCount14)
    {
        if (m_data.functionCount >= m_data.functionCapacity)
        {
            const int oldCapacity = m_data.functionCapacity;
            const int newCapacity = oldCapacity * 2 + 4;
            script::LogicFunctionRecord* replacement = nullptr;
            try
            {
                replacement = allocateScriptRecords<script::LogicFunctionRecord>(newCapacity);
            }
            catch (...)
            {
                fatalLogError(g_fileLogger, "!!!ERROR!!!::LIST: Not enough memory %i", newCapacity);
            }
            script::LogicFunctionRecord* const oldRecords = functionRecordStorage();
            for (int i = 0; i < oldCapacity; ++i)
                replacement[i].copyFrom(oldRecords[i]);
            if (oldRecords)
                script::destroyLogicFunctionRecordStorage(oldRecords, 3);
            m_data.functionTableToken = pointerToken(replacement);
            m_data.functionCapacity = newCapacity;
        }
        script::LogicFunctionRecord temp;
        temp.name = name;
        temp.flags = flags;
        temp.text = text;
        temp.value0 = bytecodeStart0C;
        temp.value1 = stackBase10;
        temp.value2 = argCount14;
        functionRecordStorage()[static_cast<std::size_t>(m_data.functionCount)].copyFrom(temp);
        ++m_data.functionCount;
    }

    void SCRIPT::clearDefines()
    {
        m_data.defineCount = 0;
        m_data.defineCapacity = 0;
        ScriptDefinePairRecord* const records = defineRecordStorage();
        if (records)
            scriptDefinePairDeletingDestructor(records, 3);
        m_data.defineTableToken = 0;
    }

    int SCRIPT::findDefine(const STRING& name) const
    {

        const ScriptDefinePairRecord* const records = defineRecordStorage();
        for (int i = m_data.defineCount - 1; i >= 0; --i)
        {
            if (std::strcmp(records[static_cast<std::size_t>(i)].name.c_str(), name.c_str()) == 0)
                return i;
        }
        return -1;
    }

    int SCRIPT::addOrReplaceDefine(const STRING& name, const STRING& value)
    {
        const int existing = findDefine(name);
        if (existing >= 0)
        {
            defineRecordStorage()[static_cast<std::size_t>(existing)].value = value;
            return existing;
        }

        if (defineCount() >= m_data.defineCapacity)
        {
            const int oldCapacity = m_data.defineCapacity;
            const int capacity = oldCapacity * 2 + 4;
            try
            {
                ScriptDefinePairRecord* const oldRecords = defineRecordStorage();
                ScriptDefinePairRecord* const replacement = allocateScriptRecords<ScriptDefinePairRecord>(capacity);
                for (int i = 0; i < oldCapacity; ++i)
                {
                    replacement[i].name = oldRecords[i].name;
                    replacement[i].value = oldRecords[i].value;
                }
                if (oldRecords)
                    scriptDefinePairDeletingDestructor(oldRecords, 3);
                m_data.defineTableToken = pointerToken(replacement);
            }
            catch (...)
            {
                fatalLogError(g_fileLogger, "!!!ERROR!!!::LIST: Not enough memory %i", capacity);
            }
            m_data.defineCapacity = capacity;
        }

        ScriptDefinePairRecord rec;
        rec.name = name;
        rec.value = value;
        ScriptDefinePairRecord& dst = defineRecordStorage()[static_cast<std::size_t>(m_data.defineCount)];
        dst.name = rec.name;
        dst.value = rec.value;
        ++m_data.defineCount;
        return m_data.defineCount - 1;
    }

    int SCRIPT::undefine(const STRING& name)
    {
        const int index = findDefine(name);
        if (index < 0)
            return -1;

        ScriptDefinePairRecord* const records = defineRecordStorage();
        for (int i = index; i + 1 < m_data.defineCount; ++i)
        {
            records[i].name = records[i + 1].name;
            records[i].value = records[i + 1].value;
        }
        if (m_data.defineCount > 0)
        {
            records[m_data.defineCount - 1].name = STRING();
            records[m_data.defineCount - 1].value = STRING();
            --m_data.defineCount;
        }
        if (m_data.defineCount == 0)
            clearDefines();
        return index;
    }

    int SCRIPT::rewriteDefineMacro(int tokenStartOffset, int tokenLength)
    {


        const char* const token = reinterpret_cast<const char*>(
            sourceStorage() + static_cast<std::size_t>(tokenStartOffset));

        int found = -1;
        const ScriptDefinePairRecord* const records = defineRecordStorage();
        for (int i = m_data.defineCount - 1; i >= 0; --i)
        {
            const char* const defineName = records[static_cast<std::size_t>(i)].name.c_str();
            const std::size_t defineNameLength = std::strlen(defineName);
            if (defineNameLength == static_cast<std::size_t>(tokenLength) &&
                std::memcmp(defineName, token, defineNameLength) == 0)
            {
                found = i;
                break;
            }
        }
        if (found < 0)
            return -1;

        const char* const value = records[static_cast<std::size_t>(found)].value.c_str();
        const int valueLength = static_cast<int>(std::strlen(value));
        const int newCursor = tokenStartOffset + tokenLength - valueLength;
        setSourceCursorOffset(newCursor);
        if (valueLength > 0)
        {
            std::memcpy(sourceStorage() + static_cast<std::size_t>(newCursor),
                        value, static_cast<std::size_t>(valueLength));
        }
        return found;
    }

    std::uint8_t SCRIPT::sourceByteAtCursor() const
    {
        return sourceStorage()[static_cast<std::size_t>(sourceCursorOffset())];
    }

    void SCRIPT::setSourceCursorOffset(int offset)
    {
        m_data.sourceCursor = m_data.sourceBufferToken == 0u
            ? 0u
            : m_data.sourceBufferToken + static_cast<std::uint32_t>(offset);
    }


    void SCRIPT::reportCompileError(int errorCode, const char* detailText, int detailValue)
    {
        logFileLoggerResourceError(g_fileLogger,
            "LOGIC '%s' line %i",
            errorCode,
            detailText,
            detailValue,
            scriptFileStorage().c_str(),
            m_data.sourceLine + 1);

        if (m_data.sourceCursor == 0)
            return;


        m_data.compileError = 1;

        char window[61];
        const int start = sourceCursorOffset() - 30;
        for (int i = 0; i < 60; ++i)
        {
            unsigned char c = sourceStorage()[static_cast<std::size_t>(start + i)];
            if (c == '\n' || c == '\r' || c == '\t')
                c = '?';
            window[i] = static_cast<char>(c);
        }
        window[60] = '\0';
        logFileLoggerResourceError(g_fileLogger, "LOGIC", 10, window, 0);

        for (int i = 0; i < 60; ++i)
            window[i] = (i == 30) ? '^' : ' ';
        window[60] = '\0';
        logFileLoggerResourceError(g_fileLogger, "LOGIC", 10, window, 0);
    }


    void SCRIPT::EmitByteIfNoError(std::uint8_t opcode)
    {
        if (m_data.compileError != 0)
            return;
        bytecodeStorage()[static_cast<std::size_t>(m_data.bytecodeEnd)] = opcode;
        ++m_data.bytecodeEnd;
    }


    void SCRIPT::EmitByteInt32(std::uint8_t opcode, int value)
    {
        const std::size_t offset = static_cast<std::size_t>(m_data.bytecodeEnd);
        bytecodeStorage()[offset] = opcode;
        ++m_data.bytecodeEnd;
        std::memcpy(bytecodeStorage() + static_cast<std::size_t>(m_data.bytecodeEnd),
                    &value, sizeof(value));
        m_data.bytecodeEnd += 4;
    }

    namespace
    {
        int ctypeArgument(unsigned char c) noexcept
        {
            return static_cast<int>(static_cast<signed char>(c));
        }

        bool isIdentifierStart(unsigned char c)
        {
            return std::isalpha(ctypeArgument(c)) != 0 || c == '_';
        }

        bool isIdentifierChar(unsigned char c)
        {
            return std::isalnum(ctypeArgument(c)) != 0 || c == '_';
        }

        bool isScriptWhitespace(unsigned char c)
        {
            return std::isspace(ctypeArgument(c)) != 0;
        }
    }

    int SCRIPT::skipTriviaAndPreprocess()
    {
        int skipDepth = 0;
        int commentState = 0;

        while (sourceCursorOffset() < sourceEndOffset())
        {
            if (commentState != 0)
            {
                const unsigned char c = sourceByteAtCursor();
                if (commentState == 1)
                {
                    if (c == '\n')
                        commentState = 0;
                }
                else if (commentState == 2)
                {
                    if (c == '/' &&
                        sourceStorage()[static_cast<std::size_t>(sourceCursorOffset() - 1)] == '*')
                        commentState = 0;
                }
                setSourceCursorOffset(sourceCursorOffset() + 1);
                if (c == '\n')
                    ++m_data.sourceLine;
                continue;
            }

            unsigned char c = sourceByteAtCursor();

            if (isIdentifierStart(c) && m_data.parseMode == 0)
            {
                const int tokenStart = sourceCursorOffset();
                int tokenLength = 0;
                while (isIdentifierChar(sourceStorage()[static_cast<std::size_t>(tokenStart + tokenLength)]))
                {
                    if (tokenLength >= 0x0FFF)
                    {
                        reportCompileError(10, "Very long name", 0);
                        std::exit(1);
                    }
                    ++tokenLength;
                }
                if (rewriteDefineMacro(tokenStart, tokenLength) >= 0)
                    continue;
            }

            if (c == '#')
            {
                const char* cur = reinterpret_cast<const char*>(sourceStorage()) + sourceCursorOffset();

                if (std::strncmp(cur, "#ifdef", 6) == 0)
                {
                    setSourceCursorOffset(sourceCursorOffset() + 6);
                    ++m_data.conditionalDepth;
                    if (skipDepth == 0)
                    {
                        STRING name;
                        m_data.parseMode = 1;
                        readIdentifier(name);
                        m_data.parseMode = 0;
                        if (getFunctionIndex(name) < 0 && findDefine(name) < 0)
                            skipDepth = m_data.conditionalDepth;
                    }
                    continue;
                }

                if (std::strncmp(cur, "#ifndef", 7) == 0)
                {
                    setSourceCursorOffset(sourceCursorOffset() + 7);
                    ++m_data.conditionalDepth;
                    if (skipDepth == 0)
                    {
                        STRING name;
                        m_data.parseMode = 1;
                        readIdentifier(name);
                        m_data.parseMode = 0;
                        if (getFunctionIndex(name) >= 0 || findDefine(name) >= 0)
                            skipDepth = m_data.conditionalDepth;
                    }
                    continue;
                }

                if (std::strncmp(cur, "#endif", 6) == 0)
                {
                    setSourceCursorOffset(sourceCursorOffset() + 6);
                    if (skipDepth == m_data.conditionalDepth)
                        skipDepth = 0;
                    --m_data.conditionalDepth;
                    if (m_data.conditionalDepth < 0)
                        reportCompileError(10, "#endif without #ifdef", 0);
                    continue;
                }

                if (std::strncmp(cur, "#else", 5) == 0)
                {
                    setSourceCursorOffset(sourceCursorOffset() + 5);
                    if (skipDepth == 0)
                    {
                        if (m_data.conditionalDepth > 0)
                            skipDepth = m_data.conditionalDepth;
                    }
                    else if (skipDepth == m_data.conditionalDepth)
                        skipDepth = 0;

                    if (m_data.conditionalDepth <= 0)
                        reportCompileError(10, "#else without #ifdef", 0);
                    continue;
                }
            }

            if (c == '/')
            {
                const std::size_t nextOff = static_cast<std::size_t>(sourceCursorOffset() + 1);
                const unsigned char next = sourceStorage()[nextOff];
                if (next == '/')
                {
                    commentState = 1;
                    setSourceCursorOffset(sourceCursorOffset() + 1);
                    continue;
                }
                if (next == '*')
                {
                    commentState = 2;
                    setSourceCursorOffset(sourceCursorOffset() + 1);
                    continue;
                }
            }

            if (c == '?')
            {
                reportCompileError(10, "?: not supported in this version", 0);
                std::exit(1);
            }

            if (skipDepth == 0 && !isScriptWhitespace(c) && c != 0)
                return 0;

            setSourceCursorOffset(sourceCursorOffset() + 1);
            if (c == '\n')
                ++m_data.sourceLine;
        }

        if (m_data.conditionalDepth > 0)
            reportCompileError(10, "#ifdef without #endif", m_data.conditionalDepth);
        return 1;
    }

    int SCRIPT::requireSourceToken()
    {
        if (skipTriviaAndPreprocess())
        {
            reportCompileError(10, "End of file", 0);
            std::exit(1);
        }
        return 0;
    }

    int SCRIPT::readSourceLine(STRING& outLine)
    {


        if (requireSourceToken())
            return 1;

        char line[0x1000];
        int length = 0;
        for (;;)
        {
            const unsigned char c = sourceByteAtCursor();
            if (c == '\n' || c == '\r')
                break;
            if (length >= 0x0FFF)
            {
                reportCompileError(10, "Very long line", 0);
                std::exit(1);
            }
            line[length++] = static_cast<char>(c);
            setSourceCursorOffset(sourceCursorOffset() + 1);
        }
        line[length] = '\0';

        if (length == 0)
        {
            reportCompileError(10, "empty line", 0);
            std::exit(1);
        }

        if (char* const comment = std::strstr(line, "//"))
            *comment = '\0';

        int trimmed = static_cast<int>(std::strlen(line));
        while (trimmed > 0)
        {
            const unsigned char c = static_cast<unsigned char>(line[trimmed - 1]);
            if (c != ' ' && c != '\n' && c != '\r' && c != '\t')
                break;
            --trimmed;
        }
        line[trimmed] = '\0';

        outLine = STRING(line);
        return requireSourceToken();
    }

    int SCRIPT::readIdentifier(STRING& outName)
    {


        if (requireSourceToken())
            return 0;

        char name[0x1000];
        int length = 0;
        for (;;)
        {
            const unsigned char c = sourceByteAtCursor();
            if (!isIdentifierChar(c))
                break;
            if (length >= 0x0FFF)
            {
                reportCompileError(10, "Very long name", 0);
                std::exit(1);
            }
            name[length++] = static_cast<char>(c);
            setSourceCursorOffset(sourceCursorOffset() + 1);
        }
        name[length] = '\0';
        outName = STRING(name);
        if (length == 0)
        {
            reportCompileError(4, "name", 0);
            std::exit(1);
        }
        requireSourceToken();
        return length;
    }


    int SCRIPT::matchToken(const char* token)
    {
        const std::size_t len = std::strlen(token);
        requireSourceToken();
        const char* cur = reinterpret_cast<const char*>(sourceStorage() + sourceCursorOffset());
        if (std::strncmp(cur, token, len) != 0)
            return 0;
        const unsigned char first = static_cast<unsigned char>(token[0]);
        const unsigned char after = static_cast<unsigned char>(cur[len]);
        if ((std::isalpha(ctypeArgument(first)) != 0 || first == '#') &&
            isIdentifierChar(after))
            return 0;


        if (len == 1u && cur[1] == token[0] && std::strchr("-+|&=", token[0]) != nullptr)
            return 0;

        setSourceCursorOffset(sourceCursorOffset() + static_cast<int>(len));
        skipTriviaAndPreprocess();
        return 1;
    }


    int SCRIPT::requireToken(const char* token)
    {
        if (matchToken(token))
            return 1;
        reportCompileError(13, token, 0);
        std::exit(1);
    }

    int SCRIPT::parseConstantIntExpression()
    {
        requireSourceToken();
        const int bytecodeStart = m_data.bytecodeEnd;
        CompileExpression(1);

        if (bytecodeStorage()[static_cast<std::size_t>(bytecodeStart)] == 1 &&
            m_data.bytecodeEnd - bytecodeStart == 5)
        {
            int value = 0;
            std::memcpy(&value,
                bytecodeStorage() + static_cast<std::size_t>(bytecodeStart + 1),
                sizeof(value));
            m_data.bytecodeEnd -= 5;
            return value;
        }

        reportCompileError(4, "constant int value", 0);
        std::exit(1);
    }


    int SCRIPT::readQuotedStringLiteral(char* outText)
    {
        char* dst = outText;
        if (sourceByteAtCursor() != '"')
            return 0;

        setSourceCursorOffset(sourceCursorOffset() + 1);
        if (sourceByteAtCursor() != '"')
        {
            while (true)
            {
                if (sourceCursorOffset() >= sourceEndOffset())
                    break;

                unsigned char c = sourceByteAtCursor();
                if (c == '\\')
                {
                    const int nextOffset = sourceCursorOffset() + 1;
                    const unsigned char next = sourceStorage()[static_cast<std::size_t>(nextOffset)];

                    if (next == '\r')
                    {
                        const int afterCrOffset = sourceCursorOffset() + 2;
                        const unsigned char afterCr = sourceStorage()[static_cast<std::size_t>(afterCrOffset)];
                        if (afterCr == '\n')
                        {
                            setSourceCursorOffset(afterCrOffset);
                            ++m_data.sourceLine;
                        }
                        else
                        {
                            setSourceCursorOffset(nextOffset);
                            *dst++ = static_cast<char>(next);
                        }
                    }
                    else if (next == '\n')
                    {
                        setSourceCursorOffset(nextOffset);
                        ++m_data.sourceLine;
                    }
                    else if (next == 'n')
                    {
                        *dst++ = '\n';
                        setSourceCursorOffset(sourceCursorOffset() + 1);
                    }
                    else if (next == 'r')
                    {
                        *dst++ = '\r';
                        setSourceCursorOffset(sourceCursorOffset() + 1);
                    }
                    else
                    {
                        setSourceCursorOffset(nextOffset);
                        *dst++ = static_cast<char>(next);
                    }
                }
                else
                {
                    *dst++ = static_cast<char>(c);
                }

                setSourceCursorOffset(sourceCursorOffset() + 1);
                if (sourceByteAtCursor() == '"')
                    break;
            }
        }

        const int quoteOffset = sourceCursorOffset();
        setSourceCursorOffset(sourceCursorOffset() + 1);
        if (quoteOffset >= sourceEndOffset())
        {
            reportCompileError(10, "End of file", 0);
            std::exit(1);
        }

        *dst++ = '\0';
        return static_cast<int>(dst - outText);
    }


    int SCRIPT::setLastFunctionElementCount(int argCount)
    {


        functionRecordStorage()[static_cast<std::size_t>(m_data.functionCount - 1)].value2 = argCount;
        return m_data.functionCount * 3;
    }


    void SCRIPT::compileIntDeclaration(int declarationMode)
    {


        constexpr int UnsizedArrayCount = 999999;
        int declaredCount = 1;
        int rawFlags = 0;
        STRING name;

        requireSourceToken();
        if (sourceByteAtCursor() == '*')
        {
            setSourceCursorOffset(sourceCursorOffset() + 1);
            rawFlags = script::STACK_OBJECT_DYNAMIC;
        }

        readIdentifier(name);
        if (getFunctionIndex(name) >= 0)
        {
            char buffer[512];
            std::snprintf(buffer, sizeof(buffer), "int redefinition '%s'", name.c_str());
            reportCompileError(10, buffer, 0);
            std::exit(1);
        }

        const int stackBase = executionStackCount();

        if (std::strncmp(name.c_str(), "Action", 6) == 0)
        {
            const char* suffix = name.c_str() + 6;
            if (*suffix != '\0')
            {
                const bool numeric = std::isdigit(ctypeArgument(static_cast<unsigned char>(*suffix))) != 0 ||
                    (*suffix == '-' && std::isdigit(ctypeArgument(static_cast<unsigned char>(suffix[1]))) != 0);
                if (numeric)
                {
                    int action = 0;
                    if (suffix[1] == 'x')
                        std::sscanf(suffix, "%i", &action);
                    else
                        action = std::atoi(suffix);
                    if (static_cast<unsigned int>(action) < 256u)
                        m_data.actionN[static_cast<std::size_t>(action)] = stackBase;
                }
            }
        }
        appendFunctionRecord(
            name, 1, STRING(), stackBase, 0, 0);

        if (matchToken("["))
        {
            rawFlags |= script::STACK_OBJECT_ARRAY;
            if (sourceByteAtCursor() == ']')
                declaredCount = UnsizedArrayCount;
            else
                declaredCount = parseConstantIntExpression();
            requireToken("]");
        }

        auto appendUninitialized = [&](int flags) -> int
        {
            script::StackObject value;
            value.assignFields(static_cast<std::uint8_t>(flags | script::STACK_OBJECT_INT |
                                                          script::STACK_OBJECT_CHAR_WRITE),
                               0, STRING());
            appendExecutionStackObject(value);
            return executionStackCount() - 1;
        };

        auto appendConstant = [&](int flags, int value) -> int
        {
            script::StackObject stackValue;
            stackValue.assignFields(static_cast<std::uint8_t>(flags | script::STACK_OBJECT_INT |
                                                               script::STACK_OBJECT_HAS_PAYLOAD),
                                    value, STRING());
            appendExecutionStackObject(stackValue);
            return executionStackCount() - 1;
        };

        auto emitInitializer = [&](int slot)
        {
            CompileExpression(1);
            bytecodeStorage()[static_cast<std::size_t>(m_data.bytecodeEnd++)] =
                script::opcodeValue(script::VmOpcode::Assign);
            std::memcpy(bytecodeStorage() + static_cast<std::size_t>(m_data.bytecodeEnd),
                        &slot, sizeof(slot));
            m_data.bytecodeEnd += 4;
            bytecodeStorage()[static_cast<std::size_t>(m_data.bytecodeEnd++)] =
                script::opcodeValue(script::VmOpcode::StatementEnd);
            const int line = m_data.sourceLine;
            std::memcpy(bytecodeStorage() + static_cast<std::size_t>(m_data.bytecodeEnd),
                        &line, sizeof(line));
            m_data.bytecodeEnd += 4;
        };

        int initialized = 0;
        if (matchToken("="))
        {
            if ((rawFlags & script::STACK_OBJECT_ARRAY) != 0)
            {
                requireToken("{");
                for (;;)
                {
                    if (declaredCount != UnsizedArrayCount && initialized >= declaredCount)
                    {
                        reportCompileError(10, "too many initializers", 0);
                        std::exit(1);
                    }

                    if (declarationMode == 0)
                    {
                        appendUninitialized(rawFlags);
                        emitInitializer(stackBase + initialized);
                    }
                    else
                    {
                        const int value = parseConstantIntExpression();
                        appendConstant(rawFlags, value);
                    }
                    ++initialized;

                    if (!matchToken(","))
                        break;
                }
                requireToken("}");
                if (declaredCount == UnsizedArrayCount)
                    declaredCount = initialized;
            }
            else
            {
                if (declarationMode == 0)
                {
                    appendUninitialized(rawFlags);
                    emitInitializer(stackBase);
                }
                else
                {
                    const int value = parseConstantIntExpression();
                    appendConstant(rawFlags, value);
                }
                initialized = 1;
                declaredCount = 1;
            }
        }
        else if (declaredCount == UnsizedArrayCount)
        {
            reportCompileError(10, "for [] need initialisation", 0);
            std::exit(1);
        }

        for (int i = initialized; i < declaredCount; ++i)
            appendUninitialized(rawFlags);

        setLastFunctionElementCount(declaredCount);
    }


    void SCRIPT::compileStringDeclaration(int declarationMode)
    {

        constexpr int UnsizedArrayCount = 999999;
        int declaredCount = 1;
        int rawFlags = 0;
        STRING name;

        requireSourceToken();
        if (sourceByteAtCursor() == '*')
        {
            setSourceCursorOffset(sourceCursorOffset() + 1);
            rawFlags = script::STACK_OBJECT_DYNAMIC;
        }

        readIdentifier(name);
        if (getFunctionIndex(name) >= 0)
        {
            char buffer[512];
            std::snprintf(buffer, sizeof(buffer), "string redefinition '%s'", name.c_str());
            reportCompileError(10, buffer, 0);
            std::exit(1);
        }

        const int stackBase = executionStackCount();
        appendFunctionRecord(
            name, 1, STRING(), stackBase, 0, 0);

        if (matchToken("["))
        {
            rawFlags |= script::STACK_OBJECT_ARRAY;
            if (sourceByteAtCursor() == ']')
                declaredCount = UnsizedArrayCount;
            else
                declaredCount = parseConstantIntExpression();
            requireToken("]");
        }

        auto appendUninitialized = [&](int flags) -> int
        {
            script::StackObject value;
            value.assignFields(static_cast<std::uint8_t>(flags | script::STACK_OBJECT_STRING |
                                                          script::STACK_OBJECT_CHAR_WRITE),
                               0, STRING());
            appendExecutionStackObject(value);
            return executionStackCount() - 1;
        };

        auto appendConstant = [&](int flags, const STRING& text) -> int
        {
            script::StackObject value;
            value.assignFields(static_cast<std::uint8_t>(flags | script::STACK_OBJECT_STRING |
                                                          script::STACK_OBJECT_HAS_PAYLOAD),
                               0, text);
            appendExecutionStackObject(value);
            return executionStackCount() - 1;
        };

        auto parseConstantStringExpression = [&]() -> STRING
        {
            requireSourceToken();
            const int bytecodeStart = m_data.bytecodeEnd;
            CompileExpression(1);
            if (bytecodeStorage()[static_cast<std::size_t>(bytecodeStart)] !=
                script::opcodeValue(script::VmOpcode::PushString))
            {
                reportCompileError(4, "constant string value", 0);
                std::exit(1);
            }

            const char* const text = reinterpret_cast<const char*>(
                bytecodeStorage() + static_cast<std::size_t>(bytecodeStart + 1));
            const int encodedBytes = static_cast<int>(std::strlen(text)) + 1;
            if (m_data.bytecodeEnd - bytecodeStart != encodedBytes + 1)
            {
                reportCompileError(4, "constant string value", 0);
                std::exit(1);
            }

            STRING result(text);
            m_data.bytecodeEnd = bytecodeStart;
            return result;
        };

        auto emitInitializer = [&](int slot)
        {
            CompileExpression(1);
            bytecodeStorage()[static_cast<std::size_t>(m_data.bytecodeEnd++)] =
                script::opcodeValue(script::VmOpcode::Assign);
            std::memcpy(bytecodeStorage() + static_cast<std::size_t>(m_data.bytecodeEnd),
                        &slot, sizeof(slot));
            m_data.bytecodeEnd += 4;
            bytecodeStorage()[static_cast<std::size_t>(m_data.bytecodeEnd++)] =
                script::opcodeValue(script::VmOpcode::StatementEnd);
            const int line = m_data.sourceLine;
            std::memcpy(bytecodeStorage() + static_cast<std::size_t>(m_data.bytecodeEnd),
                        &line, sizeof(line));
            m_data.bytecodeEnd += 4;
        };

        int initialized = 0;
        if (matchToken("="))
        {
            if ((rawFlags & script::STACK_OBJECT_ARRAY) != 0)
            {
                requireToken("{");
                for (;;)
                {
                    if (declaredCount != UnsizedArrayCount && initialized >= declaredCount)
                    {
                        reportCompileError(10, "too many initializers", 0);
                        std::exit(1);
                    }

                    if (declarationMode == 0)
                    {
                        appendUninitialized(rawFlags);
                        emitInitializer(stackBase + initialized);
                    }
                    else
                    {
                        const STRING value = parseConstantStringExpression();
                        appendConstant(rawFlags, value);
                    }
                    ++initialized;

                    if (!matchToken(","))
                        break;
                }
                requireToken("}");
                if (declaredCount == UnsizedArrayCount)
                    declaredCount = initialized;
            }
            else
            {
                if (declarationMode == 0)
                {
                    appendUninitialized(rawFlags);
                    emitInitializer(stackBase);
                }
                else
                {
                    const STRING value = parseConstantStringExpression();
                    appendConstant(rawFlags, value);
                }
                initialized = 1;
                declaredCount = 1;
            }
        }
        else if (declaredCount == UnsizedArrayCount)
        {
            reportCompileError(10, "for [] need initialisation", 0);
            std::exit(1);
        }

        for (int i = initialized; i < declaredCount; ++i)
            appendUninitialized(rawFlags);

        setLastFunctionElementCount(declaredCount);
    }


    int SCRIPT::mnog()
    {
        std::uint8_t unaryOpcode = 0;
        std::uint8_t prefixOpcode = script::opcodeValue(script::VmOpcode::ReadVariable);
        std::uint8_t castOpcode = 0;


        if (matchToken("(int)"))
            castOpcode = script::opcodeValue(script::VmOpcode::ConvertToInteger);
        else if (matchToken("(string)"))
            castOpcode = script::opcodeValue(script::VmOpcode::ConvertToString);
        else if (matchToken("(sprite)"))
            castOpcode = script::opcodeValue(script::VmOpcode::ConvertToObject);

        const int cursorBeforeUnary = sourceCursorOffset();
        const bool nextIsMinus =
            sourceStorage()[static_cast<std::size_t>(cursorBeforeUnary + 1)] == '-';
        const bool nextIsPlus =
            sourceStorage()[static_cast<std::size_t>(cursorBeforeUnary + 1)] == '+';
        if (!nextIsMinus && matchToken("-"))
            unaryOpcode = script::opcodeValue(script::VmOpcode::Negate);
        else if (!nextIsPlus && matchToken("+"))
            unaryOpcode = 0;
        else if (matchToken("~"))
            unaryOpcode = script::opcodeValue(script::VmOpcode::BitwiseNot);
        else if (matchToken("!"))
            unaryOpcode = script::opcodeValue(script::VmOpcode::LogicalNot);

        if (matchToken("--"))
            prefixOpcode = script::opcodeValue(script::VmOpcode::PreDecrement);
        else if (matchToken("++"))
            prefixOpcode = script::opcodeValue(script::VmOpcode::PreIncrement);
        else if (matchToken("&"))
            prefixOpcode = script::opcodeValue(script::VmOpcode::AddressOf);

        auto canWrite = [&](int byteCount) -> bool
        {
            (void)byteCount;
            return true;
        };
        auto emitByte = [&](std::uint8_t value) -> bool
        {
            if (m_data.compileError != 0)
                return false;
            EmitByteIfNoError(value);
            return true;
        };
        auto emitIntObject = [&](int value) -> bool
        {
            const std::size_t off = static_cast<std::size_t>(m_data.bytecodeEnd);
            bytecodeStorage()[off] = script::opcodeValue(script::VmOpcode::PushInteger);
            std::memcpy(bytecodeStorage() + off + 1, &value, sizeof(value));
            m_data.bytecodeEnd += 5;
            return true;
        };
        auto emitIntPayload = [&](int value) -> bool
        {
            const std::size_t off = static_cast<std::size_t>(m_data.bytecodeEnd);
            std::memcpy(bytecodeStorage() + off, &value, sizeof(value));
            m_data.bytecodeEnd += 4;
            return true;
        };
        auto emitByteAndIntPayload = [&](std::uint8_t opcode, int value) -> bool
        {
            if (m_data.compileError != 0)
                return false;
            EmitByteInt32(opcode, value);
            return true;
        };
        auto emitTrailingUnary = [&]() -> bool
        {

            if (unaryOpcode != 0 && !emitByte(static_cast<std::uint8_t>(unaryOpcode)))
                return false;
            if (castOpcode != 0 && !emitByte(static_cast<std::uint8_t>(castOpcode)))
                return false;
            return true;
        };


        const unsigned char current = sourceByteAtCursor();
        if (std::isdigit(current))
        {
            STRING token;
            readIdentifier(token);
            int value = 0;
            std::sscanf(token.c_str(), "%i", &value);
            if (unaryOpcode == script::opcodeValue(script::VmOpcode::Negate))
            {
                value = -value;
                unaryOpcode = 0;
            }
            else if (unaryOpcode == script::opcodeValue(script::VmOpcode::BitwiseNot))
            {
                value = ~value;
                unaryOpcode = 0;
            }
            else if (unaryOpcode == script::opcodeValue(script::VmOpcode::LogicalNot))
            {
                value = value ? 0 : 1;
                unaryOpcode = 0;
            }
            if (!emitIntObject(value))
                return 1;
            return emitTrailingUnary() ? 0 : 1;
        }

        if (current == '"')
        {
            if (!emitByte(script::opcodeValue(script::VmOpcode::PushString)))
                return 1;
            const std::size_t outOff = static_cast<std::size_t>(m_data.bytecodeEnd);
            const int written = readQuotedStringLiteral(reinterpret_cast<char*>(bytecodeStorage() + outOff));
            m_data.bytecodeEnd += written;
            return emitTrailingUnary() ? 0 : 1;
        }

        if (current == '\'')
        {
            setSourceCursorOffset(sourceCursorOffset() + 1);
            const signed char ch = static_cast<signed char>(sourceByteAtCursor());
            setSourceCursorOffset(sourceCursorOffset() + 1);
            if (!emitIntObject(static_cast<int>(ch)))
                return 1;
            if (sourceByteAtCursor() != '\'')
            {
                reportCompileError(13, "second '", 0);
                std::exit(1);
            }
            setSourceCursorOffset(sourceCursorOffset() + 1);
            return emitTrailingUnary() ? 0 : 1;
        }

        if (matchToken("sizeof"))
        {
            if (!matchToken("("))
            {
                reportCompileError(13, "'(' for sizeof", 0);
                std::exit(1);
            }

            if (!emitByte(script::opcodeValue(script::VmOpcode::PushInteger)))
                return 1;

            int sizeofValue = 4;
            if (!matchToken("int") && !matchToken("string"))
            {
                STRING sizeofName;
                readIdentifier(sizeofName);
                int foundIndex = -1;
                const int count = functionCount();
                const script::LogicFunctionRecord* const records = functionRecordStorage();
                for (int i = count - 1; i >= 0; --i)
                {
                    if (std::strcmp(records[static_cast<std::size_t>(i)].name.c_str(), sizeofName.c_str()) == 0)
                    {
                        foundIndex = i;
                        break;
                    }
                }
                if (foundIndex < 0 || records[static_cast<std::size_t>(foundIndex)].flags != 1)
                {
                    reportCompileError(4, "sizeof parameter", 0);
                    std::exit(1);
                }
                sizeofValue = records[static_cast<std::size_t>(foundIndex)].value2 << 2;
            }

            if (!emitIntPayload(sizeofValue))
                return 1;
            requireToken(")");
            return emitTrailingUnary() ? 0 : 1;
        }

        if (matchToken("static"))
        {
            if (matchToken("int"))
            {
                do
                {
                    compileIntDeclaration(1);
                }
                while (matchToken(","));
                return emitTrailingUnary() ? 0 : 1;
            }
            if (matchToken("string"))
            {
                do
                {
                    compileStringDeclaration(1);
                }
                while (matchToken(","));
                return emitTrailingUnary() ? 0 : 1;
            }
            reportCompileError(4, "static variable", 0);
            std::exit(1);
        }

        if (matchToken("int"))
        {
            do
            {
                compileIntDeclaration(0);
            }
            while (matchToken(","));
            return emitTrailingUnary() ? 0 : 1;
        }

        if (matchToken("string"))
        {
            do
            {
                compileStringDeclaration(0);
            }
            while (matchToken(","));
            return emitTrailingUnary() ? 0 : 1;
        }

        if (matchToken("return"))
        {
            CompileExpression(1);
            if (!emitByte(script::opcodeValue(script::VmOpcode::Return)))
                return 1;


            if (std::uint8_t* const functionFlags = reinterpret_cast<std::uint8_t*>(
                    static_cast<std::uintptr_t>(m_data.currentSymbolToken)))
                functionFlags[1] = static_cast<std::uint8_t>(functionFlags[1] | 0x02u);
            return emitTrailingUnary() ? 0 : 1;
        }

        if (matchToken("("))
        {
            CompileExpression(m_data.parseContext);
            requireToken(")");
            return emitTrailingUnary() ? 0 : 1;
        }

        if (std::isalpha(current))
        {
            auto sourceCursorChar = [&]() -> int
            {
                if (sourceCursorOffset() < 0 || sourceCursorOffset() >= sourceEndOffset())
                    return -1;
                return sourceByteAtCursor();
            };
            auto emitFormattedPrimaryDiagnostic = [&](int code, const char* fmt, const STRING& name)
            {
                char buffer[512];
                std::snprintf(buffer, sizeof(buffer), fmt, name.c_str());
                reportCompileError(code, buffer, 0);
                std::exit(1);
            };

            STRING name;
            readIdentifier(name);

            const int foundIndex = getFunctionIndex(name);
            if (foundIndex < 0)
            {
                if (sourceCursorChar() == ':')
                {
                    setSourceCursorOffset(sourceCursorOffset() + 1);
                    appendFunctionRecord(name, 7, STRING(), m_data.bytecodeEnd, 0, 0);
                    return mnog();
                }
                emitFormattedPrimaryDiagnostic(10, "Undeclared identifier '%s'", name);
            }

            const script::LogicFunctionRecord* const records = functionRecordStorage();
            const std::uint8_t flags = records[static_cast<std::size_t>(foundIndex)].flags;
            if (flags == 8)
            {
                if (sourceCursorChar() != ':')
                    emitFormattedPrimaryDiagnostic(10, "Incorrect use label '%s'", name);
                setSourceCursorOffset(sourceCursorOffset() + 1);
                int patchOffset = 0;
                script::LogicFunctionRecord* const label = mutableFunctionRecordAt(foundIndex);
                if (label)
                {
                    patchOffset = label->value0;
                    label->flags = 7;
                    label->value0 = m_data.bytecodeEnd;
                    const int delta = m_data.bytecodeEnd - patchOffset;
                    std::memcpy(bytecodeStorage() + patchOffset, &delta, sizeof(delta));
                }
                return mnog();
            }

            if (flags == 7)
            {
                if (sourceCursorChar() == ':')
                    emitFormattedPrimaryDiagnostic(10, "Label redefinition '%s'", name);
                emitFormattedPrimaryDiagnostic(10, "Incorrect use label '%s'", name);
            }

            if (flags == 2)
            {
                const int parameterBase = records[static_cast<std::size_t>(foundIndex)].value1;
                const int parameterCount = records[static_cast<std::size_t>(foundIndex)].value2;
                const int externOpcode = records[static_cast<std::size_t>(foundIndex)].value0;

                requireToken("(");
                int parsedCount = 0;
                if (!matchToken(")"))
                {
                    for (;;)
                    {
                        const script::StackObject* const parameterRecord =
                            mutableExecutionStackStorageAt(parameterBase + parsedCount);
                        if ((parameterRecord->flags & script::STACK_OBJECT_DYNAMIC) != 0)
                            g_scriptCompileDynamicArgument = 1;
                        CompileExpression(1);
                        matchToken(",");
                        g_scriptCompileDynamicArgument = 0;
                        ++parsedCount;
                        if (matchToken(")"))
                            break;
                    }
                }

                while (parsedCount < parameterCount)
                {
                    const script::StackObject* defaultRecord = mutableExecutionStackStorageAt(parameterBase + parsedCount);
                    if ((defaultRecord->flags & script::STACK_OBJECT_HAS_PAYLOAD) == 0)
                        break;
                    if (!emitByteAndIntPayload(script::opcodeValue(script::VmOpcode::ReadVariable), parameterBase + parsedCount))
                        return 1;
                    ++parsedCount;
                }

                if (parsedCount != parameterCount)
                {
                    reportCompileError(4, "extern function parameters number", 0);
                    std::exit(1);
                }

                if (!emitByte(static_cast<std::uint8_t>(externOpcode)))
                    return 1;
                return emitTrailingUnary() ? 0 : 1;
            }

            if (flags == 3)
            {
                const int parameterBase = records[static_cast<std::size_t>(foundIndex)].value1;
                const int parameterCount = records[static_cast<std::size_t>(foundIndex)].value2;

                requireToken("(");
                int parsedCount = 0;
                if (!matchToken(")"))
                {
                    for (;;)
                    {
                        const script::StackObject* const parameterRecord =
                            mutableExecutionStackStorageAt(parameterBase + parsedCount);
                        if ((parameterRecord->flags & script::STACK_OBJECT_DYNAMIC) != 0)
                            g_scriptCompileDynamicArgument = 1;
                        CompileExpression(1);
                        matchToken(",");
                        g_scriptCompileDynamicArgument = 0;
                        if (!emitByteAndIntPayload(script::opcodeValue(script::VmOpcode::Assign), parameterBase + parsedCount))
                            return 1;
                        if (!emitByte(script::opcodeValue(script::VmOpcode::Pop)))
                            return 1;
                        ++parsedCount;
                        if (matchToken(")"))
                            break;
                    }
                }

                while (parsedCount < parameterCount)
                {
                    const script::StackObject* defaultRecord = mutableExecutionStackStorageAt(parameterBase + parsedCount);
                    if ((defaultRecord->flags & script::STACK_OBJECT_HAS_PAYLOAD) == 0)
                        break;

                    if ((defaultRecord->flags & script::STACK_OBJECT_STRING) != 0)
                    {
                        const std::size_t textLen = std::strlen(defaultRecord->text.c_str()) + 1;
                        if (!emitByte(script::opcodeValue(script::VmOpcode::PushString)))
                            return 1;
                        if (!canWrite(static_cast<int>(textLen)))
                            return 1;
                        std::memcpy(bytecodeStorage() + static_cast<std::size_t>(m_data.bytecodeEnd),
                            defaultRecord->text.c_str(), textLen);
                        m_data.bytecodeEnd += static_cast<int>(textLen);
                    }
                    else
                    {
                        if (!emitIntObject(defaultRecord->intValue))
                            return 1;
                    }

                    if (!emitByteAndIntPayload(script::opcodeValue(script::VmOpcode::Assign), parameterBase + parsedCount))
                        return 1;
                    if (!emitByte(script::opcodeValue(script::VmOpcode::Pop)))
                        return 1;
                    ++parsedCount;
                }

                if (parsedCount != parameterCount)
                {
                    reportCompileError(4, "function parameters number", 0);
                    std::exit(1);
                }


                if (!emitByteAndIntPayload(script::opcodeValue(script::VmOpcode::CallScriptFunction), foundIndex))
                    return 1;


                if (m_data.parseContext != 0)
                {
                    if (script::LogicFunctionRecord* const callee = mutableFunctionRecordAt(foundIndex))
                        callee->statusFlags = static_cast<std::uint8_t>(callee->statusFlags | 0x01u);
                }
                return emitTrailingUnary() ? 0 : 1;
            }

            if (flags == 4)
            {
                const STRING& textValue = records[static_cast<std::size_t>(foundIndex)].text;
                const std::size_t textLen = std::strlen(textValue.c_str()) + 1;
                if (!emitByte(script::opcodeValue(script::VmOpcode::PushString)))
                    return 1;
                if (!canWrite(static_cast<int>(textLen)))
                    return 1;
                std::memcpy(bytecodeStorage() + static_cast<std::size_t>(m_data.bytecodeEnd),
                    textValue.c_str(), textLen);
                m_data.bytecodeEnd += static_cast<int>(textLen);
                return emitTrailingUnary() ? 0 : 1;
            }

            if (flags == 5)
            {
                const int value = records[static_cast<std::size_t>(foundIndex)].value0;
                if (!emitIntObject(value))
                    return 1;
                return emitTrailingUnary() ? 0 : 1;
            }

            if (flags == 1)
            {
                const int stackIndex = records[static_cast<std::size_t>(foundIndex)].value0;
                const script::StackObject* stackRecord = mutableExecutionStackStorageAt(stackIndex);

                int indexBytecodeStart = 0;
                int savedIndexBytecodeSize = 0;
                std::uint8_t* savedIndexBytecode = nullptr;
                if (matchToken("["))
                {
                    indexBytecodeStart = m_data.bytecodeEnd;
                    if ((stackRecord->flags & (script::STACK_OBJECT_STRING |
                                               script::STACK_OBJECT_ARRAY |
                                               script::STACK_OBJECT_DYNAMIC)) == 0)
                    {
                        reportCompileError(10, "[] for not array", 0);
                        std::exit(1);
                    }

                    CompileExpression(1);
                    if (!emitByte(script::opcodeValue(script::VmOpcode::ArrayIndex)))
                        return 1;
                    savedIndexBytecodeSize = m_data.bytecodeEnd - indexBytecodeStart;
                    savedIndexBytecode = static_cast<std::uint8_t*>(
                        ::operator new(static_cast<std::size_t>(savedIndexBytecodeSize)));
                    std::memcpy(savedIndexBytecode,
                        bytecodeStorage() + static_cast<std::size_t>(indexBytecodeStart),
                        static_cast<std::size_t>(savedIndexBytecodeSize));
                    m_data.bytecodeEnd = indexBytecodeStart;
                    requireToken("]");
                }

                std::uint8_t assignmentOpcode = prefixOpcode;
                script::BinaryCommand compoundCommand = script::BinaryCommand::Add;
                auto sourceCharAtOffset = [&](int delta) -> int
                {
                    const int pos = sourceCursorOffset() + delta;
                    return sourceStorage()[static_cast<std::size_t>(pos)];
                };

                if (sourceCharAtOffset(1) != '=' && matchToken("="))
                {
                    CompileExpression(1);
                    assignmentOpcode = script::opcodeValue(script::VmOpcode::Assign);
                }
                else if (matchToken("+="))
                {
                    CompileExpression(1);
                    assignmentOpcode = script::opcodeValue(script::VmOpcode::CompoundAssign);
                    compoundCommand = script::BinaryCommand::Add;
                }
                else if (matchToken("-="))
                {
                    CompileExpression(1);
                    assignmentOpcode = script::opcodeValue(script::VmOpcode::CompoundAssign);
                    compoundCommand = script::BinaryCommand::Subtract;
                }
                else if (matchToken("/="))
                {
                    CompileExpression(1);
                    assignmentOpcode = script::opcodeValue(script::VmOpcode::CompoundAssign);
                    compoundCommand = script::BinaryCommand::Divide;
                }
                else if (matchToken("*="))
                {
                    CompileExpression(1);
                    assignmentOpcode = script::opcodeValue(script::VmOpcode::CompoundAssign);
                    compoundCommand = script::BinaryCommand::Multiply;
                }
                else if (matchToken("%="))
                {
                    CompileExpression(1);
                    assignmentOpcode = script::opcodeValue(script::VmOpcode::CompoundAssign);
                    compoundCommand = script::BinaryCommand::Modulo;
                }
                else if (matchToken("&="))
                {
                    CompileExpression(1);
                    assignmentOpcode = script::opcodeValue(script::VmOpcode::CompoundAssign);
                    compoundCommand = script::BinaryCommand::BitwiseAnd;
                }
                else if (matchToken("|="))
                {
                    CompileExpression(1);
                    assignmentOpcode = script::opcodeValue(script::VmOpcode::CompoundAssign);
                    compoundCommand = script::BinaryCommand::BitwiseOr;
                }
                else if (matchToken("^="))
                {
                    CompileExpression(1);
                    assignmentOpcode = script::opcodeValue(script::VmOpcode::CompoundAssign);
                    compoundCommand = script::BinaryCommand::BitwiseXor;
                }
                else if (matchToken("<<="))
                {
                    CompileExpression(1);
                    assignmentOpcode = script::opcodeValue(script::VmOpcode::CompoundAssign);
                    compoundCommand = script::BinaryCommand::ShiftLeft;
                }
                else if (matchToken(">>="))
                {
                    CompileExpression(1);
                    assignmentOpcode = script::opcodeValue(script::VmOpcode::CompoundAssign);
                    compoundCommand = script::BinaryCommand::ShiftRight;
                }
                else if (matchToken("++"))
                {
                    assignmentOpcode = script::opcodeValue(script::VmOpcode::PostIncrement);
                }
                else if (matchToken("--"))
                {
                    assignmentOpcode = script::opcodeValue(script::VmOpcode::PostDecrement);
                }

                if (savedIndexBytecode)
                {
                    std::memcpy(bytecodeStorage() + static_cast<std::size_t>(m_data.bytecodeEnd),
                        savedIndexBytecode, static_cast<std::size_t>(savedIndexBytecodeSize));
                    m_data.bytecodeEnd += savedIndexBytecodeSize;
                    ::operator delete(savedIndexBytecode);
                    savedIndexBytecode = nullptr;
                }
                else if ((stackRecord->flags & (script::STACK_OBJECT_ARRAY | script::STACK_OBJECT_DYNAMIC)) != 0)
                {
                    if (assignmentOpcode == script::opcodeValue(script::VmOpcode::PostIncrement) ||
                        assignmentOpcode == script::opcodeValue(script::VmOpcode::PostDecrement) ||
                        assignmentOpcode == script::opcodeValue(script::VmOpcode::PreIncrement) ||
                        assignmentOpcode == script::opcodeValue(script::VmOpcode::PreDecrement))
                    {
                        reportCompileError(10, "Increment or decrement for array", 0);
                        std::exit(1);
                    }
                    if (assignmentOpcode != script::opcodeValue(script::VmOpcode::ReadVariable) ||
                        g_scriptCompileDynamicArgument == 0)
                    {
                        reportCompileError(4, "operation for array", assignmentOpcode);
                        std::exit(1);
                    }
                }

                if (!emitByteAndIntPayload(static_cast<std::uint8_t>(assignmentOpcode), stackIndex))
                    return 1;
                if (assignmentOpcode == script::opcodeValue(script::VmOpcode::CompoundAssign) && !emitByte(script::opcodeValue(compoundCommand)))
                    return 1;
                return emitTrailingUnary() ? 0 : 1;
            }

            return emitTrailingUnary() ? 0 : 1;
        }

        if (unaryOpcode != 0)
        {
            reportCompileError(10, "error symbol", 0);
            std::exit(1);
        }
        return 0;
    }


    void SCRIPT::SetOperation(int byteCodePos, int operation)
    {
        if (m_data.compileError != 0)
            return;

        std::uint8_t* const code = bytecodeStorage();
        const std::size_t start = static_cast<std::size_t>(byteCodePos);
        const bool canReadTwoConstants =
            code[start] == script::opcodeValue(script::VmOpcode::PushInteger) &&
            code[start + 5] == script::opcodeValue(script::VmOpcode::PushInteger) &&
            m_data.bytecodeEnd - byteCodePos == 10;

        if (canReadTwoConstants)
        {
            int lhs = 0;
            int rhs = 0;
            std::memcpy(&lhs, code + start + 1, sizeof(lhs));
            std::memcpy(&rhs, code + start + 6, sizeof(rhs));

            bool hasFoldedValue = true;
            int folded = lhs;
            switch (static_cast<script::BinaryCommand>(operation))
            {
            case script::BinaryCommand::Multiply:
                folded = lhs * rhs;
                break;
            case script::BinaryCommand::Divide:
                folded = lhs / rhs;
                break;
            case script::BinaryCommand::Modulo:
                folded = lhs % rhs;
                break;
            case script::BinaryCommand::Add:
                folded = lhs + rhs;
                break;
            case script::BinaryCommand::Subtract:
                folded = lhs - rhs;
                break;
            case script::BinaryCommand::ShiftRight:
                folded = lhs >> (rhs & 31);
                break;
            case script::BinaryCommand::ShiftLeft:
                folded = lhs << (rhs & 31);
                break;
            case script::BinaryCommand::BitwiseXor:
                folded = lhs ^ rhs;
                break;
            case script::BinaryCommand::BitwiseAnd:
                folded = lhs & rhs;
                break;
            case script::BinaryCommand::BitwiseOr:
                folded = lhs | rhs;
                break;
            default:
                hasFoldedValue = false;
                break;
            }

            if (hasFoldedValue)
                std::memcpy(code + start + 1, &folded, sizeof(folded));


            m_data.bytecodeEnd -= 5;
            return;
        }

        if (code[start] == script::opcodeValue(script::VmOpcode::PushString))
        {
            const std::size_t leftLength =
                std::strlen(reinterpret_cast<const char*>(code + start + 1));
            const int secondMarker =
                byteCodePos + static_cast<int>(leftLength) + 2;
            if (code[static_cast<std::size_t>(secondMarker)] ==
                script::opcodeValue(script::VmOpcode::PushString))
            {
                char* const second = reinterpret_cast<char*>(
                    code + static_cast<std::size_t>(secondMarker + 1));
                const std::size_t rightLength = std::strlen(second);
                const int encodedSize = static_cast<int>(leftLength + rightLength) + 4;
                if (m_data.bytecodeEnd - byteCodePos == encodedSize)
                {
                    if (operation == script::opcodeValue(script::BinaryCommand::Add))
                    {
                        std::memmove(code + start + leftLength + 1,
                                     second,
                                     rightLength + 1);
                    }
                    m_data.bytecodeEnd -= 2;
                    return;
                }
            }
        }

        EmitByteIfNoError(static_cast<std::uint8_t>(operation));
    }


    void SCRIPT::slag()
    {
        const int bytecodeStart = m_data.bytecodeEnd;
        mnog();
        for (;;)
        {
            if (matchToken("*"))
            {
                mnog();
                SetOperation(bytecodeStart, script::opcodeValue(script::BinaryCommand::Multiply));
                continue;
            }
            if (matchToken("/"))
            {
                mnog();
                SetOperation(bytecodeStart, script::opcodeValue(script::BinaryCommand::Divide));
                continue;
            }
            if (matchToken("%"))
            {
                mnog();
                SetOperation(bytecodeStart, script::opcodeValue(script::BinaryCommand::Modulo));
                continue;
            }
            break;
        }
    }


    void SCRIPT::cmpslag()
    {
        const int bytecodeStart = m_data.bytecodeEnd;
        slag();
        for (;;)
        {
            if (matchToken("+"))
            {
                slag();
                SetOperation(bytecodeStart, script::opcodeValue(script::BinaryCommand::Add));
                continue;
            }
            if (matchToken("-"))
            {
                slag();
                SetOperation(bytecodeStart, script::opcodeValue(script::BinaryCommand::Subtract));
                continue;
            }
            break;
        }
    }


    void SCRIPT::logicslag()
    {
        const int bytecodeStart = m_data.bytecodeEnd;
        cmpslag();
        for (;;)
        {
            if (matchToken(">="))
            {
                cmpslag();
                bytecodeStorage()[static_cast<std::size_t>(m_data.bytecodeEnd++)] = script::opcodeValue(script::BinaryCommand::GreaterEqual);
                continue;
            }
            if (matchToken(">>"))
            {
                cmpslag();
                SetOperation(bytecodeStart, script::opcodeValue(script::BinaryCommand::ShiftRight));
                continue;
            }
            if (matchToken(">"))
            {
                cmpslag();
                bytecodeStorage()[static_cast<std::size_t>(m_data.bytecodeEnd++)] = script::opcodeValue(script::BinaryCommand::Greater);
                continue;
            }
            if (matchToken("<="))
            {
                cmpslag();
                bytecodeStorage()[static_cast<std::size_t>(m_data.bytecodeEnd++)] = script::opcodeValue(script::BinaryCommand::LessEqual);
                continue;
            }
            if (matchToken("<<"))
            {
                cmpslag();
                SetOperation(bytecodeStart, script::opcodeValue(script::BinaryCommand::ShiftLeft));
                continue;
            }
            if (matchToken("<"))
            {
                cmpslag();
                bytecodeStorage()[static_cast<std::size_t>(m_data.bytecodeEnd++)] = script::opcodeValue(script::BinaryCommand::Less);
                continue;
            }
            if (matchToken("=="))
            {
                cmpslag();
                bytecodeStorage()[static_cast<std::size_t>(m_data.bytecodeEnd++)] = script::opcodeValue(script::BinaryCommand::Equal);
                continue;
            }
            if (matchToken("!="))
            {
                cmpslag();
                bytecodeStorage()[static_cast<std::size_t>(m_data.bytecodeEnd++)] = script::opcodeValue(script::BinaryCommand::NotEqual);
                continue;
            }
            break;
        }
    }


    void SCRIPT::vyragAnd()
    {
        const int bytecodeStart = m_data.bytecodeEnd;
        logicslag();
        for (;;)
        {
            requireSourceToken();
            const int cursor = sourceCursorOffset();
            if (sourceStorage()[static_cast<std::size_t>(cursor)] == '&' &&
                sourceStorage()[static_cast<std::size_t>(cursor + 1)] == '&')
                break;
            if (!matchToken("&"))
                break;
            logicslag();
            SetOperation(bytecodeStart, script::opcodeValue(script::BinaryCommand::BitwiseAnd));
        }
    }


    void SCRIPT::vyragXor()
    {
        const int bytecodeStart = m_data.bytecodeEnd;
        vyragAnd();
        while (matchToken("^"))
        {
            vyragAnd();
            SetOperation(bytecodeStart, script::opcodeValue(script::BinaryCommand::BitwiseXor));
        }
    }


    void SCRIPT::vyragOr()
    {
        const int bytecodeStart = m_data.bytecodeEnd;
        vyragXor();
        for (;;)
        {
            requireSourceToken();
            const int cursor = sourceCursorOffset();
            if (sourceStorage()[static_cast<std::size_t>(cursor)] == '|' &&
                sourceStorage()[static_cast<std::size_t>(cursor + 1)] == '|')
                break;
            if (!matchToken("|"))
                break;
            vyragXor();
            SetOperation(bytecodeStart, script::opcodeValue(script::BinaryCommand::BitwiseOr));
        }
    }


    void SCRIPT::vyragCmpAnd()
    {
        vyragOr();
        while (matchToken("&&"))
        {


            bytecodeStorage()[static_cast<std::size_t>(m_data.bytecodeEnd++)] =
                script::opcodeValue(script::VmOpcode::BranchIfTrue);
            const int patchOffset = m_data.bytecodeEnd;
            m_data.bytecodeEnd += 4;

            vyragOr();
            bytecodeStorage()[static_cast<std::size_t>(m_data.bytecodeEnd++)] =
                script::opcodeValue(script::BinaryCommand::LogicalAnd);
            const int delta = m_data.bytecodeEnd - patchOffset;
            std::memcpy(bytecodeStorage() + static_cast<std::size_t>(patchOffset), &delta, sizeof(delta));
        }
    }


    void SCRIPT::CompileExpression(int parseContext)
    {


        const int savedParseContext = m_data.parseContext;
        m_data.parseContext = parseContext;
        vyragCmpAnd();
        while (matchToken("||"))
        {


            bytecodeStorage()[static_cast<std::size_t>(m_data.bytecodeEnd++)] =
                script::opcodeValue(script::VmOpcode::BranchIfFalse);
            const int patchOffset = m_data.bytecodeEnd;
            m_data.bytecodeEnd += 4;

            vyragCmpAnd();
            bytecodeStorage()[static_cast<std::size_t>(m_data.bytecodeEnd++)] =
                script::opcodeValue(script::BinaryCommand::LogicalOr);
            const int delta = m_data.bytecodeEnd - patchOffset;
            std::memcpy(bytecodeStorage() + static_cast<std::size_t>(patchOffset), &delta, sizeof(delta));
        }
        m_data.parseContext = savedParseContext;
    }


    int SCRIPT::vyrag_oper()
    {
        CompileExpression(0);
        while (m_data.compileError == 0)
        {
            EmitByteInt32(script::opcodeValue(script::VmOpcode::StatementEnd), m_data.sourceLine + 1);
            if (!matchToken(","))
                return 0;
            CompileExpression(0);
        }
        return m_data.compileError;
    }

    void SCRIPT::compileStatement(std::int32_t* breakPatchList)
    {
        const int iffMatched = matchToken("iff");
        if (iffMatched || matchToken("if"))
        {
            requireToken("(");
            CompileExpression(1);
            requireToken(")");

            const script::VmOpcode branchOpcode = iffMatched
                ? script::VmOpcode::IfFalseChain
                : script::VmOpcode::If;
            bytecodeStorage()[static_cast<std::size_t>(m_data.bytecodeEnd++)] = script::opcodeValue(branchOpcode);
            const int firstPatchOffset = m_data.bytecodeEnd;
            *reinterpret_cast<std::int32_t*>(bytecodeStorage() + static_cast<std::size_t>(m_data.bytecodeEnd)) = (0);
            m_data.bytecodeEnd += 4;

            compileStatement(breakPatchList);
            *reinterpret_cast<std::int32_t*>(bytecodeStorage() + static_cast<std::size_t>(firstPatchOffset)) = (m_data.bytecodeEnd - firstPatchOffset);

            if (!matchToken("else"))
                return;

            *reinterpret_cast<std::int32_t*>(bytecodeStorage() + static_cast<std::size_t>(firstPatchOffset)) = (m_data.bytecodeEnd - firstPatchOffset + 5);
            bytecodeStorage()[static_cast<std::size_t>(m_data.bytecodeEnd++)] = script::opcodeValue(script::VmOpcode::Jump);
            const int elsePatchOffset = m_data.bytecodeEnd;
            *reinterpret_cast<std::int32_t*>(bytecodeStorage() + static_cast<std::size_t>(m_data.bytecodeEnd)) = (0);
            m_data.bytecodeEnd += 4;

            compileStatement(breakPatchList);
            *reinterpret_cast<std::int32_t*>(bytecodeStorage() + static_cast<std::size_t>(elsePatchOffset)) = (m_data.bytecodeEnd - elsePatchOffset);
            return;
        }

        if (matchToken("while"))
        {
            std::int32_t localBreakPatchList[0x80] = {};
            requireToken("(");
            const int loopConditionStart = m_data.bytecodeEnd;
            CompileExpression(1);
            requireToken(")");
            bytecodeStorage()[static_cast<std::size_t>(m_data.bytecodeEnd++)] = script::opcodeValue(script::VmOpcode::If);
            const int conditionPatchOffset = m_data.bytecodeEnd;
            *reinterpret_cast<std::int32_t*>(bytecodeStorage() + static_cast<std::size_t>(m_data.bytecodeEnd)) = (0);
            m_data.bytecodeEnd += 4;

            compileStatement(localBreakPatchList);
            bytecodeStorage()[static_cast<std::size_t>(m_data.bytecodeEnd++)] = script::opcodeValue(script::VmOpcode::Jump);
            const int jumpBackPatchOffset = m_data.bytecodeEnd;
            *reinterpret_cast<std::int32_t*>(bytecodeStorage() + static_cast<std::size_t>(m_data.bytecodeEnd)) = (loopConditionStart - jumpBackPatchOffset);
            m_data.bytecodeEnd += 4;
            *reinterpret_cast<std::int32_t*>(bytecodeStorage() + static_cast<std::size_t>(conditionPatchOffset)) = (m_data.bytecodeEnd - conditionPatchOffset);

            for (int slot = 0; slot < 0x80 && localBreakPatchList[slot] != 0; ++slot)
            {
                const int patchOffset = localBreakPatchList[slot];
            *reinterpret_cast<std::int32_t*>(bytecodeStorage() + static_cast<std::size_t>(patchOffset)) = (m_data.bytecodeEnd - patchOffset);
            }
            return;
        }

        if (matchToken("do"))
        {
            std::int32_t localBreakPatchList[0x80] = {};
            const int loopBodyStart = m_data.bytecodeEnd;
            compileStatement(localBreakPatchList);

            requireToken("while");
            requireToken("(");
            CompileExpression(1);
            requireToken(")");
            bytecodeStorage()[static_cast<std::size_t>(m_data.bytecodeEnd++)] = script::opcodeValue(script::VmOpcode::LogicalNot);
            bytecodeStorage()[static_cast<std::size_t>(m_data.bytecodeEnd++)] = script::opcodeValue(script::VmOpcode::If);
            const int branchBackPatchOffset = m_data.bytecodeEnd;
            *reinterpret_cast<std::int32_t*>(bytecodeStorage() + static_cast<std::size_t>(m_data.bytecodeEnd)) = (loopBodyStart - branchBackPatchOffset);
            m_data.bytecodeEnd += 4;

            for (int slot = 0; slot < 0x80 && localBreakPatchList[slot] != 0; ++slot)
            {
                const int patchOffset = localBreakPatchList[slot];
            *reinterpret_cast<std::int32_t*>(bytecodeStorage() + static_cast<std::size_t>(patchOffset)) = (m_data.bytecodeEnd - patchOffset);
            }
            return;
        }

        if (matchToken("for"))
        {
            std::int32_t localBreakPatchList[0x80] = {};

            requireToken("(");
            vyrag_oper();
            requireToken(";");

            const int conditionStart = m_data.bytecodeEnd;
            CompileExpression(1);
            requireToken(";");
            bytecodeStorage()[static_cast<std::size_t>(m_data.bytecodeEnd++)] = script::opcodeValue(script::VmOpcode::If);
            const int conditionPatchOffset = m_data.bytecodeEnd;
            *reinterpret_cast<std::int32_t*>(bytecodeStorage() + static_cast<std::size_t>(m_data.bytecodeEnd)) = (0);
            m_data.bytecodeEnd += 4;
            bytecodeStorage()[static_cast<std::size_t>(m_data.bytecodeEnd++)] = script::opcodeValue(script::VmOpcode::Jump);
            const int skipUpdatePatchOffset = m_data.bytecodeEnd;
            *reinterpret_cast<std::int32_t*>(bytecodeStorage() + static_cast<std::size_t>(m_data.bytecodeEnd)) = (0);
            m_data.bytecodeEnd += 4;

            const int updateStart = m_data.bytecodeEnd;
            vyrag_oper();
            requireToken(")");
            bytecodeStorage()[static_cast<std::size_t>(m_data.bytecodeEnd++)] = script::opcodeValue(script::VmOpcode::Jump);
            const int updateJumpBackPatchOffset = m_data.bytecodeEnd;
            *reinterpret_cast<std::int32_t*>(bytecodeStorage() + static_cast<std::size_t>(m_data.bytecodeEnd)) = (conditionStart - updateJumpBackPatchOffset);
            m_data.bytecodeEnd += 4;
            *reinterpret_cast<std::int32_t*>(bytecodeStorage() + static_cast<std::size_t>(skipUpdatePatchOffset)) = (m_data.bytecodeEnd - skipUpdatePatchOffset);

            compileStatement(localBreakPatchList);
            bytecodeStorage()[static_cast<std::size_t>(m_data.bytecodeEnd++)] = script::opcodeValue(script::VmOpcode::Jump);
            const int bodyJumpBackPatchOffset = m_data.bytecodeEnd;
            *reinterpret_cast<std::int32_t*>(bytecodeStorage() + static_cast<std::size_t>(m_data.bytecodeEnd)) = (updateStart - bodyJumpBackPatchOffset);
            m_data.bytecodeEnd += 4;
            *reinterpret_cast<std::int32_t*>(bytecodeStorage() + static_cast<std::size_t>(conditionPatchOffset)) = (m_data.bytecodeEnd - conditionPatchOffset);

            for (int slot = 0; slot < 0x80 && localBreakPatchList[slot] != 0; ++slot)
            {
                const int patchOffset = localBreakPatchList[slot];
            *reinterpret_cast<std::int32_t*>(bytecodeStorage() + static_cast<std::size_t>(patchOffset)) = (m_data.bytecodeEnd - patchOffset);
            }
            return;
        }

        if (matchToken("break"))
        {
            requireToken(";");
            if (!breakPatchList)
            {
                reportCompileError(10, "'break' without loop", 0);
                std::exit(1);
            }

            int slot = 0;
            while (slot < 0x80 && breakPatchList[slot] != 0)
                ++slot;
            if (slot >= 0x80)
            {
                reportCompileError(10, "Too many 'break'", 0);
                std::exit(1);
            }
            bytecodeStorage()[static_cast<std::size_t>(m_data.bytecodeEnd++)] = script::opcodeValue(script::VmOpcode::Jump);
            breakPatchList[slot] = m_data.bytecodeEnd;
            *reinterpret_cast<std::int32_t*>(bytecodeStorage() + static_cast<std::size_t>(m_data.bytecodeEnd)) = (0);
            m_data.bytecodeEnd += 4;
            breakPatchList[slot + 1] = 0;
            return;
        }

        if (matchToken("goto"))
        {
            STRING name;
            readIdentifier(name);

            int labelIndex = getFunctionIndex(name);
            const bool appendedPendingLabel = labelIndex < 0;
            if (appendedPendingLabel)
            {
                appendFunctionRecord(name, 8, STRING(), m_data.bytecodeEnd + 1, 0, 0);
                labelIndex = functionCount() - 1;
            }

            const script::LogicFunctionRecord* const records = functionRecordStorage();
            const script::LogicFunctionRecord& labelRecord = records[static_cast<std::size_t>(labelIndex)];

            if (labelRecord.flags == 8 && !appendedPendingLabel)
            {
                char buffer[512];
                std::snprintf(buffer, sizeof(buffer), "second use undefined label '%s'", name.c_str());
                reportCompileError(10, buffer, 0);
                std::exit(1);
            }
            if (labelRecord.flags != 7 && labelRecord.flags != 8)
            {
                char buffer[512];
                std::snprintf(buffer, sizeof(buffer), "'%s' is not label", name.c_str());
                reportCompileError(10, buffer, 0);
                std::exit(1);
            }
            bytecodeStorage()[static_cast<std::size_t>(m_data.bytecodeEnd++)] = script::opcodeValue(script::VmOpcode::Jump);
            const int payloadOffset = m_data.bytecodeEnd;
            *reinterpret_cast<std::int32_t*>(bytecodeStorage() + static_cast<std::size_t>(m_data.bytecodeEnd)) = (labelRecord.value0 - payloadOffset);
            m_data.bytecodeEnd += 4;
            return;
        }

        if (matchToken("{"))
        {
            const int savedFunctionCount = functionCount();
            while (!matchToken("}"))
                compileStatement(breakPatchList);
            m_data.functionCount = savedFunctionCount;
            return;
        }

        vyrag_oper();
        requireToken(";");
    }

    int SCRIPT::compileNextSourceItem()
    {
        if (skipTriviaAndPreprocess())
            return 1;

        const std::size_t cursor = static_cast<std::size_t>(sourceCursorOffset());
        auto starts = [&](const char* token) -> bool
        {
            return std::strncmp(reinterpret_cast<const char*>(sourceStorage() + cursor),
                token, std::strlen(token)) == 0;
        };

        if (starts("#define"))
            return compileDefineDirective();
        if (starts("#undef"))
            return compileUndefDirective();
        if (matchToken("#include"))
            return compileIncludeDirective();
        if (matchToken("extern"))
            return compileExternDirective();

        if (matchToken("static"))
        {
            if (matchToken("int"))
            {
                do
                {
                    compileIntDeclaration(3);
                }
                while (matchToken(","));
                requireToken(";");
                return skipTriviaAndPreprocess();
            }
            if (matchToken("string"))
            {
                do
                {
                    compileStringDeclaration(3);
                }
                while (matchToken(","));
                requireToken(";");
                return skipTriviaAndPreprocess();
            }
            reportCompileError(4, "static variable", 0);
            std::exit(1);
        }

        if (matchToken("int"))
        {
            do
            {
                compileIntDeclaration(2);
            }
            while (matchToken(","));
            requireToken(";");
            return skipTriviaAndPreprocess();
        }
        if (matchToken("string"))
        {
            do
            {
                compileStringDeclaration(2);
            }
            while (matchToken(","));
            requireToken(";");
            return skipTriviaAndPreprocess();
        }

        return compileFunctionDirective();
    }

    int SCRIPT::compileDefineDirective()
    {
        setSourceCursorOffset(sourceCursorOffset() + 7);
        STRING name;
        m_data.parseMode = 1;
        readIdentifier(name);
        m_data.parseMode = 0;

        STRING value;
        readSourceLine(value);

        addOrReplaceDefine(name, value);

        destroyStringStorage(value);
        value.ResetSharedEmptyWithoutRelease();
        return skipTriviaAndPreprocess();
    }

    int SCRIPT::compileUndefDirective()
    {
        setSourceCursorOffset(sourceCursorOffset() + 6);
        STRING name;
        m_data.parseMode = 1;
        readIdentifier(name);
        m_data.parseMode = 0;

        if (undefine(name) < 0)
        {
            reportCompileError(4, "#undef parameters", 0);
            std::exit(1);
        }
        return skipTriviaAndPreprocess();
    }

    int SCRIPT::compileIncludeDirective()
    {
        STRING savedScriptFile = scriptFileStorage();

        const unsigned char delimiter = sourceByteAtCursor();
        if (delimiter != '"' && delimiter != '<')
        {
            reportCompileError(13, "include file name", 0);
            std::exit(1);
        }

        setSourceCursorOffset(sourceCursorOffset() + 1);

        char includeName[0x400];
        int includeNameLength = 0;
        for (;;)
        {
            const unsigned char c = sourceByteAtCursor();
            if (c == '"' || c == '>')
                break;
            if (sourceCursorOffset() >= sourceEndOffset())
            {
                reportCompileError(10, "End of file", 0);
                std::exit(1);
            }
            includeName[includeNameLength++] = static_cast<char>(c);
            setSourceCursorOffset(sourceCursorOffset() + 1);
        }
        includeName[includeNameLength] = '\0';
        setSourceCursorOffset(sourceCursorOffset() + 1);

        FSTREAM includeStream(includeName, "rb");
        if (!includeStream.isOpen())
        {
            reportCompileError(7, includeName, 0);
            std::exit(1);
        }

        const std::uint32_t includeLength = fileLength32(includeStream);
        const int savedCursor = sourceCursorOffset();
        const int savedEnd = sourceEndOffset();
        const int savedLine = m_data.sourceLine;
        const int savedConditionalDepth = m_data.conditionalDepth;

        void* includeSourceOwner = nullptr;
        try
        {
            const std::uint32_t includeAllocationSize = includeLength + static_cast<std::uint32_t>(SourceBufferPadding);
            includeSourceOwner = ::operator new(static_cast<std::size_t>(includeAllocationSize));
        }
        catch (...)
        {
            reportCompileError(2, "include", 0);
            std::exit(1);
        }
        const std::uint32_t savedSourceToken = m_data.sourceBufferToken;
        m_data.sourceBufferToken = pointerToken(includeSourceOwner);
        setSourceCursorOffset(static_cast<int>(SourcePayloadOffset));
        const std::uint32_t includeEndOffset = static_cast<std::uint32_t>(SourcePayloadOffset) + includeLength;
        setSourceEndOffset(static_cast<std::int32_t>(includeEndOffset));
        if (includeLength != 0)
        {
            includeStream.read(sourceStorage() + SourcePayloadOffset,
                includeLength);
        }

        m_data.sourceLine = 0;
        assignStringFromCString(scriptFileStorage(), includeName);
        m_data.conditionalDepth = 0;

        int status = compileNextSourceItem();
        while (status == 0)
            status = compileNextSourceItem();

        includeStream.close();


        ::operator delete(static_cast<void*>(sourceStorage()));
        m_data.sourceBufferToken = savedSourceToken;
        setSourceCursorOffset(savedCursor);
        setSourceEndOffset(savedEnd);
        m_data.sourceLine = savedLine;
        m_data.conditionalDepth = savedConditionalDepth;
        assignStringFromString(scriptFileStorage(), savedScriptFile);

        destroyStringStorage(savedScriptFile);
        savedScriptFile.ResetSharedEmptyWithoutRelease();
        return skipTriviaAndPreprocess();
    }

    int SCRIPT::compileExternDirective()
    {
        const int savedFunctionCount = functionCount();

        STRING name;
        readIdentifier(name);

        const script::LogicFunctionRecord* const records = functionRecordStorage();
        for (int i = savedFunctionCount - 1; i >= 0; --i)
        {
            if (std::strcmp(records[static_cast<std::size_t>(i)].name.c_str(), name.c_str()) == 0)
            {
                reportCompileError(10, "function redefinition", 0);
                std::exit(1);
            }
        }

        const int stackBase = executionStackCount();
        requireToken("(");
        for (;;)
        {
            if (matchToken("int"))
            {
                compileIntDeclaration(4);
            }
            else if (matchToken("string"))
            {
                compileStringDeclaration(4);
            }

            if (!matchToken(","))
                break;
        }
        requireToken(")");

        const int parameterCount = executionStackCount() - stackBase;
        const int nativeCode = parseConstantIntExpression();

        const unsigned char previous = sourceStorage()[static_cast<std::size_t>(sourceCursorOffset() - 1)];
        if (!std::isdigit(ctypeArgument(previous)))
        {
            reportCompileError(13, "extern function code", 0);
            std::exit(1);
        }

        m_data.functionCount = savedFunctionCount;
        appendFunctionRecord(name, 2, STRING(), nativeCode, stackBase, parameterCount);
        requireToken(";");
        return skipTriviaAndPreprocess();
    }


    int SCRIPT::compileFunctionDirective()
    {


        const int savedFunctionCount = functionCount();
        STRING name;
        readIdentifier(name);

        int existingFunction = -1;
        const script::LogicFunctionRecord* records = functionRecordStorage();
        for (int i = savedFunctionCount - 1; i >= 0; --i)
        {
            if (std::strcmp(records[static_cast<std::size_t>(i)].name.c_str(), name.c_str()) == 0)
            {
                existingFunction = i;
                break;
            }
        }

        const int stackBase = executionStackCount();
        const int bytecodeStart = m_data.bytecodeEnd;
        int parameterOrdinal = 0;


        std::uint8_t newFunctionState[2] = { 3u, 0u };
        if (existingFunction >= 0)
        {
            script::LogicFunctionRecord* const existing = mutableFunctionRecordAt(existingFunction);
            m_data.currentSymbolToken = pointerToken(existing ? &existing->flags : nullptr);
        }
        else
        {
            m_data.currentSymbolToken = pointerToken(&newFunctionState[0]);
        }

        requireToken("(");
        for (;;)
        {
            bool declaredParameter = false;
            if (matchToken("int"))
            {
                compileIntDeclaration(4);
                declaredParameter = true;
            }
            else if (matchToken("string"))
            {
                compileStringDeclaration(4);
                declaredParameter = true;
            }


            if (declaredParameter && existingFunction >= 0 && functionCount() > 0)
            {
                records = functionRecordStorage();
                script::LogicFunctionRecord* const lastParameterSymbol =
                    mutableFunctionRecordAt(functionCount() - 1);
                if (lastParameterSymbol)
                {
                    lastParameterSymbol->value0 =
                        records[static_cast<std::size_t>(existingFunction)].value1 + parameterOrdinal;
                }
            }

            ++parameterOrdinal;
            if (!matchToken(","))
                break;
        }
        requireToken(")");

        const int parameterCount = executionStackCount() - stackBase;
        if (existingFunction >= 0)
        {
            records = functionRecordStorage();
            const script::LogicFunctionRecord& previous =
                records[static_cast<std::size_t>(existingFunction)];
            if (previous.value2 != parameterCount || previous.value0 != -1)
            {
                reportCompileError(10, "function redefinition", 0);
                std::exit(1);
            }


            if (stackBase <= 0)
                clearExecutionStack();
            else if (m_data.stackCount > stackBase)
                m_data.stackCount = stackBase;
        }

        int functionBytecodeStart = bytecodeStart;
        if (matchToken(";"))
        {
            functionBytecodeStart = -1;
        }
        else
        {


            if (existingFunction >= 0)
            {
                script::LogicFunctionRecord* const previous =
                    mutableFunctionRecordAt(existingFunction);
                if (previous)
                    previous->value0 = bytecodeStart;
            }

            requireToken("{");
            while (!matchToken("}"))
                compileStatement(nullptr);

            bytecodeStorage()[static_cast<std::size_t>(m_data.bytecodeEnd++)] =
                script::opcodeValue(script::VmOpcode::Return);
        }


        m_data.functionCount = savedFunctionCount;

        if (existingFunction >= 0)
        {
            m_data.currentSymbolToken = 0u;
            return skipTriviaAndPreprocess();
        }

        appendFunctionRecord(name, 3, STRING(), functionBytecodeStart, stackBase, parameterCount);
        const int newFunctionIndex = functionCount() - 1;
        if (script::LogicFunctionRecord* const added = mutableFunctionRecordAt(newFunctionIndex))
            added->statusFlags = newFunctionState[1];
        m_data.currentSymbolToken = 0u;

        if (std::strcmp(name.c_str(), "main") == 0)
        {
            m_data.fallbackFunction = newFunctionIndex;
            return skipTriviaAndPreprocess();
        }


        static const char ScriptEventPrefix[] = "ScriptEvent";
        const char* const functionName = name.c_str();
        const std::size_t prefixLength = sizeof(ScriptEventPrefix) - 1u;


        if (std::strncmp(functionName, ScriptEventPrefix, prefixLength) == 0)
        {
            const char* const suffix = functionName + prefixLength;
            const unsigned char first = static_cast<unsigned char>(*suffix);
            const unsigned char second = static_cast<unsigned char>(suffix[1]);
            if (std::isdigit(ctypeArgument(first)) != 0 ||
                (first == '-' && std::isdigit(ctypeArgument(second)) != 0))
            {
                int eventNumber = 0;
                if (suffix[1] == 'x')
                    std::sscanf(suffix, "%i", &eventNumber);
                else
                    eventNumber = std::atoi(suffix);

                if (parameterCount != 3)
                {
                    reportCompileError(10, "ScriptEvent must have 3 parameters", 0);
                    std::exit(1);
                }
                if (eventNumber < 0 || eventNumber >= 0x100)
                {
                    reportCompileError(10, "too big number in function name", 0);
                    std::exit(1);
                }
                m_data.scriptEventFunction[static_cast<std::size_t>(eventNumber)] =
                    newFunctionIndex;
            }
        }

        return skipTriviaAndPreprocess();
    }


    namespace
    {
        bool readVmDword(const std::uint8_t* bytecode, int offset, int& value)
        {
            std::uint32_t raw = 0;
            std::memcpy(&raw, bytecode + static_cast<std::size_t>(offset), sizeof(raw));
            value = static_cast<int>(raw);
            return true;
        }

        int stackValueToInteger(const script::StackObject& value)
        {
            return (value.flags & script::STACK_OBJECT_STRING)
                ? script::ParseStackIntegerText(value.text.c_str())
                : value.intValue;
        }
        int scriptSpritePointerValue(SPRITE* sprite) noexcept
        {
            return sprite
                ? static_cast<int>(static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(sprite)))
                : 0;
        }

        SPRITE* scriptResolveSpriteReference(int value) noexcept
        {
            if (value == 0)
                return nullptr;
            return reinterpret_cast<SPRITE*>(
                static_cast<std::uintptr_t>(static_cast<std::uint32_t>(value)));
        }

        bool isValidNvid(int nvid) noexcept
        {
            core::ApplicationVidTable& table = core::GlobalApplicationVidTable();
            return nvid >= 0 && nvid < table.count() &&
                   table.slot(nvid) != nullptr;
        }

        VID* resolveVidByNvid(int nvid) noexcept
        {
            core::ApplicationVidTable& table = core::GlobalApplicationVidTable();
            if (nvid >= 0 && nvid < table.count())
            {
                if (VID* const vid = table.slot(nvid))
                    return vid;
            }
            return EmptyVid;
        }

        PLAYER* scriptPlayerSlot(int index) noexcept
        {


            return Map->Player(index);
        }

        SPRITE* beginReverseDrawPassIteration(core::ApplicationDrawDispatcherState& drawState, int pass, int* cursor)
        {
            const core::ApplicationDrawPassBucket& bucket = drawState.drawPassBucket(pass);
            *cursor = bucket.count() - 1;
            while (*cursor >= 0 && !bucket.spriteAt(*cursor))
                --(*cursor);
            return *cursor >= 0 ? bucket.spriteAt(*cursor) : nullptr;
        }

        int g_unitIteratorArmyBucket = 0;
        int g_unitIteratorOrdinal = 0;


        ENGINE* GetTrainEng(int a1, int a2, int* outOrdinal)
        {
            SPRITE_COLLECTOR* const map = GlobalSpriteCollector();
            int skipped = 0;
            int matched = 0;
            int count = map->overflowCount();
            if (!count)
            {
                *outOrdinal = 0;
                return nullptr;
            }

            int index = count - 1;
            SPRITE* sprite = map->overflowSpriteAt(index);
            if (!sprite)
            {
                *outOrdinal = 0;
                return nullptr;
            }

            for (;;)
            {
                if (sprite->Vid()->spriteClassId() == 21 && !sprite->engineChainPrevious())
                {
                    if (a1 == 4 || sprite->armyIndex() == a1)
                    {
                        ++skipped;
                        if (++matched == a2)
                        {
                            *outOrdinal = skipped;
                            SPRITE* const special = static_cast<ENGINE*>(sprite)->findEngineChainSpecialWeaponNode();
                            if (!special)
                                return static_cast<ENGINE*>(sprite);
                            if ((sprite->runtimeFlags() ^ special->runtimeFlags()) & SPRITE::ArmyBitsMask)
                                --matched;
                            else
                                return static_cast<ENGINE*>(special);
                        }
                    }
                    else
                    {
                        ++skipped;
                    }
                }

                if (index > map->overflowCount())
                    index = map->overflowCount();
                --index;
                if (index < 0)
                {
                    *outOrdinal = 0;
                    return nullptr;
                }
                sprite = map->overflowSpriteAt(index);
                if (!sprite)
                {
                    *outOrdinal = 0;
                    return nullptr;
                }
            }
        }


        ENGINE* FirstTrain(int a1)
        {
            if (a1 < 0)
                return nullptr;
            int bucket = a1;
            if (bucket >= 4)
                bucket = 3;
            g_unitIteratorArmyBucket = bucket;
            g_unitIteratorOrdinal = 1;
            return GetTrainEng(bucket, 1, &a1);
        }


        ENGINE* NextTrain()
        {
            int value = 0;
            return GetTrainEng(g_unitIteratorArmyBucket, ++g_unitIteratorOrdinal, &value);
        }


        constexpr std::uint32_t scriptSinTableBits[256] =
        {
        0x00000000u, 0x3CC90AB0u, 0x3D48FB2Fu, 0x3D96A905u, 0x3DC8BD36u, 0x3DFAB273u, 0x3E164083u, 0x3E2F10A2u,
        0x3E47C5C2u, 0x3E605C13u, 0x3E78CFCCu, 0x3E888E93u, 0x3E94A031u, 0x3EA09AE5u, 0x3EAC7CD4u, 0x3EB8442Au,
        0x3EC3EF15u, 0x3ECF7BCAu, 0x3EDAE880u, 0x3EE63375u, 0x3EF15AEAu, 0x3EFC5D27u, 0x3F039C3Du, 0x3F08F59Bu,
        0x3F0E39DAu, 0x3F13682Au, 0x3F187FC0u, 0x3F1D7FD1u, 0x3F226799u, 0x3F273656u, 0x3F2BEB4Au, 0x3F3085BBu,
        0x3F3504F3u, 0x3F396842u, 0x3F3DAEF9u, 0x3F41D870u, 0x3F45E403u, 0x3F49D112u, 0x3F4D9F02u, 0x3F514D3Du,
        0x3F54DB31u, 0x3F584853u, 0x3F5B941Au, 0x3F5EBE05u, 0x3F61C598u, 0x3F64AA59u, 0x3F676BD8u, 0x3F6A09A7u,
        0x3F6C835Eu, 0x3F6ED89Eu, 0x3F710908u, 0x3F731447u, 0x3F74FA0Bu, 0x3F76BA07u, 0x3F7853F8u, 0x3F79C79Du,
        0x3F7B14BEu, 0x3F7C3B28u, 0x3F7D3AACu, 0x3F7E1324u, 0x3F7EC46Du, 0x3F7F4E6Du, 0x3F7FB10Fu, 0x3F7FEC43u,
        0x3F800000u, 0x3F7FEC43u, 0x3F7FB10Fu, 0x3F7F4E6Du, 0x3F7EC46Du, 0x3F7E1324u, 0x3F7D3AACu, 0x3F7C3B28u,
        0x3F7B14BEu, 0x3F79C79Du, 0x3F7853F8u, 0x3F76BA07u, 0x3F74FA0Bu, 0x3F731447u, 0x3F710908u, 0x3F6ED89Eu,
        0x3F6C835Eu, 0x3F6A09A7u, 0x3F676BD8u, 0x3F64AA59u, 0x3F61C598u, 0x3F5EBE05u, 0x3F5B941Au, 0x3F584853u,
        0x3F54DB31u, 0x3F514D3Du, 0x3F4D9F02u, 0x3F49D112u, 0x3F45E403u, 0x3F41D870u, 0x3F3DAEF9u, 0x3F396842u,
        0x3F3504F3u, 0x3F3085BBu, 0x3F2BEB4Au, 0x3F273656u, 0x3F226799u, 0x3F1D7FD1u, 0x3F187FC0u, 0x3F13682Au,
        0x3F0E39DAu, 0x3F08F59Bu, 0x3F039C3Du, 0x3EFC5D27u, 0x3EF15AEAu, 0x3EE63375u, 0x3EDAE880u, 0x3ECF7BCAu,
        0x3EC3EF15u, 0x3EB8442Au, 0x3EAC7CD4u, 0x3EA09AE5u, 0x3E94A031u, 0x3E888E93u, 0x3E78CFCCu, 0x3E605C13u,
        0x3E47C5C2u, 0x3E2F10A2u, 0x3E164083u, 0x3DFAB273u, 0x3DC8BD36u, 0x3D96A905u, 0x3D48FB2Fu, 0x3CC90AB0u,
        0x00000000u, 0xBCC90AAFu, 0xBD48FB2Fu, 0xBD96A905u, 0xBDC8BD36u, 0xBDFAB273u, 0xBE164083u, 0xBE2F10A2u,
        0xBE47C5C2u, 0xBE605C13u, 0xBE78CFCCu, 0xBE888E93u, 0xBE94A031u, 0xBEA09AE5u, 0xBEAC7CD4u, 0xBEB8442Au,
        0xBEC3EF15u, 0xBECF7BCAu, 0xBEDAE880u, 0xBEE63375u, 0xBEF15AEAu, 0xBEFC5D27u, 0xBF039C3Du, 0xBF08F59Bu,
        0xBF0E39DAu, 0xBF13682Au, 0xBF187FC0u, 0xBF1D7FD1u, 0xBF226799u, 0xBF273656u, 0xBF2BEB4Au, 0xBF3085BBu,
        0xBF3504F3u, 0xBF396842u, 0xBF3DAEF9u, 0xBF41D870u, 0xBF45E403u, 0xBF49D112u, 0xBF4D9F02u, 0xBF514D3Du,
        0xBF54DB31u, 0xBF584853u, 0xBF5B941Au, 0xBF5EBE05u, 0xBF61C598u, 0xBF64AA59u, 0xBF676BD8u, 0xBF6A09A7u,
        0xBF6C835Eu, 0xBF6ED89Eu, 0xBF710908u, 0xBF731447u, 0xBF74FA0Bu, 0xBF76BA07u, 0xBF7853F8u, 0xBF79C79Du,
        0xBF7B14BEu, 0xBF7C3B28u, 0xBF7D3AACu, 0xBF7E1324u, 0xBF7EC46Du, 0xBF7F4E6Du, 0xBF7FB10Fu, 0xBF7FEC43u,
        0xBF800000u, 0xBF7FEC43u, 0xBF7FB10Fu, 0xBF7F4E6Du, 0xBF7EC46Du, 0xBF7E1324u, 0xBF7D3AACu, 0xBF7C3B28u,
        0xBF7B14BEu, 0xBF79C79Du, 0xBF7853F8u, 0xBF76BA07u, 0xBF74FA0Bu, 0xBF731447u, 0xBF710908u, 0xBF6ED89Eu,
        0xBF6C835Eu, 0xBF6A09A7u, 0xBF676BD8u, 0xBF64AA59u, 0xBF61C598u, 0xBF5EBE05u, 0xBF5B941Au, 0xBF584853u,
        0xBF54DB31u, 0xBF514D3Du, 0xBF4D9F02u, 0xBF49D112u, 0xBF45E403u, 0xBF41D870u, 0xBF3DAEF9u, 0xBF396842u,
        0xBF3504F3u, 0xBF3085BBu, 0xBF2BEB4Au, 0xBF273656u, 0xBF226799u, 0xBF1D7FD1u, 0xBF187FC0u, 0xBF13682Au,
        0xBF0E39DAu, 0xBF08F59Bu, 0xBF039C3Du, 0xBEFC5D27u, 0xBEF15AEAu, 0xBEE63375u, 0xBEDAE880u, 0xBECF7BCAu,
        0xBEC3EF15u, 0xBEB8442Au, 0xBEAC7CD4u, 0xBEA09AE5u, 0xBE94A031u, 0xBE888E93u, 0xBE78CFCCu, 0xBE605C13u,
        0xBE47C5C2u, 0xBE2F10A2u, 0xBE164083u, 0xBDFAB273u, 0xBDC8BD36u, 0xBD96A905u, 0xBD48FB30u, 0xBCC90AB0u,
        };

        constexpr std::uint32_t scriptCosTableBits[256] =
        {
        0x3F800000u, 0x3F7FEC43u, 0x3F7FB10Fu, 0x3F7F4E6Du, 0x3F7EC46Du, 0x3F7E1324u, 0x3F7D3AACu, 0x3F7C3B28u,
        0x3F7B14BEu, 0x3F79C79Du, 0x3F7853F8u, 0x3F76BA07u, 0x3F74FA0Bu, 0x3F731447u, 0x3F710908u, 0x3F6ED89Eu,
        0x3F6C835Eu, 0x3F6A09A7u, 0x3F676BD8u, 0x3F64AA59u, 0x3F61C598u, 0x3F5EBE05u, 0x3F5B941Au, 0x3F584853u,
        0x3F54DB31u, 0x3F514D3Du, 0x3F4D9F02u, 0x3F49D112u, 0x3F45E403u, 0x3F41D870u, 0x3F3DAEF9u, 0x3F396842u,
        0x3F3504F3u, 0x3F3085BBu, 0x3F2BEB4Au, 0x3F273656u, 0x3F226799u, 0x3F1D7FD1u, 0x3F187FC0u, 0x3F13682Au,
        0x3F0E39DAu, 0x3F08F59Bu, 0x3F039C3Du, 0x3EFC5D27u, 0x3EF15AEAu, 0x3EE63375u, 0x3EDAE880u, 0x3ECF7BCAu,
        0x3EC3EF15u, 0x3EB8442Au, 0x3EAC7CD4u, 0x3EA09AE5u, 0x3E94A031u, 0x3E888E93u, 0x3E78CFCCu, 0x3E605C13u,
        0x3E47C5C2u, 0x3E2F10A2u, 0x3E164083u, 0x3DFAB273u, 0x3DC8BD36u, 0x3D96A905u, 0x3D48FB2Fu, 0x3CC90AB0u,
        0x00000000u, 0xBCC90AAFu, 0xBD48FB2Fu, 0xBD96A905u, 0xBDC8BD36u, 0xBDFAB273u, 0xBE164083u, 0xBE2F10A2u,
        0xBE47C5C2u, 0xBE605C13u, 0xBE78CFCCu, 0xBE888E93u, 0xBE94A031u, 0xBEA09AE5u, 0xBEAC7CD4u, 0xBEB8442Au,
        0xBEC3EF15u, 0xBECF7BCAu, 0xBEDAE880u, 0xBEE63375u, 0xBEF15AEAu, 0xBEFC5D27u, 0xBF039C3Du, 0xBF08F59Bu,
        0xBF0E39DAu, 0xBF13682Au, 0xBF187FC0u, 0xBF1D7FD1u, 0xBF226799u, 0xBF273656u, 0xBF2BEB4Au, 0xBF3085BBu,
        0xBF3504F3u, 0xBF396842u, 0xBF3DAEF9u, 0xBF41D870u, 0xBF45E403u, 0xBF49D112u, 0xBF4D9F02u, 0xBF514D3Du,
        0xBF54DB31u, 0xBF584853u, 0xBF5B941Au, 0xBF5EBE05u, 0xBF61C598u, 0xBF64AA59u, 0xBF676BD8u, 0xBF6A09A7u,
        0xBF6C835Eu, 0xBF6ED89Eu, 0xBF710908u, 0xBF731447u, 0xBF74FA0Bu, 0xBF76BA07u, 0xBF7853F8u, 0xBF79C79Du,
        0xBF7B14BEu, 0xBF7C3B28u, 0xBF7D3AACu, 0xBF7E1324u, 0xBF7EC46Du, 0xBF7F4E6Du, 0xBF7FB10Fu, 0xBF7FEC43u,
        0xBF800000u, 0xBF7FEC43u, 0xBF7FB10Fu, 0xBF7F4E6Du, 0xBF7EC46Du, 0xBF7E1324u, 0xBF7D3AACu, 0xBF7C3B28u,
        0xBF7B14BEu, 0xBF79C79Du, 0xBF7853F8u, 0xBF76BA07u, 0xBF74FA0Bu, 0xBF731447u, 0xBF710908u, 0xBF6ED89Eu,
        0xBF6C835Eu, 0xBF6A09A7u, 0xBF676BD8u, 0xBF64AA59u, 0xBF61C598u, 0xBF5EBE05u, 0xBF5B941Au, 0xBF584853u,
        0xBF54DB31u, 0xBF514D3Du, 0xBF4D9F02u, 0xBF49D112u, 0xBF45E403u, 0xBF41D870u, 0xBF3DAEF9u, 0xBF396842u,
        0xBF3504F3u, 0xBF3085BBu, 0xBF2BEB4Au, 0xBF273656u, 0xBF226799u, 0xBF1D7FD1u, 0xBF187FC0u, 0xBF13682Au,
        0xBF0E39DAu, 0xBF08F59Bu, 0xBF039C3Du, 0xBEFC5D27u, 0xBEF15AEAu, 0xBEE63375u, 0xBEDAE880u, 0xBECF7BCAu,
        0xBEC3EF15u, 0xBEB8442Au, 0xBEAC7CD4u, 0xBEA09AE5u, 0xBE94A031u, 0xBE888E93u, 0xBE78CFCCu, 0xBE605C13u,
        0xBE47C5C2u, 0xBE2F10A2u, 0xBE164083u, 0xBDFAB273u, 0xBDC8BD36u, 0xBD96A905u, 0xBD48FB2Fu, 0xBCC90AB0u,
        0x00000000u, 0x3CC90AAFu, 0x3D48FB2Fu, 0x3D96A905u, 0x3DC8BD36u, 0x3DFAB273u, 0x3E164083u, 0x3E2F10A2u,
        0x3E47C5C2u, 0x3E605C13u, 0x3E78CFCCu, 0x3E888E93u, 0x3E94A031u, 0x3EA09AE5u, 0x3EAC7CD4u, 0x3EB8442Au,
        0x3EC3EF15u, 0x3ECF7BCAu, 0x3EDAE880u, 0x3EE63375u, 0x3EF15AEAu, 0x3EFC5D27u, 0x3F039C3Du, 0x3F08F59Bu,
        0x3F0E39DAu, 0x3F13682Au, 0x3F187FC0u, 0x3F1D7FD1u, 0x3F226799u, 0x3F273656u, 0x3F2BEB4Au, 0x3F3085BBu,
        0x3F3504F3u, 0x3F396842u, 0x3F3DAEF9u, 0x3F41D870u, 0x3F45E403u, 0x3F49D112u, 0x3F4D9F02u, 0x3F514D3Du,
        0x3F54DB31u, 0x3F584853u, 0x3F5B941Au, 0x3F5EBE05u, 0x3F61C598u, 0x3F64AA59u, 0x3F676BD8u, 0x3F6A09A7u,
        0x3F6C835Eu, 0x3F6ED89Eu, 0x3F710908u, 0x3F731447u, 0x3F74FA0Bu, 0x3F76BA07u, 0x3F7853F8u, 0x3F79C79Du,
        0x3F7B14BEu, 0x3F7C3B28u, 0x3F7D3AACu, 0x3F7E1324u, 0x3F7EC46Du, 0x3F7F4E6Du, 0x3F7FB10Fu, 0x3F7FEC43u,
        };

        float scriptNativeFloatFromBits(std::uint32_t bits)
        {
            float value = 0.0f;
            std::memcpy(&value, &bits, sizeof(value));
            return value;
        }

        int scriptNativeTable1024ToInt(float value)
        {
            return static_cast<int>(value * 1024.0f);
        }

        int scriptNativeSin1024(int angle)
        {
            return scriptNativeTable1024ToInt(SPRITE::rawDirectionSin(angle));
        }

        int scriptNativeCos1024(int angle)
        {
            return scriptNativeTable1024ToInt(SPRITE::rawDirectionCos(angle));
        }
    }


    int SCRIPT::getActionVariableInt(int actionIndex)
    {
        if (m_data.compileError != 0)
            return 0;

        const std::uint32_t rawAction = static_cast<std::uint32_t>(actionIndex);
        if (rawAction < 256u)
        {
            const int stackIndex = m_data.actionN[rawAction];
            if (static_cast<std::uint32_t>(stackIndex) <
                static_cast<std::uint32_t>(m_data.stackCount))
            {
                return mutableExecutionStackStorageAt(stackIndex)->Int();
            }
        }

        char variableName[64]{};
        std::snprintf(variableName, sizeof(variableName), "variable for Get Action%i", actionIndex);


        reportCompileError(13, variableName, 0);
        return 0;
    }


const char* SCRIPT::stringTextPointer(const STRING& value) const
    {
        return value.c_str();
    }

    int SCRIPT::writeCStringToStream(const STRING& source, BaseStream* target) const
    {
        const char* const text = source.c_str();
        return target->write(text, static_cast<unsigned>(std::strlen(text) + 1));
    }

    std::size_t SCRIPT::writeCStringRecord(const STRING& value, std::FILE* file) const
    {
        const char* text = value.c_str();
        const std::size_t sizeWithNul = std::strlen(text) + 1;
        return std::fwrite(text, sizeWithNul, 1, file);
    }

    std::FILE* SCRIPT::openScriptFile(const STRING& path, const char* mode) const
    {

        const char* text = path.c_str();
        if (text[0] == '\0')
            return nullptr;
        return std::fopen(text, mode);
    }


    int SCRIPT::popSpriteReferenceValue()
    {
        const int oldIndex = m_data.stackCount - 1;
        script::StackObject* top = mutableExecutionStackStorageAt(oldIndex);


        --m_data.stackCount;
        return stackValueToInteger(*top);
    }


    void SCRIPT::pushSpriteReferenceValue(int value, const STRING* context)
    {
        (void)context;
        script::StackObject obj;
        obj.initializeReferenceValue(value);
        appendExecutionStackObject(obj);
    }




    int SCRIPT::IsLastStackString() const
    {
        const script::StackObject& top = executionStackStorage()[static_cast<std::size_t>(m_data.stackCount - 1)];
        return (top.flags & script::STACK_OBJECT_STRING) != 0 ? 1 : 0;
    }


    void MAP::PushObject(int value, const STRING* context)
    {
        (void)context;
        SCRIPT* const script = reinterpret_cast<SCRIPT*>(
            reinterpret_cast<std::uint8_t*>(this) + core::application_layout::ScriptRuntime);
        script::StackObject obj;
        obj.initializeReferenceValue(value);
        reinterpret_cast<script::StackObjectList*>(&script->m_data.stackListVtable)->appendFields(
            obj.flags, obj.intValue, obj.text);
    }


    int ScriptExecFunc(int opcode)
    {


        return Map->ExecFunc(opcode);
    }


    int MAP::ExecFunc(int opcode)
    {
        static STRING str;
        SCRIPT* const script = reinterpret_cast<SCRIPT*>(
            reinterpret_cast<std::uint8_t*>(this) + core::application_layout::ScriptRuntime);


        core::ApplicationDrawDispatcherState& drawState =
            core::GlobalApplicationDrawDispatcherState();
        switch (static_cast<script::NativeFunctionCode>(opcode))
        {
        case script::NativeFunctionCode::CreateSprite:
{
                const int parentHandle = script->popSpriteReferenceValue();
                const int direction = PopInt();
                const int z = PopInt();
                const int y = PopInt();
                const int x = PopInt();
                VID* const vid = PopVid("for CreateSprite()");
                if (vid == EmptyVid)
                {
                    PushInt(0);
                    return 0;
                }

                MAP* const map = Map;
                SPRITE* const parent = scriptResolveSpriteReference(parentHandle);
                SPRITE* const created = map->CreateSprite(
                    vid,
                    VECTOR(static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)),
                    ANGLE(direction),
                    parent,
                    false);
                PushObject(scriptSpritePointerValue(created), nullptr);
                return 0;
            }
        case script::NativeFunctionCode::Flagman:
{
                        const int army = PopInt();
                        SPRITE* sprite = nullptr;
                        sprite = reinterpret_cast<MAP*>(win::applicationWinInstance())->flagmanSpriteForPlayer(army);
                        PushObject(scriptSpritePointerValue(sprite), nullptr);
                        return 0;
            }
        case script::NativeFunctionCode::FirstUnit:
{
                typeunit = PopInt();
                nmonster = 0;
                SPRITE_COLLECTOR* const hash = GlobalSpriteCollector();
                core::List<SPRITE*>& list = hash->mutableOverflowList();
                SPRITE* sprite = list.BeginIterate(&nmonster);
                while (sprite && (!sprite->Vid() || (static_cast<int>(sprite->Vid()->spriteType) & typeunit) == 0))
                    sprite = list.NextIterate(&nmonster);
                PushObject(scriptSpritePointerValue(sprite), nullptr);
                return 0;
            }
        case script::NativeFunctionCode::NextUnit:
{
                SPRITE_COLLECTOR* const hash = GlobalSpriteCollector();
                core::List<SPRITE*>& list = hash->mutableOverflowList();
                SPRITE* sprite = list.NextIterate(&nmonster);
                while (sprite && (!sprite->Vid() || (static_cast<int>(sprite->Vid()->spriteType) & typeunit) == 0))
                    sprite = list.NextIterate(&nmonster);
                PushObject(scriptSpritePointerValue(sprite), nullptr);
                return 0;
            }
        case script::NativeFunctionCode::GetSprite:
{
                SPRITE* const previous = script->popSpriteReference();
                const int y = PopInt();
                const int x = PopInt();
                const int type = PopInt();
                auto* const application = reinterpret_cast<core::Application*>(core::ApplicationOwner());

                SPRITE* const sprite = application->findSpriteAtPointByBounds(
                    type, static_cast<float>(x), static_cast<float>(y), previous);
                PushObject(scriptSpritePointerValue(sprite), nullptr);
                return 0;
            }
        case script::NativeFunctionCode::GetSpriteScr:
{
                const int screenY = PopInt();
                const int screenX = PopInt();
                const int type = PopInt();
                auto* const application = reinterpret_cast<core::Application*>(core::ApplicationOwner());

                SPRITE* const sprite = application->findSpriteAtPointByFilter(
                    type, static_cast<float>(screenX), static_cast<float>(screenY));
                PushObject(scriptSpritePointerValue(sprite), nullptr);
                return 0;
            }
        case script::NativeFunctionCode::FindNearestSprite:
{
                SPRITE* const previous = script->popSpriteReference();
                const int radius = PopInt();
                const int y = PopInt();
                const int x = PopInt();
                const int type = PopInt();
                auto* const application = reinterpret_cast<core::Application*>(core::ApplicationOwner());

                SPRITE* const sprite = application->findNearestSpriteByFilter(
                    type, static_cast<float>(x), static_cast<float>(y), static_cast<float>(radius), previous);
                PushObject(scriptSpritePointerValue(sprite), nullptr);
                return 0;
            }
        case script::NativeFunctionCode::FirstInBox:
{
                const int bottom = PopInt();
                const int right = PopInt();
                const int top = PopInt();
                const int left = PopInt();
                SPRITE* sprite = GlobalSpriteCollectorFirstHashInBox(
                    static_cast<float>(left),
                    static_cast<float>(top),
                    static_cast<float>(right),
                    static_cast<float>(bottom));
                PushObject(scriptSpritePointerValue(sprite), nullptr);
                return 0;
            }
        case script::NativeFunctionCode::NextInBox:
{
                SPRITE* sprite = GlobalSpriteCollectorNextHashInBox();
                PushObject(scriptSpritePointerValue(sprite), nullptr);
                return 0;
            }
        case script::NativeFunctionCode::FirstSprite:
{


                g_scriptSpriteIteratorPass = -1;
                n_sprite = 0;

                SPRITE* sprite = nullptr;
                while (!sprite &&
                       g_scriptSpriteIteratorPass < core::ApplicationDrawDispatcherState::PassCount - 1)
                {
                    ++g_scriptSpriteIteratorPass;
                    n_sprite =
                        drawState.drawPassBucket(g_scriptSpriteIteratorPass).count();
                    sprite = core::Application::previousSpriteInDrawPass(
                        drawState, g_scriptSpriteIteratorPass, &n_sprite);
                }

                PushObject(scriptSpritePointerValue(sprite), nullptr);
                return 0;
            }
        case script::NativeFunctionCode::NextSprite:
{
                SPRITE* sprite = nullptr;

                --n_sprite;
                if (n_sprite >= 0)
                {
                    const core::ApplicationDrawPassBucket& bucket = drawState.drawPassBucket(g_scriptSpriteIteratorPass);
                    while (n_sprite >= 0)
                    {
                        sprite = bucket.spriteAt(n_sprite);
                        if (sprite)
                            break;
                        --n_sprite;
                    }
                }

                while (!sprite && g_scriptSpriteIteratorPass < core::ApplicationDrawDispatcherState::PassCount - 1)
                {
                    ++g_scriptSpriteIteratorPass;
                    n_sprite = drawState.drawPassBucket(g_scriptSpriteIteratorPass).count();
                    sprite = core::Application::previousSpriteInDrawPass(drawState, g_scriptSpriteIteratorPass, &n_sprite);
                }

                PushObject(scriptSpritePointerValue(sprite), nullptr);
                return 0;
            }
        case script::NativeFunctionCode::ActionByName:
{


                const int var4 = PopInt();
                const int var3 = PopInt();
                const int var2 = PopInt();
                const int var1 = PopInt();
                const STRING groupName = *PopStr();
                struct RawNamedSpriteList
                {
                    std::uint32_t vtableToken;
                    std::int32_t count;
                    std::int32_t capacity;
                    SPRITE** items;
                };

                void* const applicationOwner = core::ApplicationOwner();


                const auto* const named = reinterpret_cast<const RawNamedSpriteList*>(
                    static_cast<const std::uint8_t*>(applicationOwner) +
                    core::application_layout::TailSpriteList);
                for (int i = named->count - 1; i >= 0; --i)
                {
                    SPRITE* const sprite = named->items[i];
                    if (!sprite)
                        continue;
                    const STRING spriteName = sprite->GetName();
                    if (std::strcmp(spriteName.c_str(), groupName.c_str()) == 0)
                        (void)sprite->dispatchVirtualAction(static_cast<std::uint32_t>(var1), var2, var3, var4);
                }
                return 0;
            }
        case script::NativeFunctionCode::Action:
{
                const int var3 = PopInt();
                const int var2 = PopInt();
                const int var1 = PopInt();
                const int act = PopInt();
                SPRITE* const actionSprite = scriptResolveSpriteReference(script->popSpriteReferenceValue());


                if (!actionSprite)
                {
                    PushInt(0);
                    return 0;
                }

                if (act < 17)
                {
                    actionSprite->ChangeAnimation(act);
                    PushInt(0);
                    return 0;
                }

                if (act == 121 || act == 124 || act == 125)
                {


                    const int rawStringOwner = actionSprite->dispatchVirtualAction(
                        static_cast<std::uint32_t>(act), var1, var2, var3);
                    const STRING* const owner = reinterpret_cast<const STRING*>(
                        static_cast<std::uintptr_t>(static_cast<std::uint32_t>(rawStringOwner)));
                    PushStr(*owner);
                    return 0;
                }

                if (act == 90 || act == 156 || act == 155 || act == 154 || act == 101 || act == 103)
                {
                    const int result = actionSprite->dispatchVirtualAction(
                        static_cast<std::uint32_t>(act), var1, var2, var3);
                    PushObject(result, nullptr);
                    return 0;
                }

                if ((act == 33 || act == 32 || act == 36 || act == 34 || act == 150 || act == 151) &&
                    actionSprite->Vid()->spriteClassId() == 21u &&
                    static_cast<unsigned char>(actionSprite->runtimeFlags() & SPRITE::CommandBitsMask) == 104u)
                {
                    PushInt(0);
                    return 0;
                }

                const int result = actionSprite->dispatchVirtualAction(
                    static_cast<std::uint32_t>(act), var1, var2, var3);
                PushInt(result);
                return 0;
            }
        case script::NativeFunctionCode::SizeTo:
{
                const int y = PopInt();
                const int x = PopInt();
                const int handle = script->popSpriteReferenceValue();
                SPRITE* const sprite = scriptResolveSpriteReference(handle);
                int value = 0xEA60;
                if (sprite)
                {


                    const float distance = static_cast<float>(approximatePlanarDistance(
                        static_cast<float>(x) - sprite->X(),
                        static_cast<float>(y) - sprite->Y()));
                    value = static_cast<int>(distance);
                }
                PushInt(value);
                return 0;
            }
        case script::NativeFunctionCode::AddCommand:
{
                const int var3 = PopInt();
                const int var2 = PopInt();
                const int var1 = PopInt();
                const int act = PopInt();
                const int handle = script->popSpriteReferenceValue();
                if (handle == 0)
                    return 0;
                scriptResolveSpriteReference(handle)->AddActionAfterStop(act, var1, var2, var3);
                return 0;
            }
        case script::NativeFunctionCode::GetUnitVid:
{
                const int handle = script->popSpriteReferenceValue();
                SPRITE* const sprite = scriptResolveSpriteReference(handle);
                PushInt(sprite && sprite->vidPointer() ? sprite->vidPointer()->nVid : 0);
                return 0;
            }
        case script::NativeFunctionCode::Destroy:
{
                const int handle = script->popSpriteReferenceValue();
                if (handle == 0)
                    return 0;
                SPRITE* const sprite = scriptResolveSpriteReference(handle);
                DeleteSpriteThroughVirtualDeletingDestructor(sprite);
                return 0;
            }
        case script::NativeFunctionCode::GetX:
{
                const int handle = script->popSpriteReferenceValue();
                SPRITE* const sprite = scriptResolveSpriteReference(handle);
                PushInt(sprite ? static_cast<int>(sprite->xCoordinateValue()) : 0);
                return 0;
            }
        case script::NativeFunctionCode::GetY:
{
                const int handle = script->popSpriteReferenceValue();
                SPRITE* const sprite = scriptResolveSpriteReference(handle);
                PushInt(sprite ? static_cast<int>(sprite->yCoordinateValue()) : 0);
                return 0;
            }
        case script::NativeFunctionCode::GetZ:
{
                const int handle = script->popSpriteReferenceValue();
                SPRITE* const sprite = scriptResolveSpriteReference(handle);
                PushInt(sprite ? static_cast<int>(sprite->Z()) : 0);
                return 0;
            }
        case script::NativeFunctionCode::GetDirection:
{
                const int handle = script->popSpriteReferenceValue();
                SPRITE* const sprite = scriptResolveSpriteReference(handle);
                PushInt(sprite ? sprite->Direction().Int() : 0);
                return 0;
            }
        case script::NativeFunctionCode::GetAnimation:
{
                const int handle = script->popSpriteReferenceValue();
                SPRITE* const sprite = scriptResolveSpriteReference(handle);
                PushInt(sprite ? sprite->Animation() : 0);
                return 0;
            }
        case script::NativeFunctionCode::DirectionTo:
{
                const int y = PopInt();
                const int x = PopInt();
                const int handle = script->popSpriteReferenceValue();
                SPRITE* const sprite = scriptResolveSpriteReference(handle);
                int value = 0;
                if (sprite)
                    value = sprite->DirectionTo(VECTOR2{static_cast<float>(x), static_cast<float>(y)}).Int();
                PushInt(value);
                return 0;
            }
        case script::NativeFunctionCode::GetCommands:
{
                assignStringFromCString(str, Class);
                const int handle = script->popSpriteReferenceValue();
                SPRITE* const sprite = scriptResolveSpriteReference(handle);
                if (!sprite)
                    return (PushStr(str), 0);

                const DWORD spriteClass = sprite->Vid()->spriteClass;
                if (spriteClass == 2u || spriteClass == 0x18u || spriteClass == 3u || spriteClass == 7u)
                {
                    STRING commandWordsText = sprite->GetTextItems();
                    assignStringFromString(str, commandWordsText);
                    commandWordsText.ReleaseOwnedStorage();
                }

                STRING commandRecordsText = sprite->GetTextActions();
                appendStringOwner(str, commandRecordsText);
                commandRecordsText.ReleaseOwnedStorage();

                return (PushStr(str), 0);
            }
        case script::NativeFunctionCode::SetCommands:
{
                assignStringFromString(str, *PopStr());

                const int handle = script->popSpriteReferenceValue();
                if (handle == 0)
                    return 0;

                SPRITE* const sprite = scriptResolveSpriteReference(handle);
                if (!sprite)
                    return 0;

                const DWORD spriteClass = sprite->Vid()->spriteClass;
                if (spriteClass == 2u || spriteClass == 0x18u || spriteClass == 3u || spriteClass == 7u)
                    sprite->SetTextItems(&str);

                static const char kCommandSectionDelimiter[] = { '\x02', '\0' };
                if (std::strstr(str.c_str(), kCommandSectionDelimiter))
                {
                    STRING commandRecordsOnlyText;
                    constructRightOfFirstMarker(str, commandRecordsOnlyText, kCommandSectionDelimiter);
                    assignStringFromString(str, commandRecordsOnlyText);
                    commandRecordsOnlyText.ReleaseOwnedStorage();
                }

                sprite->SetTextActions(&str);

                return 0;
            }
        case script::NativeFunctionCode::Load:
{

                        const STRING path = *PopStr();
                        const std::uint32_t flags = core::ApplicationFlags() | application_flags::PendingCommandOrLoad;
                        core::SetApplicationFlags(flags);
                        win::applicationWinInstance()->setPendingCommand(path);
                        win::applicationWinInstance()->setFlags(flags);
                        return 0;
            }
        case script::NativeFunctionCode::Save:
{
                        const STRING path = *PopStr();
                        win::applicationWinInstance()->saveMap(path);
                        return 0;
            }
        case script::NativeFunctionCode::SaveDemo:
{
                RESOURCE& demoResource = Map->demoResource();
                if (demoResource.isOpen())
                    return 0;

                const STRING& path = *PopStr();
                demoResource.OpenForWrite(&path, RESOURCE::ResTypes::DEMO);
                return 0;
            }
        case script::NativeFunctionCode::MenuFind:
{
                const int ndir = PopInt();
                VID* const vid = PopVid("for MenuFind");

                SPRITE* found = nullptr;
                if (vid != EmptyVid && vid->totalSpriteCount() != 0)
                {
                    BaseSpriteList<0>& list = applicationFrameSpriteList();
                    const int count = list.activeCount();
                    for (int i = 0; i < count; ++i)
                    {
                        SPRITE* const sprite = list.at(i);
                        if (!sprite || sprite->Vid() != vid)
                            continue;
                        if (ndir != 999999)
                        {
                            const std::uint32_t directionByte =
                                static_cast<std::uint32_t>(sprite->directionIndex()
                                    + vid->directionQuantizationOffset()) & 0xFFu;
                            const int directionIndex = static_cast<int>(
                                (directionByte * static_cast<std::uint32_t>(vid->directionCount())) >> 8);
                            const int rawDirectionSelector = 999000 + sprite->directionIndex();
                            if (directionIndex != ndir && rawDirectionSelector != ndir)
                                continue;
                        }
                        found = sprite;
                        break;
                    }
                }
                PushObject(scriptSpritePointerValue(found), nullptr);
                return 0;
            }
        case script::NativeFunctionCode::MenuLoad:
{


                const STRING path = *PopStr();
                (void)applicationMenu().Load(path);
                return 0;
            }
        case script::NativeFunctionCode::MenuRelease:
{
                const STRING path = *PopStr();
                if (path.isEmpty())
                    applicationFrameSpriteList().deleteAllSprites();
                else
                    (void)applicationMenu().DeleteFromFile(path);
                return 0;
            }
        case script::NativeFunctionCode::MenuNvidUnderCursor:
{
                PushInt(applicationMenu().NVidUnderCursor());
                return 0;
            }
        case script::NativeFunctionCode::MenuNdirUnderCursor:
{
                PushInt(applicationMenu().NDirUnderCursor());
                return 0;
            }
        case script::NativeFunctionCode::MenuAction:
{
                const int var3 = PopInt();
                const int var2 = PopInt();
                const int var1 = PopInt();
                const int action = PopInt();
                const int ndir = PopInt();
                VID* const vid = PopVid("for MenuAction");
                if (vid == EmptyVid)
                    return 0;

                BaseSpriteList<0>& list = applicationFrameSpriteList();
                const int count = list.activeCount();
                for (int i = 0; i < count; ++i)
                {
                    SPRITE* const sprite = list.at(i);
                    if (!sprite || sprite->Vid() != vid)
                        continue;
                    if (ndir != 999999)
                    {
                        const std::uint32_t directionByte =
                            static_cast<std::uint32_t>(sprite->directionIndex()
                                + vid->directionQuantizationOffset()) & 0xFFu;
                        const int directionIndex = static_cast<int>(
                            (directionByte * static_cast<std::uint32_t>(vid->directionCount())) >> 8);
                        const int rawDirectionSelector = 999000 + sprite->directionIndex();
                        if (directionIndex != ndir && rawDirectionSelector != ndir)
                            continue;
                    }

                    if (action < 0x11)
                        sprite->ChangeAnimation(action);
                    else
                        (void)sprite->dispatchVirtualAction(
                            static_cast<std::uint32_t>(action), var1, var2, var3);
                }
                return 0;
            }
        case script::NativeFunctionCode::MenuFindNamed:
{
                const STRING name = *PopStr();
                PushObject(scriptSpritePointerValue(applicationMenu().SpriteWithName(name)), nullptr);
                return 0;
            }
        case script::NativeFunctionCode::MenuSpriteUnderCursor:
{
                PushObject(scriptSpritePointerValue(applicationMenu().selectedSprite()), nullptr);
                return 0;
            }
        case script::NativeFunctionCode::MenuCreate:
{
                const int z = PopInt();
                const int yDelta = PopInt();
                const int y = z + yDelta;
                const int x = PopInt();
                const int directionSource = PopInt();
                VID* const vid = PopVid("for MenuCreate");
                if (vid == EmptyVid)
                {
                    PushInt(0);
                    return 0;
                }

                const int direction = ((directionSource << 8) / static_cast<int>(vid->directionCount())) & 0xFF;
                SPRITE* const created = Map->CreateSprite(
                    vid, VECTOR(static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)),
                    ANGLE(direction), nullptr, false);
                PushObject(scriptSpritePointerValue(created), nullptr);
                return 0;
            }
        case script::NativeFunctionCode::MenuLeftClick:
{
                MENU& list = applicationMenu();
                SPRITE* const selected = (list.controlFlags() & 1u) != 0u
                    ? list.selectedSprite()
                    : nullptr;
                PushObject(scriptSpritePointerValue(selected), nullptr);
                return 0;
            }
        case script::NativeFunctionCode::GetInputX:
{
                PushInt(static_cast<int>(scriptApplicationInputState().clientX));
                return 0;
            }
        case script::NativeFunctionCode::GetInputY:
{
                PushInt(static_cast<int>(scriptApplicationInputState().clientY));
                return 0;
            }
        case script::NativeFunctionCode::GetKey:
{
                const int value = static_cast<int>(scriptApplicationInputState().lastCode);
                PushInt(value);
                return 0;
            }
        case script::NativeFunctionCode::MenuMode:
{
                const int value = PopInt();
                auto* const application = reinterpret_cast<core::Application*>(core::ApplicationOwner());
                if (value != 0)
                {

                    application->beginBucketTimingSnapshot();
                    mouseInstanceRef()->ChangeAnimation(0);
                }
                else
                {

                    application->endBucketTimingSnapshot();
                }
                return 0;
            }
        case script::NativeFunctionCode::SetCursor:
{
                const int cursorId = PopInt();
                MOUSE* mouse = mouseInstanceRef();

                if (cursorId == -1)
                {
                    mouse->Disable();
                    return 0;
                }
                if (cursorId == 0x100)
                {
                    mouse->HardwareOn();
                    return 0;
                }
                if (cursorId == 0x101)
                {
                    mouse->HardwareOff();
                    return 0;
                }

                if (!mouse->cursorHandlesLoaded())
                    mouse->Enable();
                mouse = mouseInstanceRef();
                mouse->ChangeAnimation(cursorId);
                return 0;
            }
        case script::NativeFunctionCode::MessageText:
{
                        const int y = PopInt();
                        const int x = PopInt();
                        STRING text = *PopStr();
                        win::ApplicationWin* const app = win::applicationWinInstance();
                        PLAYER* const player = app->startupPlayerSlotByIndex(
                            static_cast<int>(app->activeStartupPlayerIndex()));


                        using PlayerCoordinateFn = int (__thiscall*)(PLAYER*, STRING*, float, float);
                        void** const playerVtable = *reinterpret_cast<void***>(player);
                        (void)reinterpret_cast<PlayerCoordinateFn>(playerVtable[9])(
                            player, &text, static_cast<float>(x), static_cast<float>(y));
                        return 0;
            }
        case script::NativeFunctionCode::GetInputState:
{
                const std::uint32_t state = scriptApplicationInputState().flags;
                const int bitOrder[] = {15, 14, 9, 10, 8, 7, 12, 11, 6, 5, 2, 0};
                int packed = 0;
                for (int bit : bitOrder)
                    packed = (packed << 1) | static_cast<int>((state >> bit) & 1u);
                PushInt(packed);
                return 0;
            }
        case script::NativeFunctionCode::SetShiftCoor:
{
                const int y = PopInt();
                const int x = PopInt();
                Map->SetShiftCoor(static_cast<float>(x), static_cast<float>(y), 0);
                return 0;
            }
        case script::NativeFunctionCode::SetScrollType:
{
                core::SetApplicationScrollType(static_cast<std::uint32_t>(PopInt()));
                return 0;
            }
        case script::NativeFunctionCode::GetScrollType:
{
                PushInt(static_cast<int>(core::ApplicationScrollType()));
                return 0;
            }
        case script::NativeFunctionCode::ScreenX:
{
                const int value = static_cast<int>(Graph->SizeX());
                PushInt(value);
                return 0;
            }
        case script::NativeFunctionCode::ScreenY:
{
                const int value = static_cast<int>(Graph->SizeY());
                PushInt(value);
                return 0;
            }
        case script::NativeFunctionCode::SetApplicationFlag7:
{
                const int value = PopInt();
                std::uint32_t flags = core::ApplicationFlags();
                flags = (flags & ~application_flags::ScriptControlBit7) |
                    (value != 0 ? application_flags::ScriptControlBit7 : 0u);
                core::SetApplicationFlags(flags);
                return 0;
            }
        case script::NativeFunctionCode::PlayerNoop:
{
                const int value = PopInt();
                PLAYER* const player = scriptPlayerSlot(static_cast<int>(core::ActivePlayerIndex()));
                using PlayerReservedFn = void (__thiscall*)(PLAYER*);
                void** const playerVtable = *reinterpret_cast<void***>(player);
                reinterpret_cast<PlayerReservedFn>(playerVtable[value != 0 ? 7 : 8])(player);
                return 0;
            }
        case script::NativeFunctionCode::GetString:
{
                assignStringFromString(str, STRING());
                assignStringFromString(str, *PopStr());

                const STRING section = *PopStr();

                const STRING& profilePath = *core::g_startupStringsIniPathOwner;

                STRING defaultValue;
                STRING profileValue;
                core::profile_p::readProfileStringInto(profileValue, profilePath, section, str, defaultValue);
                PushStr(profileValue);
                profileValue.ReleaseOwnedStorage();
                if (defaultValue.isEmpty())
                    return 0;
                defaultValue.ReleaseOwnedStorage();
                return 0;
            }
        case script::NativeFunctionCode::Exit:
{
                        const STRING reason = *PopStr();
                        (void)reason;
                        if (win::ApplicationWin* const app = win::applicationWinInstance())
                            if (HWND hwnd = app->nativeWindow())
                                ::PostMessageA(hwnd, WM_CLOSE, 0, 0);
                        return 0;
            }
        case script::NativeFunctionCode::ToScreenX:
{
                const int x = PopInt();
                const int value = static_cast<int>(static_cast<float>(x) - core::GlobalApplicationDrawDispatcherState().cameraShiftX());
                PushInt(value);
                return 0;
            }
        case script::NativeFunctionCode::ToScreenY:
{
                const int z = PopInt();
                const int y = PopInt();
                const int value = static_cast<int>(static_cast<float>(y) - static_cast<float>(z) - core::GlobalApplicationDrawDispatcherState().cameraShiftY());
                PushInt(value);
                return 0;
            }
        case script::NativeFunctionCode::MenuRightClick:
{
                MENU& list = applicationMenu();
                SPRITE* const selected = (list.controlFlags() & 2u) != 0u
                    ? list.selectedSprite()
                    : nullptr;
                PushObject(scriptSpritePointerValue(selected), nullptr);
                return 0;
            }
        case script::NativeFunctionCode::SetMouseClick:
{
                const int vkButton = PopInt();
                const int firstOrSecond = PopInt();
                input::InputControlKeys& keys = input::g_inputControlKeys;
                if (firstOrSecond == 0x400)
                {
                    keys.first0 = static_cast<std::uint32_t>(vkButton);
                    keys.first1 = static_cast<std::uint32_t>(vkButton);
                    return 0;
                }
                if (firstOrSecond == 0x800)
                {
                    keys.second0 = static_cast<std::uint32_t>(vkButton);
                    keys.second1 = static_cast<std::uint32_t>(vkButton);
                    return 0;
                }
                return 0;
            }
        case script::NativeFunctionCode::CursorAction:
{
                const int var1 = PopInt();
                const int var2 = PopInt();
                Mouse->Action(63, static_cast<std::intptr_t>(var2), var1, 0);
                return 0;
            }
        case script::NativeFunctionCode::SetSoundVolume:
{
                const int volume = PopInt();
                sound::g_globalSoundEngine->applyMasterVolumePercent(volume);
                return 0;
            }
        case script::NativeFunctionCode::SetMusicVolume:
{
                const int volume = PopInt();
                sound::g_globalSoundEngine->applyMusicVolumePercent(volume);
                return 0;
            }
        case script::NativeFunctionCode::PlaySfx:
{
                const int nsfx = PopInt();
                sound::g_globalSoundEngine->enqueueSoundRequest(nsfx, 0, 0);
                return 0;
            }
        case script::NativeFunctionCode::StopSfx:
{
                const int nsfx = PopInt();
                sound::g_globalSoundEngine->stopSoundNumber(nsfx);
                return 0;
            }
        case script::NativeFunctionCode::StopMusic:
{

                const int fadeTime = PopInt();
                sound::g_globalSoundEngine->stopMusic(fadeTime);
                return 0;
            }
        case script::NativeFunctionCode::PlaySfxFromCoor:
{
                const int y = PopInt();
                const int x = PopInt();
                const int nsfx = PopInt();
                GRAPH* const graph = Graph;
                const core::ApplicationDrawDispatcherState& drawState = core::GlobalApplicationDrawDispatcherState();
                const float halfScreenX = static_cast<float>(graph->SizeX()) * 0.5f;
                const float halfScreenY = static_cast<float>(graph->SizeY()) * 0.5f;
                const float soundX = static_cast<float>(x) - drawState.cameraShiftX() - halfScreenX;
                const float soundY = static_cast<float>(y) - drawState.cameraShiftY() - halfScreenY;
                sound::g_globalSoundEngine->enqueueSoundRequestFromCoordinates(nsfx, soundX, soundY);
                return 0;
            }
        case script::NativeFunctionCode::PlayMusicFile:
{


                const int fadeTime = PopInt();
                const int loop = PopInt();
                const STRING filename = *PopStr();
                sound::g_globalSoundEngine->playMusicFile(filename.c_str(), loop, fadeTime);
                return 0;
            }
        case script::NativeFunctionCode::Effect:
{
                const int duration = PopInt();
                const int var2 = PopInt();
                const int var1 = PopInt();
                const int effect = PopInt();
                (void)Graph->Effect(effect, var1, var2, duration);
                return 0;
            }
        case script::NativeFunctionCode::SetEnvironment:
{
                const int value = PopInt();
                Graph->SetEnvironment(static_cast<std::uint32_t>(value));
                return 0;
            }
        case script::NativeFunctionCode::SetGraphDetail:
        case script::NativeFunctionCode::SetAutoReBirth:
{
                (void)PopInt();
                return 0;
            }
        case script::NativeFunctionCode::SetGamma:
{
                const int gammaIndex = PopInt();
                std::uint32_t diffuse = 0;
                std::uint32_t specular = 0;
                scriptNativeDecodeGammaIndex(gammaIndex, diffuse, specular);
                Graph->setGamma(diffuse, specular);
                return 0;
            }
        case script::NativeFunctionCode::SetWind:
{
                const int direct = PopInt();
                const int wind = PopInt();
                Graph->SetWind(wind, ANGLE(static_cast<unsigned char>(direct)));
                return 0;
            }
        case script::NativeFunctionCode::PlayMovie:
{
                const STRING filename = *PopStr();
                Graph->PlayMovie(&filename);
                return 0;
            }
        case script::NativeFunctionCode::IsPlayMovie:
{
                const int value = Graph->movieComObject(0) != nullptr ? 1 : 0;
                PushInt(value);
                return 0;
            }
        case script::NativeFunctionCode::StopMovie:
{
                Graph->releaseMoviePlayback();
                return 0;
            }
        case script::NativeFunctionCode::IsPlayMusic:
{


                const std::uint8_t* const soundOwner = reinterpret_cast<const std::uint8_t*>(
                    sound::g_globalSoundEngine);
                void* const streamOwner =
                    *reinterpret_cast<void* const*>(soundOwner + 0x410u);
                int value = 0;
                if (streamOwner != nullptr)
                {
                    using StreamStatusMethod = int(__thiscall*)(void*);
                    void* const* const streamVtable = *reinterpret_cast<void* const* const*>(streamOwner);
                    const auto streamStatus = reinterpret_cast<StreamStatusMethod>(streamVtable[1]);
                    if (streamStatus(streamOwner) != 0)
                    {
                        value = 1;
                    }
                    else if (*reinterpret_cast<const std::uint32_t*>(soundOwner + 0x40Cu) != 0u)
                    {
                        value = 1;
                    }
                    else
                    {
                        const char* const pendingMusicPath =
                            *reinterpret_cast<const char* const*>(soundOwner + 0x414u);
                        value = std::strcmp(pendingMusicPath, STRING::SharedEmptyText()) != 0 ? 1 : 0;
                    }
                }
                PushInt(value);
                return 0;
            }
        case script::NativeFunctionCode::CountGamma:
{
                const int time = PopInt();
                const int g2 = PopInt();
                const int g1 = PopInt();
                PushInt(CountGamma(g1, g2, time));
                return 0;
            }
        case script::NativeFunctionCode::GetGamma:
{
                const Gamma& gamma = Graph->rawGammaPair();
                std::uint32_t out = (gamma.first >> 1) & 0x7F7F7F7Fu;
                for (int shift = 0; shift < 32; shift += 8)
                {
                    const std::uint32_t specByte = (gamma.second >> shift) & 0xFFu;
                    if (specByte != 0)
                    {
                        const std::uint32_t packedByte = 0x80u | (((~specByte) & 0xFEu) >> 1);
                        out = (out & ~(0xFFu << shift)) | ((packedByte & 0xFFu) << shift);
                    }
                }
                PushInt(static_cast<int>(out));
                return 0;
            }
        case script::NativeFunctionCode::GetEffectState:
{
                const int effect = PopInt();
                PushInt(Graph->GetEffectState(effect));
                return 0;
            }
        case script::NativeFunctionCode::GetPrevMapName:
{
                PushStr(core::ApplicationPreviousMapName());
                return 0;
            }
        case script::NativeFunctionCode::SetGraphScreen:
{


                static STRING zs1Native150String;
                PushStr(zs1Native150String);
                return 0;
            }
        case script::NativeFunctionCode::GetMapName:
{
                PushStr(core::ApplicationCurrentMapName());
                return 0;
            }
        case script::NativeFunctionCode::ExecuteShellFile:
{
                str.Assign(PopStr()->c_str());
                LOG::Write("Exec '%s'", str.c_str());

                STRING shellParameters = str.After(" ");
                STRING shellExecutable = str.Before(" ");
                ShellExecuteA(nullptr,
                              nullptr,
                              shellExecutable.c_str(),
                              shellParameters.c_str(),
                              nullptr,
                              5);
                return 0;
            }
        case script::NativeFunctionCode::CharAt:
{
                const int index = PopInt();
                const STRING text = *PopStr();

                const int value = static_cast<int>(static_cast<signed char>(text.c_str()[index]));
                PushInt(value);
                return 0;
            }
        case script::NativeFunctionCode::Log:
{

                const STRING text = *PopStr();
                LOG::Write(text.c_str());
                return 0;
            }
        case script::NativeFunctionCode::Random:
{


                const int maxValue = PopInt();
                const int value = Random(maxValue);
                PushInt(value);
                return 0;
            }
        case script::NativeFunctionCode::ChangeZUnit:
{
                const int z = PopInt();
                VID* const vid = PopVid("for ChangeZUnit");
                if (vid == EmptyVid)
                    return 0;

                const int pass = vid->renderLayer();
                int cursor = drawState.drawPassBucket(pass).count();
                SPRITE* sprite = core::Application::previousSpriteInDrawPass(drawState, pass, &cursor);
                while (sprite)
                {
                    if (sprite->Vid() == vid)
                        sprite->ChangeCoor(sprite->X(), sprite->Y(), static_cast<float>(z));
                    sprite = core::Application::previousSpriteInDrawPass(drawState, pass, &cursor);
                }
                return 0;
            }
        case script::NativeFunctionCode::GetTime:
{
                PushInt(static_cast<int>(core::CurrentTimeMilliseconds()));
                return 0;
            }
        case script::NativeFunctionCode::GetGroundZ:
{
                const int y = PopInt();
                const int x = PopInt();
                const int value = static_cast<int>(Map->GetGroundZ(VECTOR2{static_cast<float>(x), static_cast<float>(y)}));
                PushInt(value);
                return 0;
            }
        case script::NativeFunctionCode::StringLength:
        case script::NativeFunctionCode::StringLengthCompat:
{
                const STRING& text = *PopStr();
                PushInt(text.Length());
                return 0;
            }
        case script::NativeFunctionCode::SetFlagman:
{
                const int spriteHandle = script->popSpriteReferenceValue();
                const int playerIndex = PopInt();
                Map->SetFlagman(playerIndex, scriptResolveSpriteReference(spriteHandle));
                return 0;
            }
        case script::NativeFunctionCode::AskPlace:
{
                const int z = PopInt();
                const int y = PopInt();
                const int x = PopInt();
                VID* const vid = PopVid("for CanPlace");

                int handle = 0;
                if (SPRITE* const hit = GlobalSpriteCollectorCanPlace(
                        *Map, vid, static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)))
                {
                    handle = scriptSpritePointerValue(hit);
                }
                PushObject(handle, nullptr);
                return 0;
            }
        case script::NativeFunctionCode::GetVidData:
{
                const int type = PopInt();


                VID* const vid = PopVid(
                    type == script::toInt(script::VidDataCode::Validate) ? "" : "for GetVid");
                if (vid == EmptyVid)
                {
                    PushInt(0);
                    return 0;
                }

                switch (static_cast<script::VidDataCode>(type))
                {
                case script::VidDataCode::MaxHp:
                    PushInt(static_cast<int>(vid->maxHp));
                    return 0;
                case script::VidDataCode::Gamma0:
                case script::VidDataCode::Gamma1:
                case script::VidDataCode::Gamma2:
                case script::VidDataCode::Gamma3:
                {


                    const Gamma& raw = vid->armyGammaOverride(
                        static_cast<unsigned>(type - script::toInt(script::VidDataCode::Gamma0)));
                    PushInt(static_cast<int>(raw.toDword()));
                    return 0;
                }
                case script::VidDataCode::Property:
                    PushInt(static_cast<int>(vid->properties()));
                    return 0;
                case script::VidDataCode::BattleRange:
                    PushInt(static_cast<int>(vid->weaponBattleRange()));
                    return 0;
                case script::VidDataCode::Ammo:
                    PushInt(vid->GetMaxAmmo());
                    return 0;
                case script::VidDataCode::Name:
                    PushStr(vid->scriptName());
                    return 0;
                case script::VidDataCode::Count:
                    PushInt(vid->totalSpriteCount());
                    return 0;
                case script::VidDataCode::KilledUnit:
                    PushInt(vid->totalKilledUnitCount());
                    return 0;
                case script::VidDataCode::KilledUnitArmy0:
                case script::VidDataCode::KilledUnitArmy1:
                case script::VidDataCode::KilledUnitArmy2:
                case script::VidDataCode::KilledUnitArmy3:
                    PushInt(vid->killedUnitCountForArmy(type - script::toInt(script::VidDataCode::KilledUnitArmy0)));
                    return 0;
                case script::VidDataCode::CountArmy0:
                case script::VidDataCode::CountArmy1:
                case script::VidDataCode::CountArmy2:
                case script::VidDataCode::CountArmy3:
                    PushInt(vid->spriteCountForBucket(type - script::toInt(script::VidDataCode::CountArmy0)));
                    return 0;
                case script::VidDataCode::MaxHpArmy0:
                case script::VidDataCode::MaxHpArmy1:
                case script::VidDataCode::MaxHpArmy2:
                case script::VidDataCode::MaxHpArmy3:
                    PushInt(vid->GetMaxHp(type - script::toInt(script::VidDataCode::MaxHpArmy0)));
                    return 0;
                case script::VidDataCode::SpriteType:
                    PushInt(static_cast<int>(vid->spriteTypeId()));
                    return 0;
                case script::VidDataCode::Class:
                    PushInt(static_cast<int>(vid->spriteClassId()));
                    return 0;
                case script::VidDataCode::Speed:
                    PushInt(vid->maxSpeedValue() == 999999.0f
                        ? 999999
                        : static_cast<int>(vid->maxSpeedValue() * 1000.0f));
                    return 0;
                case script::VidDataCode::Lifetime:
                    PushInt(vid->lifetimeValue());
                    return 0;
                case script::VidDataCode::DetectRange:
                    PushInt(static_cast<int>(vid->weaponDetectRange()));
                    return 0;
                case script::VidDataCode::WeaponAim:
                    PushInt(static_cast<int>(vid->weaponAim()));
                    return 0;
                case script::VidDataCode::DirectionCount:
                    PushInt(vid->directionCount());
                    return 0;
                case script::VidDataCode::MoveMask:
                    PushInt(static_cast<int>(vid->movementMask()));
                    return 0;
                case script::VidDataCode::BuildTime:
                    PushInt(vid->weaponBuildTime());
                    return 0;
                case script::VidDataCode::Hide:
                    PushInt(vid->PropHide());
                    return 0;
                case script::VidDataCode::NotCreateAsChild:
                    PushInt(vid->PropNotCreateAsChild());
                    return 0;
                case script::VidDataCode::FrameSpeed:
                    PushInt(static_cast<int>(vid->defaultFrameSpeed()));
                    return 0;
                case script::VidDataCode::Link:
                    PushInt(vid->linkedVid() ? vid->linkedVid()->nvid() : 0);
                    return 0;
                case script::VidDataCode::Damage:
                    PushInt(vid->deathDamageMinimumRawBits());
                    return 0;
                case script::VidDataCode::RecolorUnit:
                    PushInt(vid->totalRecolorUnitCount());
                    return 0;
                case script::VidDataCode::RecolorUnitArmy0:
                case script::VidDataCode::RecolorUnitArmy1:
                case script::VidDataCode::RecolorUnitArmy2:
                case script::VidDataCode::RecolorUnitArmy3:
                    PushInt(vid->recolorUnitCountForArmy(type - script::toInt(script::VidDataCode::RecolorUnitArmy0)));
                    return 0;
                case script::VidDataCode::ReloadTime:
                    PushInt(vid->weaponReloadTime());
                    return 0;
                case script::VidDataCode::DeathRange:
                    PushInt(static_cast<int>(vid->deathRangeValue()));
                    return 0;
                case script::VidDataCode::SizeX:
                    PushInt(static_cast<int>(vid->sizeXYZ.x));
                    return 0;
                case script::VidDataCode::SizeY:
                    PushInt(static_cast<int>(vid->sizeXYZ.y));
                    return 0;
                case script::VidDataCode::SizeZ:
                    PushInt(static_cast<int>(vid->sizeXYZ.z));
                    return 0;
                case script::VidDataCode::ScaleX:
                    PushInt(static_cast<int>(vid->scaleXYZ.x * 1000.0f));
                    return 0;
                case script::VidDataCode::ScaleY:
                    PushInt(static_cast<int>(vid->scaleXYZ.y * 1000.0f));
                    return 0;
                case script::VidDataCode::ScaleZ:
                    PushInt(static_cast<int>(vid->scaleXYZ.z * 1000.0f));
                    return 0;
                case script::VidDataCode::Validate:
                    PushInt(1);
                    return 0;
                default:
                    break;
                }


                if (type >= script::VidChildFirst && type < script::VidChildEnd)
                {
                    VID* child = vid->childVidForDataCode(type);
                    if (!child)
                        return (PushInt(0), 0);
                    PushInt(child->nvid());
                    return 0;
                }

                if (type >= script::VidNoChildFirst && type < script::VidNoChildEnd)
                {
                    PushInt(vid->noChildValueForDataCode(type));
                    return 0;
                }

                script->RunTimeError(0x0E, "GetVid type", type);
                return 0;

            }
        case script::NativeFunctionCode::SetVidData:
{
                const int value = PopInt();
                const int type = PopInt();
                VID* vid = PopVid("for SetVid");
                if (vid == EmptyVid)
                    return 0;

                switch (static_cast<script::VidDataCode>(type))
                {
                case script::VidDataCode::MaxHp:
                    vid->maxHp = value;
                    return 0;
                case script::VidDataCode::BattleRange:


                    vid->setWeaponBattleRange(static_cast<float>(value));
                    return 0;
                case script::VidDataCode::Ammo:
                {
                    VID* target = vid;
                    VID* link = vid->linkedVid();
                    if (link && link->CanFight() != 0)
                        target = link;
                    target->setWeaponRecordAmmoCapacity(value);
                    return 0;
                }
                case script::VidDataCode::KilledUnit:
                    vid->setKilledUnitCountForArmy(3, value);
                    vid->setKilledUnitCountForArmy(2, value);
                    vid->setKilledUnitCountForArmy(1, value);
                case script::VidDataCode::KilledUnitArmy0:
                    vid->setKilledUnitCountForArmy(0, value);
                    return 0;
                case script::VidDataCode::KilledUnitArmy1:
                    vid->setKilledUnitCountForArmy(1, value);
                    return 0;
                case script::VidDataCode::KilledUnitArmy2:
                    vid->setKilledUnitCountForArmy(2, value);
                    return 0;
                case script::VidDataCode::KilledUnitArmy3:
                    vid->setKilledUnitCountForArmy(3, value);
                    return 0;
                case script::VidDataCode::MaxHpArmy0:
                case script::VidDataCode::MaxHpArmy1:
                case script::VidDataCode::MaxHpArmy2:
                case script::VidDataCode::MaxHpArmy3:
                    vid->SetMaxHp(type - script::toInt(script::VidDataCode::MaxHpArmy0), value);
                    return 0;
                case script::VidDataCode::HpCoeffArmy0:
                case script::VidDataCode::HpCoeffArmy1:
                case script::VidDataCode::HpCoeffArmy2:
                case script::VidDataCode::HpCoeffArmy3:
                    vid->SetHpCoeff(type - script::toInt(script::VidDataCode::HpCoeffArmy0), value);
                    return 0;
                case script::VidDataCode::Speed:
                {


                    const float speedValue =
                        value == 999999 ? 999999.0f : static_cast<float>(value) * 0.001f;
                    vid->setScriptSpeedValue(speedValue);
                    const core::ApplicationDrawPassBucket& bucket =
                        core::GlobalApplicationDrawDispatcherState().drawPassBucket(vid->renderLayer());
                    SPRITE* const* slots = bucket.data();
                    for (int i = bucket.count() - 1; i >= 0; --i)
                    {
                        SPRITE* const sprite = slots ? slots[i] : nullptr;
                        if (sprite && sprite->Vid() == vid)
                            sprite->syncExDataMaxSpeedFromVid();
                    }
                    return 0;
                }
                case script::VidDataCode::Lifetime:
                {
                    if (vid->nvid() == 0)
                        return 0;
                    vid->setLifetimeValue(value);
                    vid->setActionAuxStateRequired(1);
                    return 0;
                }
                case script::VidDataCode::DetectRange:
                    vid->setWeaponDetectRange(static_cast<float>(value));
                    return 0;
                case script::VidDataCode::WeaponAim:
                    vid->setWeaponAim(static_cast<float>(value));
                    return 0;
                case script::VidDataCode::ExchangeVid:
                    if (!isValidNvid(value))
                    {
                        script->RunTimeError(4, "SetVid get_image", value);
                        return 0;
                    }
                    (void)Map->ExchangeVid(vid, resolveVidByNvid(value));
                    return 0;
                case script::VidDataCode::MoveMask:
                    vid->setMovementMask(static_cast<DWORD>(value));
                    return 0;
                case script::VidDataCode::BuildTime:
                    vid->setWeaponBuildTime(value);
                    return 0;
                case script::VidDataCode::Hide:
                    vid->SetPropHide(value);
                    return 0;
                case script::VidDataCode::NotCreateAsChild:
                    vid->SetPropNotCreateAsChild(value);
                    return 0;
                case script::VidDataCode::FrameSpeed:
                    vid->setAllFrameSpeeds(value);
                    return 0;
                case script::VidDataCode::Link:
                {
                    vid->setLinkedVid(resolveVidByNvid(value));
                    return 0;
                }
                case script::VidDataCode::Damage:
                    vid->setDeathDamageMinimumRawBits(value);
                    return 0;
                case script::VidDataCode::RecolorUnit:

                    vid->SetReColorForArmy(value);
                    return 0;
                case script::VidDataCode::ReloadTime:
                    vid->setWeaponReloadTime(value);
                    return 0;
                case script::VidDataCode::DeathRange:
                    vid->deathRange = static_cast<float>(value);
                    return 0;
                case script::VidDataCode::SizeX:
                    vid->sizeXYZ.x = static_cast<float>(value);
                    return 0;
                case script::VidDataCode::SizeY:
                    vid->sizeXYZ.y = static_cast<float>(value);
                    return 0;
                case script::VidDataCode::SizeZ:
                    vid->sizeXYZ.z = static_cast<float>(value);
                    return 0;
                case script::VidDataCode::ScaleX:
                    vid->scaleXYZ.x = static_cast<float>(value) * 0.001f;
                    return 0;
                case script::VidDataCode::ScaleY:
                    vid->scaleXYZ.y = static_cast<float>(value) * 0.001f;
                    return 0;
                case script::VidDataCode::ScaleZ:
                    vid->scaleXYZ.z = static_cast<float>(value) * 0.001f;
                    return 0;
                default:
                    break;
                }


                if (!vid)
                    return 0;

                if (type >= script::VidChildFirst && type < script::VidChildEnd)
                {
                    if (value == 0)
                    {
                        vid->setChildNvidForDataCode(type, 0);
                        vid->setChildVidForDataCode(type, nullptr);
                        return 0;
                    }

                    const int absValue = value < 0 ? -value : value;
                    const bool validChildSlot = isValidNvid(absValue);
                    if (!validChildSlot)
                    {
                        script->RunTimeError(4, "SetVid child", value);
                        return 0;
                    }

                    VID* childForStore = resolveVidByNvid(absValue);
                    vid->setChildNvidForDataCode(type, value);
                    vid->setChildVidForDataCode(type, childForStore);
                    VID* childForFlag = resolveVidByNvid(absValue);
                    if (!childForFlag || childForFlag->PropBirthAsSmoke() == 0)
                        return 0;
                    vid->setActionAuxStateRequired(vid->actionAuxStateRequired() | 1);
                    return 0;
                }

                if (type >= script::toInt(script::VidDataCode::Gamma0) && type <= script::toInt(script::VidDataCode::Gamma3))
                {
                    const Gamma rawGamma(Gamma::DECODE, static_cast<DWORD>(value));
                    vid->SetGamma(rawGamma, static_cast<unsigned>(type - script::toInt(script::VidDataCode::Gamma0)));
                    return 0;
                }

                if (type >= script::VidNoChildFirst && type < script::VidNoChildEnd)
                {
                    vid->setNoChildValueForDataCode(type, value);
                    return 0;
                }

                script->RunTimeError(0x0E, "SetVid type", type);
                return 0;

            }
        case script::NativeFunctionCode::IntToString:
{
                const int value = PopInt();
                char buffer[128];
                std::snprintf(buffer, sizeof(buffer), "%d", value);
                PushStr(STRING(buffer));
                return 0;
            }
        case script::NativeFunctionCode::Sin:
{
                const int angle = PopInt();
                PushInt(scriptNativeSin1024(angle));
                return 0;
            }
        case script::NativeFunctionCode::Cos:
{
                const int angle = PopInt();
                PushInt(scriptNativeCos1024(angle));
                return 0;
            }
        case script::NativeFunctionCode::MapSizeX:
{
                        const int value = static_cast<int>(win::applicationWinInstance()->mapExtentX());
                        PushInt(value);
                        return 0;
            }
        case script::NativeFunctionCode::MapSizeY:
{
                        const int value = static_cast<int>(win::applicationWinInstance()->mapExtentY());
                        PushInt(value);
                        return 0;
            }
        case script::NativeFunctionCode::Genocide:
{
                VID* const vid = PopVid("for Genocide");
                if (!vid || vid == EmptyVid)
                    return 0;

                const int pass = vid->renderLayer();
                int cursor = 0;
                SPRITE* sprite = beginReverseDrawPassIteration(drawState, pass, &cursor);
                while (sprite)
                {
                    if (sprite->Vid() == vid)
                        DeleteSpriteThroughVirtualDeletingDestructor(sprite);
                    sprite = core::Application::previousSpriteInDrawPass(drawState, pass, &cursor);
                }
                return 0;
            }
        case script::NativeFunctionCode::ReplaceUnit:
{
                VID* const replacement = PopVid("for Replace Unit 2");
                VID* const source = PopVid("for Replace Unit 1");
                if (replacement == EmptyVid || source == EmptyVid)
                    return 0;

                const int pass = source->renderLayer();
                int cursor = 0;
                SPRITE* sprite = beginReverseDrawPassIteration(drawState, pass, &cursor);
                while (sprite)
                {
                    if (sprite->Vid() == source)
                    {
                        Map->CreateSprite(
                            replacement, VECTOR(sprite->X(), sprite->Y(), sprite->Z()),
                            ANGLE(sprite->directionIndex()), nullptr, false);
                        DeleteSpriteThroughVirtualDeletingDestructor(sprite);
                    }
                    sprite = core::Application::previousSpriteInDrawPass(drawState, pass, &cursor);
                }
                return 0;
            }
        case script::NativeFunctionCode::Crc:
{
                const STRING text = *PopStr();
                const Crc32 crc(text.c_str(), static_cast<unsigned int>(text.Length()));
                PushInt(static_cast<int>(crc.Value()));
                return 0;
            }
        case script::NativeFunctionCode::Printf:
{
                if (script->IsLastStackString())
                {
                    const STRING value = *PopStr();
                    STRING localValue;
                    copyConstructString(localValue, value);

                    const char* valueText = script->stringTextPointer(localValue);
                    const STRING format = *PopStr();
                    const char* formatText = script->stringTextPointer(format);

                    const STRING formatted = STRING::Format(formatText, valueText);
                    PushStr(formatted);
                    return 0;
                }

                const int value = PopInt();
                const STRING format = *PopStr();
                const char* formatText = script->stringTextPointer(format);
                const STRING formatted = STRING::Format(formatText, value);
                PushStr(formatted);
                return 0;
            }
        case script::NativeFunctionCode::ReloadVid:
{

                (void)Map->ReloadVid();
                return 0;
            }
        case script::NativeFunctionCode::FileWrite:
{
                const STRING value = *PopStr();
                const int fileValue = PopInt();
                if (fileValue == 0)
                    return 0;

                std::FILE* file = scriptNativeFileFromInt(fileValue);
                if (!file)
                    return 0;

                script->writeCStringRecord(value, file);
                std::fseek(file, -1, SEEK_CUR);
                std::fputs("\n", file);
                return 0;
            }
        case script::NativeFunctionCode::FileRead:
{
                const int fileValue = PopInt();
                STRING lpFile;
                RESOURCE& demoResource = Map->demoResource();

                if ((core::ApplicationFlags() & application_flags::DemoUseResource) != 0)
                {
                    readStringLineFromStream(lpFile, &demoResource);
                }
                else if (fileValue != 0)
                {
                    readStringLineFromFile(lpFile, scriptNativeFileFromInt(fileValue));
                }

                if ((core::ApplicationFlags() & application_flags::DemoWriteToResource) != 0)
                    script->writeCStringToStream(lpFile, &demoResource);

                PushStr(lpFile);
                return 0;
            }
        case script::NativeFunctionCode::FileOpen:
{
                const STRING filename = *PopStr();
                if ((core::ApplicationFlags() & application_flags::DemoUseResource) != 0)
                {
                    PushInt(0);
                    return 0;
                }

                std::FILE* file = script->openScriptFile(filename, "r+t");
                if (!file)
                    script->RunTimeError(7, script->stringTextPointer(filename), 0);
                PushInt(scriptNativeIntFromFile(file));
                return 0;
            }
        case script::NativeFunctionCode::FileClose:
{
                const int fileValue = PopInt();
                if (fileValue == 0)
                    return 0;
                if (std::FILE* file = scriptNativeFileFromInt(fileValue))
                    std::fclose(file);
                return 0;
            }
        case script::NativeFunctionCode::FileCreate:
{
                const STRING filename = *PopStr();
                if ((core::ApplicationFlags() & application_flags::DemoUseResource) != 0)
                    return (PushInt(0), 0);

                std::FILE* file = script->openScriptFile(filename, "w+t");
                PushInt(scriptNativeIntFromFile(file));
                return 0;
            }
        case script::NativeFunctionCode::FileEof:
{
                const int fileValue = PopInt();
                if (fileValue == 0)
                    return (PushInt(1), 0);

                std::FILE* file = scriptNativeFileFromInt(fileValue);
                PushInt(file && std::feof(file) ? 0x10 : 0);
                return 0;
            }
        case script::NativeFunctionCode::RegistryGetString:
{


                const STRING defaultValue = *PopStr();
                const STRING valueName = *PopStr();
                const STRING registryPath = *PopStr();
                PushStr(registryPath.ReadRegistryString(valueName, defaultValue));
                return 0;
            }
        case script::NativeFunctionCode::RegistrySetString:
{

                const STRING value = *PopStr();
                const STRING valueName = *PopStr();
                const STRING registryPath = *PopStr();
                registryPath.WriteRegistryString(valueName, value);
                return 0;
            }
        case script::NativeFunctionCode::RegistryDeleteValue:
{

                const STRING valueName = *PopStr();
                const STRING registryPath = *PopStr();
                registryPath.DeleteRegistryValue(valueName);
                return 0;
            }
        case script::NativeFunctionCode::RegistryPath:
{

                PushStr(*core::g_startupRegistryPathOwner->Path());
                return 0;
            }
        case script::NativeFunctionCode::StringLower:
{
                PushStr(PopStr()->ToLower());
                return 0;
            }
        case script::NativeFunctionCode::StringUpper:
{
                PushStr(PopStr()->ToUpper());
                return 0;
            }
        case script::NativeFunctionCode::ToBase64:
{
                const int key = PopInt();
                const STRING text = *PopStr();
                PushStr(text.ToBase64(key));
                return 0;
            }
        case script::NativeFunctionCode::TrainProperty:
{


                const int query = PopInt();
                SPRITE* const sprite = scriptResolveSpriteReference(PopInt());
                if (!sprite || !sprite->Vid() || sprite->Vid()->spriteClassId() != 21u)
                {
                    PushInt(0);
                    return 0;
                }

                TRAIN_INFO metrics(static_cast<ENGINE*>(sprite));

                switch (query)
                {
                case 1:
                    PushInt(metrics.speed);
                    break;
                case 2:
                    PushInt(metrics.weapon);
                    break;
                case 3:
                    PushInt((metrics.hp * 100) / metrics.max_hp);
                    break;
                case 4:
                    PushInt(metrics.hp);
                    break;
                case 5:
                    PushInt(metrics.percentAmmo);
                    break;
                case 6:
                    PushInt(metrics.Acceleration());
                    break;
                case 7:
                    PushInt(metrics.build_time);
                    break;
                case 8:
                    PushInt(0);
                    break;
                case 9:
                {


                    for (SPRITE* node = sprite->engineChainHead(); node; node = node->engineChainNext())
                    {
                        if (node->commandIndex() != 0)
                            PushInt(0);
                    }
                    PushInt(1);
                    break;
                }
                case 10:
                    PushInt(metrics.ammo);
                    break;
                case 11:
                    PushInt(metrics.maxAmmo);
                    break;
                default:
                    PushInt(0);
                    break;
                }
                return 0;
            }
        case script::NativeFunctionCode::GetUnitInMap:
{


                std::array<unsigned char, 0x1000> monsterMask{};
                for (int i = 0; i < 0x1000; ++i)
                {
                    STRING variable = STRING::Format("MonstersVid[%d]", i);
                    const STRING value = script->GetVariableStr(variable);
                    if (value.isEmpty())
                        break;
                    const int nvid = value.Int();
                    if (nvid >= 0 && nvid < 0x1000)
                        monsterMask[static_cast<std::size_t>(nvid)] = 1;
                }

                const auto masked = [&monsterMask](int nvid) noexcept -> bool
                {
                    return nvid >= 0 && nvid < 0x1000 &&
                           monsterMask[static_cast<std::size_t>(nvid)] != 0;
                };

                const auto addVidAndDeathChain = [&masked](VID* vid, int& count) noexcept
                {
                    if (!vid || !masked(vid->nvid()))
                        return;

                    ++count;
                    VID* const deathChild = vid->deathChildVid();
                    if (!deathChild)
                        return;

                    if (masked(vid->deathChildNvid()))
                        ++count;
                    if (masked(deathChild->deathChildNvid()))
                        ++count;
                };

                int count = 0;
                for (int pass = 0; pass < 20; ++pass)
                {
                    const core::ApplicationDrawPassBucket& bucket = drawState.drawPassBucket(pass);
                    for (int i = 0; i < bucket.count(); ++i)
                    {
                        SPRITE* const candidate = bucket.spriteAt(i);
                        if (!candidate)
                            continue;

                        addVidAndDeathChain(candidate->Vid(), count);


                        if (candidate->IsActionStackEmpty())
                            continue;

                        core::List<ACT>* const actionStack = candidate->ActionStack();
                        for (int commandIndex = actionStack->No() - 1;
                             commandIndex >= 0;
                             --commandIndex)
                        {
                            ACT queuedCommand{};
                            copyCommandRecord(&queuedCommand, (*actionStack)[commandIndex]);
                            if (queuedCommand.opcode == 0x49u)
                                break;
                            if (queuedCommand.opcode != 0x23u)
                                continue;

                            const int queuedNvid = static_cast<int>(queuedCommand.argument1);
                            if (queuedNvid <= 0 || !masked(queuedNvid))
                                continue;


                            ++count;
                            VID* const queuedVid = Vid(queuedNvid);
                            if (!queuedVid || !queuedVid->deathChildVid())
                                continue;
                            if (masked(queuedVid->deathChildNvid()))
                                ++count;
                            if (masked(queuedVid->deathChildVid()->deathChildNvid()))
                                ++count;
                        }
                    }
                }
                PushInt(count);
                return 0;
            }
        case script::NativeFunctionCode::BreakTrain:
{
                const int y = PopInt();
                const int x = PopInt();
                if (SPRITE* const sprite = script->popSpriteReference())
                    sprite->splitEngineChainAtPosition(static_cast<float>(x), static_cast<float>(y));
                return 0;
            }
        case script::NativeFunctionCode::FirstTrain:
{
                PushObject(scriptSpritePointerValue(FirstTrain(PopInt())), nullptr);
                return 0;
            }
        case script::NativeFunctionCode::NextTrain:
{
                PushObject(scriptSpritePointerValue(NextTrain()), nullptr);
                return 0;
            }
        case script::NativeFunctionCode::PatrolEngine:
{
                const int y = PopInt();
                const int x = PopInt();
                SPRITE* const sprite = script->popSpriteReference();
                if (sprite && sprite->Vid()->spriteClassId() == 21)
                {
                    static_cast<ENGINE*>(sprite)->SetCommandToTrain(25, x, y);
                    return 0;
                }
                LOG::Write(u8"Борис, у тебя в PatrolTrain - train неверный %X",
                           static_cast<unsigned int>(reinterpret_cast<std::uintptr_t>(sprite)));
                return 0;
            }
        case script::NativeFunctionCode::SetPushLine:
{
                const int value = PopInt();
                const int y2 = PopInt();
                const int x2 = PopInt();
                const int y1 = PopInt();
                const int x1 = PopInt();
                core::g_rMap.SetPushLine(x1, y1, x2, y2, value);
                return 0;
            }
        case script::NativeFunctionCode::GetScreenInputX:
{
                PushInt(static_cast<int>(scriptApplicationInputState().clientX));
                return 0;
            }
        case script::NativeFunctionCode::GetScreenInputY:
{
                PushInt(static_cast<int>(scriptApplicationInputState().clientY));
                return 0;
            }
        case script::NativeFunctionCode::SetCleverEnemyAttack:
{


                const int value = PopInt();
                static_cast<PLAYER_ARCADE*>(scriptPlayerSlot(1))->SetCleverEnemyAttack(value);
                return 0;
            }
        case script::NativeFunctionCode::AddUnitLimit:
{
                const int index = PopInt();
                VID* const vid = PopVid("for AddUnitLimit");
                const int value = PopInt();
                if (vid != EmptyVid)
                    vid->setUnitLimit(index, value);
                return 0;
            }
        case script::NativeFunctionCode::SetEnemyCanAttackNeutralTrains:
{
                const int value = PopInt();
                const std::uint32_t bit = (value != 0) ? application_flags::EnemyCanAttackNeutralTrains : 0u;
                const std::uint32_t flags = (core::ApplicationFlags() & ~application_flags::EnemyCanAttackNeutralTrains) | bit;
                core::SetApplicationFlags(flags);
                return 0;
            }
        case script::NativeFunctionCode::SetMoney:
{
                const int value = PopInt();
                const int playerIndex = PopInt();
                scriptPlayerSlot(playerIndex)->SetMoney(value);
                return 0;
            }
        case script::NativeFunctionCode::GetMoney:
{
                const int playerIndex = PopInt();
                PushInt(static_cast<int>(scriptPlayerSlot(playerIndex)->getMoney()));
                return 0;
            }
        case script::NativeFunctionCode::CanMoveEngineTo:
{

                (void)PopInt();
                (void)PopInt();
                (void)PopInt();
                return 0;
            }
        case script::NativeFunctionCode::CanAttackEngine:
{

                (void)PopInt();
                (void)PopInt();
                return 0;
            }
        case script::NativeFunctionCode::ZS1Bridge:
{
                const STRING str2 = *PopStr();
                const STRING str1 = *PopStr();
                const int arg2 = PopInt();
                const int arg1 = PopInt();
                const int subcommand = PopInt();
                int intResult = 0;
                void* objectResult = nullptr;
                zs1::script_engine::g_mainObject = this;
                const char* stringResult = zs1::ScriptExecDispatch(subcommand, arg1, arg2,
                    str1.c_str(), str2.c_str(), &intResult, &objectResult);
                if (stringResult)
                    PushStr(STRING(stringResult));
                else if (objectResult)
                    PushObject(static_cast<int>(reinterpret_cast<std::uintptr_t>(objectResult)), nullptr);
                else
                    PushInt(intResult);
                return 0;
            }
        default:
            writeLogLine(g_fileLogger, "!!!ERROR!!!LOGIC: Unknown extern Function %i", opcode);
            return 0;
        }
    }


    SPRITE* SCRIPT::popSpriteReference()
    {
        return scriptResolveSpriteReference(popSpriteReferenceValue());
    }



}
