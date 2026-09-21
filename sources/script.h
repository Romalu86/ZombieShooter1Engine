#pragma once
#include "core/as_string.h"
#include "graphics/gamma.h"
#include "script/stack_object.h"
#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <functional>

namespace as1
{
    class VID;
    class SPRITE;
    struct WEAPON;
    namespace core { struct ApplicationDrawDispatcherState; }

    namespace script
    {
        enum class VmOpcode : std::uint8_t
        {
            PushInteger        = 1,
            PushString         = 2,
            Negate             = 3,
            BitwiseNot         = 4,
            LogicalNot         = 5,
            If                 = 24,
            StatementEnd       = 25,
            Pop                = 26,
            Jump               = 28,
            IfFalseChain       = 29,
            CallScriptFunction = 30,
            Return             = 31,
            PostIncrement      = 32,
            PostDecrement      = 33,
            PreIncrement       = 34,
            PreDecrement       = 35,
            ReadVariable       = 36,
            AddressOf          = 37,
            Assign             = 38,
            CompoundAssign     = 39,
            ArrayIndex         = 40,
            ConvertToString    = 41,
            ConvertToInteger   = 42,
            ConvertToObject    = 43,
            BranchIfTrue       = 44,
            BranchIfFalse      = 45,
        };

        constexpr std::uint8_t opcodeValue(VmOpcode command) noexcept
        {
            return static_cast<std::uint8_t>(command);
        }

        struct LogicFunctionRecord
        {
            STRING name;
            std::uint8_t flags = 0;
            std::uint8_t statusFlags = 0;
            std::uint8_t reserved06 = 0;
            std::uint8_t reserved07 = 0;
            STRING text;
            int value0;
            int value1;
            int value2;

            LogicFunctionRecord();
            void copyFrom(const LogicFunctionRecord& other);
            void resetDefaults();
        };

        void* destroyLogicFunctionRecordStorage(LogicFunctionRecord* self, unsigned char flags) noexcept;
    }

    struct ScriptDefinePairRecord
    {
        STRING name;
        STRING value;
    };

    void* scriptDefinePairDeletingDestructor(ScriptDefinePairRecord* self, unsigned char flags) noexcept;
    void destroyScriptDefinePair(ScriptDefinePairRecord* self) noexcept;

    struct ScriptData
    {


        std::uint32_t stackListVtable = 0;
        std::int32_t stackCount = 0;
        std::int32_t stackCapacity = 0;
        std::uint32_t stackTableToken = 0;
        std::uint32_t functionListVtable = 0;
        std::int32_t functionCount = 0;
        std::int32_t functionCapacity = 0;
        std::uint32_t functionTableToken = 0;
        std::uint32_t defineListVtable = 0;
        std::int32_t defineCount = 0;
        std::int32_t defineCapacity = 0;
        std::uint32_t defineTableToken = 0;
        std::uint32_t currentSymbolToken = 0;
        std::uint32_t scriptFileToken = 0;
        std::uint32_t ownedBufferToken = 0;
        std::uint32_t bytecodeBufferToken = 0;
        std::int32_t bytecodeEnd = 0;
        std::uint32_t sourceCursor = 0;
        std::uint32_t sourceEnd = 0;
        std::uint32_t sourceBufferToken = 0;
        std::int32_t sourceLine = -1;
        std::int32_t conditionalDepth = 0;
        std::int32_t fallbackFunction = -1;
        std::int32_t parseMode = 0;
        std::int32_t actionN[256]{};
        std::int32_t scriptEventFunction[256]{};
        std::uint32_t runtimeScratch860 = 0;
        std::uint32_t runtimeScratch864 = 0;
        std::int32_t compileError = 0;
        std::int32_t parseContext = 0;
    };


    struct ScriptNativeContext
    {
        std::function<void(const STRING& path)> requestMapLoad;
    };



    class SCRIPT
    {
    public:
        SCRIPT();
        ~SCRIPT();
        SCRIPT(const SCRIPT&) = delete;
        SCRIPT& operator=(const SCRIPT&) = delete;

        int compileScriptSourceFile(const STRING& scriptFile, const STRING& gameRoot);
        int Load(const STRING& scriptFile);
        void writeExecutionStackToStream(BaseStream* stream);
        void readExecutionStackFromStream(BaseStream* stream);
        bool isLoaded() const { return m_data.bytecodeEnd != 0; }

        void SetNativeContext(const ScriptNativeContext& context);

        static constexpr std::uint32_t InitialListCapacity = 0x80u;
        static constexpr std::uint32_t TemporaryBytecodeCapacity = 0x3E800u;
        static constexpr std::uint32_t SourceBufferPadding = 0x1014u;
        static constexpr std::uint32_t SourcePayloadOffset = 0x0FE2u;
        void clearExecutionStack();
        int executionStackCount() const;
        int executionStackCapacity() const;
        script::StackObject* mutableExecutionStackStorageAt(int index);
        void appendExecutionStackObject(const script::StackObject& value);

