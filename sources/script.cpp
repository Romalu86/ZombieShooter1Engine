#include "script.h"
#include "core/file_stream.h"
#include "core/file_logger.h"
#include "core/log.h"
#include "map.h"
#include "vid/vid.h"
#include <cstdint>
#include <cstring>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <io.h>
#include <new>

namespace as1
{
    int ScriptExecFunc(int opcode);

    namespace script
    {
        LogicFunctionRecord::LogicFunctionRecord()
        {
            resetDefaults();
        }

        void LogicFunctionRecord::resetDefaults()
        {
            name = STRING();
            flags = 0;
            statusFlags = 0;
            reserved06 = 0;
            reserved07 = 0;
            text = STRING();
        }

        void LogicFunctionRecord::copyFrom(const LogicFunctionRecord& other)
        {
            name = other.name;
            flags = other.flags;
            statusFlags = other.statusFlags;
            text = other.text;
            value0 = other.value0;
            value1 = other.value1;
            value2 = other.value2;
        }

        void* destroyLogicFunctionRecordStorage(LogicFunctionRecord* self, unsigned char flags) noexcept
        {
            if ((flags & 0x02u) != 0)
            {
                std::uint32_t* const header = reinterpret_cast<std::uint32_t*>(self) - 1;
                const std::uint32_t count = *header;
                for (std::uint32_t i = count; i != 0; --i)
                {
                    LogicFunctionRecord& record = self[i - 1u];
                    destroyStringStorage(record.text);
                    destroyStringStorage(record.name);
                }
                if ((flags & 0x01u) != 0)
                    ::operator delete(static_cast<void*>(header));
                return header;
            }

            destroyStringStorage(self->text);
            destroyStringStorage(self->name);
            if ((flags & 0x01u) != 0)
                ::operator delete(static_cast<void*>(self));
            return self;
        }
    }

