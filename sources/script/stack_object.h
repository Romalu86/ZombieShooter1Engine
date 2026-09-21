#pragma once

#include "../core/as_string.h"
#include <cstdint>
#include <cstddef>

namespace as1
{
    class BaseStream;

    namespace script
    {
        enum class BinaryCommand : std::uint8_t
        {
            Divide        = 6,
            Modulo        = 7,
            Add           = 8,
            Subtract      = 9,
            BitwiseXor    = 10,
            BitwiseOr     = 11,
            BitwiseAnd    = 12,
            Equal         = 13,
            LogicalOr     = 14,
            Greater       = 15,
            Less          = 16,
            GreaterEqual  = 17,
            LessEqual     = 18,
            Multiply      = 19,
            NotEqual      = 20,
            LogicalAnd    = 21,
            ShiftRight    = 22,
            ShiftLeft     = 23,
        };

        constexpr std::uint8_t opcodeValue(BinaryCommand command) noexcept
        {
            return static_cast<std::uint8_t>(command);
        }

        enum StackObjectFlags : std::uint8_t
        {
            STACK_OBJECT_STRING      = 1u << 0,
            STACK_OBJECT_INT         = 1u << 1,
            STACK_OBJECT_ARRAY       = 1u << 2,
            STACK_OBJECT_HAS_PAYLOAD = 1u << 3,
            STACK_OBJECT_REF         = 1u << 4,
            STACK_OBJECT_DYNAMIC     = 1u << 5,
            STACK_OBJECT_CHAR_WRITE  = 1u << 6,
        };

        struct StackObject
        {
            std::uint8_t flags = 0;
            int intValue = 0;
            STRING text;

            StackObject();
            StackObject& operator=(const StackObject& other);
            StackObject(const StackObject& other);
            explicit StackObject(int value);
            StackObject(int value, const STRING* context);
            StackObject(const void* object, const STRING* context);
            StackObject(std::uint8_t initialFlags, int value, const STRING& initialText);

            int Int() const;
            const STRING* String();
            void assignInt(int value);
            void assignFields(std::uint8_t initialFlags, int value, const STRING& initialText);
            StackObject* initializeReferenceValue(int value) noexcept;
            void copyStorageFrom(const StackObject& other);
            void copyFrom(const StackObject& other);
            void Read(BaseStream* stream);
            void Write(BaseStream* stream) const;
            void BinarOperator(int operation, const StackObject& rhs);
        };

        class StackObjectList
        {
        public:
            void reserveExact(int capacity);
            void appendFields(std::uint8_t flags, int value, STRING text);
            void Insert(StackObject item);
            void Push(const StackObject* item);

            int count() const { return m_count; }
            int capacity() const { return m_capacity; }
            StackObject* data() noexcept
            {
                return reinterpret_cast<StackObject*>(static_cast<std::uintptr_t>(m_tableToken));
            }
            const StackObject* data() const noexcept
            {
                return reinterpret_cast<const StackObject*>(static_cast<std::uintptr_t>(m_tableToken));
            }
            const StackObject& operator[](int index) const { return data()[static_cast<std::size_t>(index)]; }
            StackObject& operator[](int index) { return data()[static_cast<std::size_t>(index)]; }

        private:
            std::uint32_t m_vtableToken = 0;
            int m_count = 0;
            int m_capacity = 0;
            std::uint32_t m_tableToken = 0;
        };

        STRING IntToStackString(int value);
        int ParseStackIntegerText(const char* text);
    }
}