        int DeletePointerToObject(void* object);
        void growExecutionStackForAppend();
        void appendExecutionStackRecord(std::uint8_t flags, int value, const STRING& text);
        int functionCount() const;
        int functionCapacity() const;
        const script::LogicFunctionRecord* functionRecordAt(int index) const noexcept;
        script::LogicFunctionRecord* mutableFunctionRecordAt(int index) noexcept;
        int getFunctionIndex(const STRING& name) const noexcept;
        int defineCount() const;
        int defineCapacity() const;
        const STRING& scriptFile() const;
        int bytecodeEnd() const;
        int sourceCursorOffset() const;
        int sourceEndOffset() const;
        int conditionalDepth() const;
        int parseMode() const;
        std::uint8_t sourceByteAtCursor() const;
        void setSourceCursorOffset(int offset);
        void reportCompileError(int errorCode, const char* detailText, int detailValue);

        void EmitByteIfNoError(std::uint8_t opcode);

        void EmitByteInt32(std::uint8_t opcode, int value);
        int skipTriviaAndPreprocess();
        int requireSourceToken();
        int readSourceLine(STRING& outLine);
        int readIdentifier(STRING& outName);
        int matchToken(const char* token);
        int requireToken(const char* token);
        int parseConstantIntExpression();
        int readQuotedStringLiteral(char* outText);
        int setLastFunctionElementCount(int argCount);
        void compileIntDeclaration(int declarationMode);
        void compileStringDeclaration(int declarationMode);

        int mnog();

        void SetOperation(int byteCodePos, int operation);

        void slag();

        void cmpslag();

        void logicslag();

        void vyragAnd();

        void vyragXor();

        void vyragOr();

        void vyragCmpAnd();

        void CompileExpression(int parseContext = 0);

        int vyrag_oper();
        void compileStatement(std::int32_t* breakPatchList);
        int compileNextSourceItem();
        int compileDefineDirective();
        int compileUndefDirective();
        int compileIncludeDirective();
        int compileExternDirective();
        int compileFunctionDirective();
        void resetScriptVmState();
        void prepareSourceCompiler(const STRING& scriptFile, BaseStream* stream, std::uint32_t sourceSize);
        void clearFunctionTable();
        void appendFunctionRecord(const STRING& name, std::uint8_t flags, const STRING& text, int bytecodeStart0C, int stackBase10, int argCount14);
        void clearDefines();
        int findDefine(const STRING& name) const;
        int addOrReplaceDefine(const STRING& name, const STRING& value);
        int undefine(const STRING& name);
        int rewriteDefineMacro(int tokenStartOffset, int tokenLength);
        int popSpriteReferenceValue();
        SPRITE* popSpriteReference();
        void pushSpriteReference(SPRITE* sprite);
        void PushInt(int value);
        void pushSpriteReferenceValue(int value, const STRING* context);
        void PushStr(const STRING& value);
        int PopInt();
        as1::VID* popVidValue(const char* errorContext);
        void RunTimeError(int errorCode, const char* text, int value);
        const char* stringTextPointer(const STRING& value) const;
        int writeCStringToStream(const STRING& source, BaseStream* target) const;
        std::size_t writeCStringRecord(const STRING& value, std::FILE* file) const;
        std::FILE* openScriptFile(const STRING& path, const char* mode) const;
        int IsLastStackString() const;
        int topValueIsString() const { return IsLastStackString(); }
        STRING* PopStr();


        STRING GetVariableStr(const STRING& expression);


        int getActionVariableInt(int actionIndex);


        void SetVariableActionInt(int actionIndex, int value);


        int callFunction(int functionIndex, const char* argumentTypes, int arg1, int arg2, int arg3);

    private:
        friend class MAP;
        static std::uint32_t pointerToken(const void* pointer) noexcept;
        script::StackObject* executionStackStorage() noexcept;
        const script::StackObject* executionStackStorage() const noexcept;
        script::LogicFunctionRecord* functionRecordStorage() noexcept;
        const script::LogicFunctionRecord* functionRecordStorage() const noexcept;
        ScriptDefinePairRecord* defineRecordStorage() noexcept;
        const ScriptDefinePairRecord* defineRecordStorage() const noexcept;
        STRING& scriptFileStorage() noexcept;
        const STRING& scriptFileStorage() const noexcept;
        std::uint8_t* bytecodeStorage() noexcept;
        const std::uint8_t* bytecodeStorage() const noexcept;
        std::uint8_t* sourceStorage() noexcept;
        const std::uint8_t* sourceStorage() const noexcept;
        void setSourceEndOffset(int offset) noexcept;
        void syncBackingPointers() noexcept;
        void syncFunctionList() noexcept;
        void syncDefineList() noexcept;
        void syncSourcePointers() noexcept;
        ScriptData m_data{};

    };


}