    namespace
    {
        std::uint32_t scriptFileLength32(const FSTREAM& stream) noexcept
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
            const std::size_t bytes = sizeof(std::uint32_t) + static_cast<std::size_t>(capacity) * sizeof(T);
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
                    records[--constructed].~T();
                ::operator delete(static_cast<void*>(raw));
                throw;
            }
            return records;
        }

        bool readScriptDword(const std::uint8_t* bytecode, int offset, int& value)
        {
            std::uint32_t raw = 0;
            std::memcpy(&raw, bytecode + static_cast<std::size_t>(offset), sizeof(raw));
            value = static_cast<int>(raw);
            return true;
        }

        int scriptStackValueToInteger(const script::StackObject& value)
        {
            return (value.flags & script::STACK_OBJECT_STRING)
                ? script::ParseStackIntegerText(value.text.c_str())
                : value.intValue;
        }


        using ScriptListDeletingDestructor = void* (__fastcall*)(void*, void*, unsigned char);
        void* __fastcall scriptStackListDeletingDestructor(void* rawThis, void*, unsigned char deleteFlags);
        void* __fastcall scriptFunctionListDeletingDestructor(void* rawThis, void*, unsigned char deleteFlags);
        void* __fastcall scriptDefineListDeletingDestructor(void* rawThis, void*, unsigned char deleteFlags);

        ScriptListDeletingDestructor scriptStackListVtable[] = { &scriptStackListDeletingDestructor };
        ScriptListDeletingDestructor scriptFunctionListInitialVtable[] = { &scriptFunctionListDeletingDestructor };
        ScriptListDeletingDestructor scriptDefineListInitialVtable[] = { &scriptDefineListDeletingDestructor };
        ScriptListDeletingDestructor scriptFunctionListFinalVtable[] = { &scriptFunctionListDeletingDestructor };
        ScriptListDeletingDestructor scriptDefineListFinalVtable[] = { &scriptDefineListDeletingDestructor };

        std::uint32_t currentPointerToken(const void* pointer) noexcept
        {
            return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(pointer));
        }


        void destroyScriptFunctionListSubobject(void* rawThis) noexcept
        {
            auto* owner = reinterpret_cast<SCRIPT*>(static_cast<unsigned char*>(rawThis) - 0x10u);
            auto* physical = reinterpret_cast<ScriptData*>(owner);
            physical->functionListVtable = currentPointerToken(scriptFunctionListFinalVtable);
            auto* records = reinterpret_cast<script::LogicFunctionRecord*>(
                static_cast<std::uintptr_t>(physical->functionTableToken));
            if (records)
                script::destroyLogicFunctionRecordStorage(records, 3);
            physical->functionTableToken = 0u;
            physical->functionCount = 0;
        }


        void destroyScriptDefineListSubobject(void* rawThis) noexcept
        {
            auto* owner = reinterpret_cast<SCRIPT*>(static_cast<unsigned char*>(rawThis) - 0x20u);
            auto* physical = reinterpret_cast<ScriptData*>(owner);
            physical->defineListVtable = currentPointerToken(scriptDefineListFinalVtable);
            auto* records = reinterpret_cast<ScriptDefinePairRecord*>(
                static_cast<std::uintptr_t>(physical->defineTableToken));
            if (records)
                scriptDefinePairDeletingDestructor(records, 3);
            physical->defineTableToken = 0u;
            physical->defineCount = 0;
        }


        void destroyScriptStackListSubobject(void* rawThis) noexcept
        {
            auto* physical = reinterpret_cast<ScriptData*>(rawThis);
            physical->stackListVtable = currentPointerToken(scriptStackListVtable);
            auto* records = reinterpret_cast<script::StackObject*>(
                static_cast<std::uintptr_t>(physical->stackTableToken));
            if (records)
            {
                std::uint32_t* const header = reinterpret_cast<std::uint32_t*>(records) - 1;
                const std::uint32_t count = *header;
                for (std::uint32_t i = count; i != 0; --i)
                    records[i - 1u].~StackObject();
                ::operator delete(static_cast<void*>(header));
            }
            physical->stackTableToken = 0u;
            physical->stackCount = 0;
        }

        void* __fastcall scriptStackListDeletingDestructor(void* rawThis, void*, unsigned char deleteFlags)
        {
            destroyScriptStackListSubobject(rawThis);
            if ((deleteFlags & 1u) != 0u)
                ::operator delete(rawThis);
            return rawThis;
        }

        void* __fastcall scriptFunctionListDeletingDestructor(void* rawThis, void*, unsigned char deleteFlags)
        {


            destroyScriptFunctionListSubobject(rawThis);
            if ((deleteFlags & 1u) != 0u)
                ::operator delete(rawThis);
            return rawThis;
        }

        void* __fastcall scriptDefineListDeletingDestructor(void* rawThis, void*, unsigned char deleteFlags)
        {

            destroyScriptDefineListSubobject(rawThis);
            if ((deleteFlags & 1u) != 0u)
                ::operator delete(rawThis);
            return rawThis;
        }


        void* initializeScriptFunctionListSubobject(void* rawThis) noexcept
        {
            auto* slot = static_cast<std::uint32_t*>(rawThis);
            slot[3] = 0u;
            slot[1] = 0u;
            slot[2] = 0u;
            slot[0] = currentPointerToken(scriptFunctionListInitialVtable);
            return rawThis;
        }


        void* initializeScriptDefineListSubobject(void* rawThis) noexcept
        {
            auto* slot = static_cast<std::uint32_t*>(rawThis);
            slot[3] = 0u;
            slot[1] = 0u;
            slot[2] = 0u;
            slot[0] = currentPointerToken(scriptDefineListInitialVtable);
            return rawThis;
        }


        void* initializeScriptStackListSubobject(void* rawThis) noexcept
        {
            auto* slot = static_cast<std::uint32_t*>(rawThis);
            slot[3] = 0u;
            slot[0] = currentPointerToken(scriptStackListVtable);
            slot[1] = 0u;
            slot[2] = 0u;
            return rawThis;
        }
    }

    SCRIPT::SCRIPT()
    {

        std::memset(&m_data, 0, sizeof(m_data));
        initializeScriptStackListSubobject(&m_data.stackListVtable);
        initializeScriptFunctionListSubobject(&m_data.functionListVtable);
        initializeScriptDefineListSubobject(&m_data.defineListVtable);
        m_data.scriptFileToken = pointerToken(as1::STRING::SharedEmptyText());
        m_data.sourceLine = -1;
    }


    std::uint32_t SCRIPT::pointerToken(const void* pointer) noexcept
    {
        return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(pointer) & 0xFFFFFFFFu);
    }

    script::StackObject* SCRIPT::executionStackStorage() noexcept
    {
        return reinterpret_cast<script::StackObject*>(static_cast<std::uintptr_t>(m_data.stackTableToken));
    }

    const script::StackObject* SCRIPT::executionStackStorage() const noexcept
    {
        return reinterpret_cast<const script::StackObject*>(static_cast<std::uintptr_t>(m_data.stackTableToken));
    }

    script::LogicFunctionRecord* SCRIPT::functionRecordStorage() noexcept
    {
        return reinterpret_cast<script::LogicFunctionRecord*>(static_cast<std::uintptr_t>(m_data.functionTableToken));
    }

    const script::LogicFunctionRecord* SCRIPT::functionRecordStorage() const noexcept
    {
        return reinterpret_cast<const script::LogicFunctionRecord*>(static_cast<std::uintptr_t>(m_data.functionTableToken));
    }

    ScriptDefinePairRecord* SCRIPT::defineRecordStorage() noexcept
    {
        return reinterpret_cast<ScriptDefinePairRecord*>(static_cast<std::uintptr_t>(m_data.defineTableToken));
    }

    const ScriptDefinePairRecord* SCRIPT::defineRecordStorage() const noexcept
    {
        return reinterpret_cast<const ScriptDefinePairRecord*>(static_cast<std::uintptr_t>(m_data.defineTableToken));
    }

    STRING& SCRIPT::scriptFileStorage() noexcept
    {
        return *reinterpret_cast<STRING*>(&m_data.scriptFileToken);
    }

    const STRING& SCRIPT::scriptFileStorage() const noexcept
    {
        return *reinterpret_cast<const STRING*>(&m_data.scriptFileToken);
    }

    std::uint8_t* SCRIPT::bytecodeStorage() noexcept
    {
        return reinterpret_cast<std::uint8_t*>(static_cast<std::uintptr_t>(m_data.bytecodeBufferToken));
    }

    const std::uint8_t* SCRIPT::bytecodeStorage() const noexcept
    {
        return reinterpret_cast<const std::uint8_t*>(static_cast<std::uintptr_t>(m_data.bytecodeBufferToken));
    }

    std::uint8_t* SCRIPT::sourceStorage() noexcept
    {
        return reinterpret_cast<std::uint8_t*>(static_cast<std::uintptr_t>(m_data.sourceBufferToken));
    }

    const std::uint8_t* SCRIPT::sourceStorage() const noexcept
    {
        return reinterpret_cast<const std::uint8_t*>(static_cast<std::uintptr_t>(m_data.sourceBufferToken));
    }

    void SCRIPT::syncFunctionList() noexcept
    {
        return;
    }

    void SCRIPT::syncDefineList() noexcept
    {
        return;
    }

    void SCRIPT::setSourceEndOffset(int offset) noexcept
    {
        m_data.sourceEnd = m_data.sourceBufferToken == 0u
            ? 0u
            : m_data.sourceBufferToken + static_cast<std::uint32_t>(offset < 0 ? 0 : offset);
    }

    void SCRIPT::syncSourcePointers() noexcept
    {
        return;
    }

    void SCRIPT::syncBackingPointers() noexcept
    {
        return;
    }

    SCRIPT::~SCRIPT()
    {
        resetScriptVmState();
        destroyScriptDefineListSubobject(&m_data.defineListVtable);
        destroyScriptFunctionListSubobject(&m_data.functionListVtable);
        destroyScriptStackListSubobject(&m_data.stackListVtable);
        destroyStringStorage(scriptFileStorage());
    }

    void SCRIPT::SetNativeContext(const ScriptNativeContext& context)
    {
        (void)context;
    }

    int SCRIPT::Load(const STRING& scriptFile)
    {
        std::uint32_t firstDword = static_cast<std::uint32_t>(m_data.stackCount);
        FSTREAM stream(scriptFile.str(), "rb");
        resetScriptVmState();
        assignStringFromString(scriptFileStorage(), scriptFile);
        if (!stream.isOpen())
        {
            reportCompileError(7, "", 0);
            return 1;
        }

        stream.read_new(&firstDword, sizeof(firstDword));

        if ((firstDword & 0xFF000000u) != 0)
        {
            const int result = compileScriptSourceFile(scriptFileStorage(), STRING());
            stream.close();
            return result;
        }

        const int stackCount = static_cast<int>(firstDword);

        if (m_data.stackCapacity < static_cast<int>(InitialListCapacity))
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
        m_data.stackCount = stackCount;
        if (stackCount > m_data.stackCapacity)
        {
            try
            {
                const int oldCapacity = m_data.stackCapacity;
                script::StackObject* const oldRecords = executionStackStorage();
                script::StackObject* const records = allocateScriptRecords<script::StackObject>(stackCount);
                for (int i = 0; i < oldCapacity; ++i)
                    records[i].copyStorageFrom(oldRecords[i]);
                if (oldRecords)
                    destroyStringRecordArray(static_cast<void*>(oldRecords), 3);
                m_data.stackTableToken = pointerToken(records);
            }
            catch (...)
            {
                fatalLogError(g_fileLogger, "!!!ERROR!!!::LIST: Not enough memory %i", stackCount);
            }
            m_data.stackCapacity = stackCount;
        }
        readExecutionStackFromStream(&stream);

        std::uint32_t bytecodeSize = 0;
        stream.read_new(&bytecodeSize, sizeof(bytecodeSize));

        try
        {
            void* const bytecodeOwner = ::operator new(static_cast<std::size_t>(bytecodeSize));
            m_data.bytecodeBufferToken = pointerToken(bytecodeOwner);
        }
        catch (...)
        {
            reportCompileError(2, "data2", 0);
            std::exit(1);
        }
        m_data.bytecodeEnd = static_cast<int>(bytecodeSize);
        syncBackingPointers();
        if (bytecodeSize != 0)
            stream.read(bytecodeStorage(), bytecodeSize);

        for (int i = 0; i < m_data.functionCount; ++i)
        {
            const script::LogicFunctionRecord* const rec = functionRecordAt(i);
            if (rec && std::strcmp(rec->name.c_str(), "main") == 0)
                m_data.fallbackFunction = i;
        }

        stream.close();
        return 0;
    }


    int SCRIPT::getFunctionIndex(const STRING& name) const noexcept
    {
        const script::LogicFunctionRecord* const records = functionRecordStorage();
        if (!records)
            return -1;
        for (int i = m_data.functionCount - 1; i >= 0; --i)
        {
            if (std::strcmp(records[static_cast<std::size_t>(i)].name.c_str(), name.c_str()) == 0)
                return i;
        }
        return -1;
    }


    STRING SCRIPT::GetVariableStr(const STRING& expression)
    {
        STRING variableName;
        constructLeftOfFirstMarker(expression, variableName, "[");
        const int functionIndex = getFunctionIndex(variableName);
        if (functionIndex < 0)
        {
            LOG::Write("!!!ERROR!!! SCRIPT Can't find variable '%s' in GetVariableString", expression.c_str());
            return STRING();
        }

        STRING indexText;
        constructRightOfFirstMarker(expression, indexText, "[");
        const int elementIndex = script::ParseStackIntegerText(indexText.c_str());
        const script::LogicFunctionRecord* const records = functionRecordStorage();
        const script::LogicFunctionRecord& record = records[static_cast<std::size_t>(functionIndex)];


        if (record.flags != 1u)
        {
            LOG::Write("!!!ERROR!!! SCRIPT Can't find variable '%s' in GetVariableString", expression.c_str());
            return STRING();
        }


        if (elementIndex >= record.value2)
            return STRING();

        const int stackIndex = record.value0 + elementIndex;
        script::StackObject* value = mutableExecutionStackStorageAt(stackIndex);

        if ((value->flags & script::STACK_OBJECT_INT) != 0)
        {
            value->text = script::IntToStackString(value->intValue);
        }
        return STRING(value->text.c_str());
    }


    void SCRIPT::SetVariableActionInt(int actionIndex, int value)
    {
        if (m_data.compileError != 0)
            return;

        const std::uint32_t rawAction = static_cast<std::uint32_t>(actionIndex);
        if (rawAction < 256u)
        {
            const int stackIndex = m_data.actionN[rawAction];
            if (static_cast<std::uint32_t>(stackIndex) <
                static_cast<std::uint32_t>(m_data.stackCount))
            {
                script::StackObject* const valueObject = mutableExecutionStackStorageAt(stackIndex);


                valueObject->intValue = value;
                valueObject->flags = script::STACK_OBJECT_INT;
                return;
            }
        }

        char variableName[64]{};
        std::snprintf(variableName, sizeof(variableName), "variable for Set Action%i", actionIndex);

        reportCompileError(13, variableName, 0);
    }


    int SCRIPT::callFunction(int functionIndex, const char* argumentTypes, int arg1, int arg2, int arg3)
    {


        if (m_data.compileError != 0 || m_data.bytecodeBufferToken == 0u)
            return 0;

        int resolvedFunction = functionIndex;
        if (resolvedFunction < 0)
            resolvedFunction = m_data.fallbackFunction;

        if (resolvedFunction < 0 || resolvedFunction >= m_data.functionCount)
        {
            LOG::Write("!!!ERROR!!! SCRIPT Call unexisted function %i", resolvedFunction);
            return 0;
        }

        const script::LogicFunctionRecord& fn = functionRecordStorage()[static_cast<std::size_t>(resolvedFunction)];
        if (fn.flags != 3)
        {
            LOG::Write("!!!ERROR!!!LOGIC: Call unexisted function %s()", fn.name.c_str());
            return 0;
        }

        const int savedStackCount = m_data.stackCount;
        const int bytecodeLimit = m_data.bytecodeEnd;
        int cursor = fn.value0;
        int controlAnchor = cursor;
        int result = 0;
        int pointerSentinel = 0x7FFFFFFF;

        PushInt(savedStackCount);
        PushInt(bytecodeLimit);
        int frameBase = static_cast<int>(
            static_cast<std::uint32_t>(savedStackCount) + 2u);


        const char* const types = argumentTypes;
        const int argumentValues[3] = { arg1, arg2, arg3 };
        const int marshaledCount = fn.value2 < 3 ? fn.value2 : 3;
        for (int i = 0; i < marshaledCount; ++i)
        {
            script::StackObject* const arg = mutableExecutionStackStorageAt(fn.value1 + i);
            const char type = types[i];
            const int value = argumentValues[i];
            if (type == 's' || type == 'p')
            {
                arg->assignFields(
                    static_cast<std::uint8_t>(
                        value != 0
                            ? (script::STACK_OBJECT_INT | script::STACK_OBJECT_REF)
                            : script::STACK_OBJECT_INT),
                    value,
                    STRING());
            }
            else
            {
                arg->assignInt(value);
            }
        }


        if (types[0] == 'p')
            pointerSentinel = arg1;
        else if (types[1] == 'p')
            pointerSentinel = arg2;
        else if (types[2] == 'p')
            pointerSentinel = arg3;

        int arrayVmActive = 0;
        int arrayVmOffset = 0;

        m_data.runtimeScratch860 = m_data.ownedBufferToken;

        while (cursor < bytecodeLimit)
        {
            m_data.runtimeScratch864 = m_data.runtimeScratch860;
            m_data.runtimeScratch860 = m_data.ownedBufferToken +
                static_cast<std::uint32_t>(cursor * 4);
            if (m_data.stackCount < frameBase)
                LOG::Write("!!!ERROR!!!SCRIPT: '%s' stack error %i", "pop, but not push", cursor);

            const std::uint8_t opcode = bytecodeStorage()[static_cast<std::size_t>(cursor++)];
            const bool isBinaryCommand =
                opcode >= script::opcodeValue(script::BinaryCommand::Divide) &&
                opcode <= script::opcodeValue(script::BinaryCommand::ShiftLeft);
            const bool isVariableCommand =
                opcode >= script::opcodeValue(script::VmOpcode::PostIncrement) &&
                opcode <= script::opcodeValue(script::VmOpcode::CompoundAssign);
            if (isBinaryCommand || isVariableCommand)
            {
                if (isBinaryCommand)
                {
                    script::StackObject* rhs = mutableExecutionStackStorageAt(m_data.stackCount - 1);
                    script::StackObject* lhs = mutableExecutionStackStorageAt(m_data.stackCount - 2);
                    lhs->BinarOperator(opcode, *rhs);
                    --m_data.stackCount;
                    continue;
                }

                int operandIndex = 0;
                readScriptDword(bytecodeStorage(), cursor, operandIndex);

                script::StackObject* operandRecord = mutableExecutionStackStorageAt(operandIndex);
                int targetIndex = operandIndex;
                if ((operandRecord->flags & script::STACK_OBJECT_DYNAMIC) != 0 && arrayVmActive != 0)
                    targetIndex = operandRecord->intValue;
                targetIndex += arrayVmOffset;

                script::StackObject* target = mutableExecutionStackStorageAt(targetIndex);

                const script::VmOpcode variableCommand = static_cast<script::VmOpcode>(opcode);

                switch (variableCommand)
                {
                case script::VmOpcode::PostIncrement:
                    reinterpret_cast<script::StackObjectList*>(&m_data.stackListVtable)->appendFields(
                        target->flags, target->intValue, target->text);
                    target->flags = static_cast<std::uint8_t>(target->flags & ~script::STACK_OBJECT_REF);
                    if ((target->flags & (script::STACK_OBJECT_INT | script::STACK_OBJECT_DYNAMIC)) != 0)
                    {
                        target->intValue += 1;
                    }
                    else if ((target->flags & script::STACK_OBJECT_STRING) != 0)
                    {
                        target->text = script::IntToStackString(scriptStackValueToInteger(*target) + 1);
                        target->flags = script::STACK_OBJECT_STRING;
                    }
                    cursor += 4;
                    arrayVmActive = 0;
                    arrayVmOffset = 0;
                    continue;
                case script::VmOpcode::PostDecrement:
                    reinterpret_cast<script::StackObjectList*>(&m_data.stackListVtable)->appendFields(
                        target->flags, target->intValue, target->text);
                    target->flags = static_cast<std::uint8_t>(target->flags & ~script::STACK_OBJECT_REF);
                    if ((target->flags & (script::STACK_OBJECT_INT | script::STACK_OBJECT_DYNAMIC)) != 0)
                    {
                        target->intValue -= 1;
                    }
                    else if ((target->flags & script::STACK_OBJECT_STRING) != 0)
                    {
                        target->text = script::IntToStackString(scriptStackValueToInteger(*target) - 1);
                        target->flags = script::STACK_OBJECT_STRING;
                    }
                    cursor += 4;
                    arrayVmActive = 0;
                    arrayVmOffset = 0;
                    continue;
                case script::VmOpcode::PreIncrement:
                    target->flags = static_cast<std::uint8_t>(target->flags & ~script::STACK_OBJECT_REF);
                    if ((target->flags & (script::STACK_OBJECT_INT | script::STACK_OBJECT_DYNAMIC)) != 0)
                    {
                        target->intValue += 1;
                    }
                    else if ((target->flags & script::STACK_OBJECT_STRING) != 0)
                    {
                        target->text = script::IntToStackString(scriptStackValueToInteger(*target) + 1);
                        target->flags = script::STACK_OBJECT_STRING;
                    }
                    reinterpret_cast<script::StackObjectList*>(&m_data.stackListVtable)->appendFields(
                        target->flags, target->intValue, target->text);
                    cursor += 4;
                    arrayVmActive = 0;
                    arrayVmOffset = 0;
                    continue;
                case script::VmOpcode::PreDecrement:
                    target->flags = static_cast<std::uint8_t>(target->flags & ~script::STACK_OBJECT_REF);
                    if ((target->flags & (script::STACK_OBJECT_INT | script::STACK_OBJECT_DYNAMIC)) != 0)
                    {
                        target->intValue -= 1;
                    }
                    else if ((target->flags & script::STACK_OBJECT_STRING) != 0)
                    {
                        target->text = script::IntToStackString(scriptStackValueToInteger(*target) - 1);
                        target->flags = script::STACK_OBJECT_STRING;
                    }
                    reinterpret_cast<script::StackObjectList*>(&m_data.stackListVtable)->appendFields(
                        target->flags, target->intValue, target->text);
                    cursor += 4;
                    arrayVmActive = 0;
                    arrayVmOffset = 0;
                    continue;
                case script::VmOpcode::ReadVariable:
                    if (arrayVmActive == 0 && (target->flags & script::STACK_OBJECT_ARRAY) != 0)
                    {
                        PushInt(operandIndex);
                    }
                    else
                    {
                        reinterpret_cast<script::StackObjectList*>(&m_data.stackListVtable)->appendFields(
                            target->flags, target->intValue, target->text);
                    }
                    cursor += 4;
                    arrayVmActive = 0;
                    arrayVmOffset = 0;
                    continue;
                case script::VmOpcode::AddressOf:
                {
                    const std::uintptr_t rawAddress =
                        (target->flags & script::STACK_OBJECT_STRING) != 0
                            ? reinterpret_cast<std::uintptr_t>(&target->text)
                            : reinterpret_cast<std::uintptr_t>(&target->intValue);
                    PushInt(static_cast<int>(rawAddress & 0xFFFFFFFFu));
                    cursor += 4;
                    arrayVmActive = 0;
                    arrayVmOffset = 0;
                    continue;
                }
                case script::VmOpcode::Assign:
                {
                    script::StackObject* source = mutableExecutionStackStorageAt(m_data.stackCount - 1);

                    script::StackObject* baseRecord = mutableExecutionStackStorageAt(operandIndex);
                    if ((baseRecord->flags & script::STACK_OBJECT_STRING) != 0)
                    {
                        if (arrayVmActive != 0 && (baseRecord->flags & script::STACK_OBJECT_ARRAY) == 0)
                        {
                            std::string text = baseRecord->text.str();
                            const int numeric = scriptStackValueToInteger(*source);
                            text[static_cast<std::size_t>(arrayVmOffset)] = static_cast<char>(numeric & 0xFF);
                            baseRecord->text.AssignBytes(text.data(), text.size());
                        }
                        else
                        {
                            target->flags = static_cast<std::uint8_t>(
                                target->flags & ~script::STACK_OBJECT_CHAR_WRITE);
                            if ((source->flags & script::STACK_OBJECT_INT) != 0)
                                target->text = script::IntToStackString(source->intValue);
                            else
                                target->text = source->text;
                        }
                    }
                    else
                    {
                        const int numeric = scriptStackValueToInteger(*source);
                        target->flags = static_cast<std::uint8_t>(target->flags & ~static_cast<std::uint8_t>(script::STACK_OBJECT_REF | script::STACK_OBJECT_CHAR_WRITE));
                        target->intValue = numeric;
                        if ((source->flags & script::STACK_OBJECT_REF) != 0)
                            target->flags = static_cast<std::uint8_t>(target->flags | script::STACK_OBJECT_REF);
                    }
                    cursor += 4;
                    arrayVmActive = 0;
                    arrayVmOffset = 0;
                    continue;
                }
                case script::VmOpcode::CompoundAssign:
                {
                    --m_data.stackCount;
                    script::StackObject* rhs = mutableExecutionStackStorageAt(m_data.stackCount);
                    const std::uint8_t compoundOpcode = bytecodeStorage()[static_cast<std::size_t>(cursor + 4)];
                    target->BinarOperator(compoundOpcode, *rhs);
                    reinterpret_cast<script::StackObjectList*>(&m_data.stackListVtable)->appendFields(
                        target->flags, target->intValue, target->text);
                    cursor += 5;
                    arrayVmActive = 0;
                    arrayVmOffset = 0;
                    continue;
                }
                default:
                    cursor += 4;
                    arrayVmActive = 0;
                    arrayVmOffset = 0;
                    continue;
                }
            }

            switch (static_cast<script::VmOpcode>(opcode))
            {
            case script::VmOpcode::PushInteger:
            {
                int value = 0;
                readScriptDword(bytecodeStorage(), cursor, value);
                PushInt(value);
                cursor += 4;
                break;
            }
            case script::VmOpcode::PushString:
            {
                const char* text = reinterpret_cast<const char*>(bytecodeStorage() + static_cast<std::size_t>(cursor));
                PushStr(STRING(text));
                cursor += static_cast<int>(std::strlen(text)) + 1;
                break;
            }
            case script::VmOpcode::Negate:
            case script::VmOpcode::BitwiseNot:
            case script::VmOpcode::LogicalNot:
            {
                script::StackObject* top = mutableExecutionStackStorageAt(m_data.stackCount - 1);
                const int value = scriptStackValueToInteger(*top);
                top->flags = script::STACK_OBJECT_INT;
                if (opcode == script::opcodeValue(script::VmOpcode::Negate))
                    top->intValue = -value;
                else if (opcode == script::opcodeValue(script::VmOpcode::BitwiseNot))
                    top->intValue = ~value;
                else
                    top->intValue = value == 0 ? 1 : 0;
                break;
            }
            case script::VmOpcode::If:
            {
                --m_data.stackCount;
                script::StackObject* cond = mutableExecutionStackStorageAt(m_data.stackCount);
                const int condValue = scriptStackValueToInteger(*cond);
                int payload = 0;
                readScriptDword(bytecodeStorage(), cursor, payload);
                cursor += condValue ? 4 : payload;
                if (m_data.stackCount - frameBase > 1)
                    LOG::Write("!!!ERROR!!!SCRIPT: '%s' stack error %i", "if", cursor);
                controlAnchor = cursor;
                m_data.stackCount = frameBase;
                break;
            }
            case script::VmOpcode::StatementEnd:
            {
                int sourceLine = 0;
                readScriptDword(bytecodeStorage(), cursor, sourceLine);
                cursor += 4;
                controlAnchor = cursor;
                if (m_data.stackCount - frameBase > 1)
                    LOG::Write("!!!ERROR!!!SCRIPT: '%s' stack error %i", "; in line ", sourceLine);
                m_data.stackCount = frameBase;
                break;
            }
            case script::VmOpcode::Pop:

                --m_data.stackCount;
                break;
            case script::VmOpcode::Jump:
            {
                int payload = 0;
                readScriptDword(bytecodeStorage(), cursor, payload);
                cursor += payload;
                break;
            }
            case script::VmOpcode::IfFalseChain:
            {
                --m_data.stackCount;
                script::StackObject* cond = mutableExecutionStackStorageAt(m_data.stackCount);
                const int condValue = scriptStackValueToInteger(*cond);
                int payload = 0;
                readScriptDword(bytecodeStorage(), cursor, payload);

                if (condValue != 0)
                {
                    const int previousAnchor = controlAnchor;
                    bytecodeStorage()[static_cast<std::size_t>(previousAnchor)] = script::opcodeValue(script::VmOpcode::Jump);
                    const int patchedRelative = payload - previousAnchor + cursor - 1;
                    std::memcpy(bytecodeStorage() + static_cast<std::size_t>(previousAnchor + 1),
                                &patchedRelative, sizeof(patchedRelative));
                    cursor += 4;
                }
                else
                {
                    cursor += payload;
                }

                controlAnchor = cursor;
                if (m_data.stackCount - frameBase > 1)
                    LOG::Write("!!!ERROR!!!SCRIPT: '%s' stack error %i", "iff", cursor);
                m_data.stackCount = frameBase;
                break;
            }
            case script::VmOpcode::CallScriptFunction:
            {


                PushInt(frameBase);
                PushInt(cursor + 4);

                int calleeIndex = 0;
                readScriptDword(bytecodeStorage(), cursor, calleeIndex);
                frameBase = m_data.stackCount;

                const script::LogicFunctionRecord& callee =
                    functionRecordStorage()[static_cast<std::size_t>(calleeIndex)];
                if (callee.value0 < 0)
                {
                    LOG::Write("!!!ERROR!!!SCRIPT: Undefined function %s()", callee.name.c_str());
                    controlAnchor = cursor;
                    break;
                }

                cursor = callee.value0;
                controlAnchor = cursor;
                break;
            }
            case script::VmOpcode::Return:
            {
                if (m_data.stackCount - frameBase > 1)
                    LOG::Write("!!!ERROR!!!SCRIPT: '%s' stack error %i", "return", cursor - 1);

                script::StackObject returnedValue;
                bool hasReturnedValue = false;
                if (m_data.stackCount > frameBase)
                {
                    script::StackObject* top = mutableExecutionStackStorageAt(m_data.stackCount - 1);
                    returnedValue.copyFrom(*top);
                    hasReturnedValue = true;
                    --m_data.stackCount;
                }

                m_data.stackCount = frameBase;

                --m_data.stackCount;
                script::StackObject* returnCursorObject = mutableExecutionStackStorageAt(m_data.stackCount);
                const int returnCursor = scriptStackValueToInteger(*returnCursorObject);

                --m_data.stackCount;
                script::StackObject* savedFrameObject = mutableExecutionStackStorageAt(m_data.stackCount);
                frameBase = scriptStackValueToInteger(*savedFrameObject);
                cursor = returnCursor;
                controlAnchor = cursor;

                if (hasReturnedValue)
                {
                    reinterpret_cast<script::StackObjectList*>(&m_data.stackListVtable)->appendFields(
                        returnedValue.flags, returnedValue.intValue, returnedValue.text);

                    if (returnCursor >= bytecodeLimit && result == 0 &&
                        (returnedValue.flags & script::STACK_OBJECT_STRING) == 0)
                        result = scriptStackValueToInteger(returnedValue);
                }
                break;
            }
            case script::VmOpcode::ArrayIndex:
            {
                --m_data.stackCount;
                script::StackObject* const indexObject = mutableExecutionStackStorageAt(m_data.stackCount);
                arrayVmOffset = scriptStackValueToInteger(*indexObject);
                arrayVmActive = 1;
                break;
            }
            case script::VmOpcode::ConvertToString:
            {
                script::StackObject* const top = mutableExecutionStackStorageAt(m_data.stackCount - 1);
                if ((top->flags & script::STACK_OBJECT_INT) != 0)
                    top->text = script::IntToStackString(top->intValue);
                top->flags = script::STACK_OBJECT_STRING;
                break;
            }
            case script::VmOpcode::ConvertToInteger:
            {
                script::StackObject* const top = mutableExecutionStackStorageAt(m_data.stackCount - 1);
                top->intValue = scriptStackValueToInteger(*top);
                top->flags = script::STACK_OBJECT_INT;
                break;
            }
            case script::VmOpcode::ConvertToObject:
            {
                script::StackObject* const top = mutableExecutionStackStorageAt(m_data.stackCount - 1);
                top->intValue = scriptStackValueToInteger(*top);
                top->flags = static_cast<std::uint8_t>(script::STACK_OBJECT_INT |
                    (top->intValue != 0 ? script::STACK_OBJECT_REF : 0));
                break;
            }
            case script::VmOpcode::BranchIfTrue:
            case script::VmOpcode::BranchIfFalse:
            {
                const script::StackObject* const top =
                    mutableExecutionStackStorageAt(m_data.stackCount - 1);
                const bool condition = scriptStackValueToInteger(*top) != 0;
                int relative = 0;
                readScriptDword(bytecodeStorage(), cursor, relative);
                const bool takeFallthrough =
                    (opcode == script::opcodeValue(script::VmOpcode::BranchIfTrue))
                        ? condition
                        : !condition;
                cursor += takeFallthrough ? 4 : relative;
                break;
            }
            default:
            {

                constexpr std::uint8_t kResultProbeOpcode = 84;
                if (opcode == kResultProbeOpcode)
                {
                    const script::StackObject* top = mutableExecutionStackStorageAt(m_data.stackCount - 1);
                    if (scriptStackValueToInteger(*top) == pointerSentinel)
                        result = 1;
                }
                ScriptExecFunc(static_cast<int>(opcode));
                break;
            }
            }
        }

        m_data.stackCount = savedStackCount;
        if (savedStackCount > m_data.stackCapacity)
        {
            auto* const stackList = reinterpret_cast<script::StackObjectList*>(&m_data.stackListVtable);
            stackList->reserveExact(savedStackCount);
        }
        return result;
    }


    void SCRIPT::PushInt(int value)
    {
        script::StackObject obj;
        obj.assignFields(static_cast<std::uint8_t>(script::STACK_OBJECT_INT), value, STRING());
        appendExecutionStackObject(obj);
    }


    void SCRIPT::PushStr(const STRING& value)
    {
        STRING ebxTemp;
        ebxTemp.AssignAllocatedCopyWithoutRelease(value.c_str());

        appendExecutionStackRecord(
            static_cast<std::uint8_t>(script::STACK_OBJECT_STRING),
            0,
            ebxTemp);

        ebxTemp.ReleaseOwnedStorage();
    }


    int SCRIPT::PopInt()
    {
        const int oldIndex = m_data.stackCount - 1;
        script::StackObject* top = mutableExecutionStackStorageAt(oldIndex);
        --m_data.stackCount;
        return scriptStackValueToInteger(*top);
    }

    void SCRIPT::RunTimeError(int errorCode, const char* text, int value)
    {


        LOG::ResourceError("MAP", errorCode, text, value);
    }


    STRING* SCRIPT::PopStr()
    {
        const int newIndex = m_data.stackCount - 1;
        m_data.stackCount = newIndex;
        script::StackObject* const top = mutableExecutionStackStorageAt(newIndex);

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


}
