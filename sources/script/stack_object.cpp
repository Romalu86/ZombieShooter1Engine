#include "stack_object.h"
#include "../core/base_stream.h"
#include "../core/log.h"
#include "../core/file_logger.h"
#include <cctype>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <new>
#include <cstddef>

namespace as1 { namespace script
{
    namespace
    {
        constexpr int DivideByZeroSentinel = 0x0FFFFFFF;

        bool isStringObject(const StackObject& value)
        {
            return (value.flags & STACK_OBJECT_STRING) != 0;
        }

        int stackCtypeArgument(unsigned char value) noexcept
        {
            return static_cast<int>(static_cast<signed char>(value));
        }

        StackObject* allocateStackRecords(int capacity)
        {
            if (capacity <= 0)
                return nullptr;
            const std::size_t bytes = sizeof(std::uint32_t) +
                static_cast<std::size_t>(capacity) * sizeof(StackObject);
            auto* raw = static_cast<std::uint8_t*>(::operator new(bytes));
            *reinterpret_cast<std::uint32_t*>(raw) = static_cast<std::uint32_t>(capacity);
            StackObject* records = reinterpret_cast<StackObject*>(raw + sizeof(std::uint32_t));
            int constructed = 0;
            try
            {
                for (; constructed < capacity; ++constructed)
                    ::new (static_cast<void*>(records + constructed)) StackObject();
            }
            catch (...)
            {
                while (constructed > 0)
                {
                    --constructed;
                    records[constructed].~StackObject();
                }
                ::operator delete(raw);
                throw;
            }
            return records;
        }

        void destroyStackRecords(StackObject* records) noexcept
        {
            if (!records)
                return;
            std::uint32_t* const header = reinterpret_cast<std::uint32_t*>(records) - 1;
            const std::uint32_t capacity = *header;
            for (std::uint32_t i = capacity; i != 0; --i)
                records[i - 1u].~StackObject();
            ::operator delete(static_cast<void*>(header));
        }
    }

    void StackObjectList::reserveExact(int capacity)
    {
        if (capacity <= m_capacity)
            return;
        StackObject* replacement = nullptr;
        try
        {
            replacement = allocateStackRecords(capacity);
        }
        catch (...)
        {
            fatalLogError(g_fileLogger, "!!!ERROR!!!::LIST: Not enough memory %i", capacity);
        }
        if (!replacement)
            fatalLogError(g_fileLogger, "!!!ERROR!!!::LIST: Not enough memory %i", capacity);

        StackObject* const oldRecords = data();
        m_tableToken = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(replacement));
        for (int i = 0; i < m_capacity; ++i)
        {
            replacement[i].copyStorageFrom(oldRecords[i]);
            replacement[i].flags = static_cast<std::uint8_t>(replacement[i].flags & 0x7Fu);
        }
        destroyStackRecords(oldRecords);
        m_capacity = capacity;
    }

    void StackObjectList::appendFields(std::uint8_t flags, int value, STRING text)
    {
        if (m_count >= m_capacity)
        {
            const int expandedCapacity = m_capacity * 2 + 4;
            if (expandedCapacity > m_capacity)
                reserveExact(expandedCapacity);
        }

        const int writeIndex = m_count;
        ++m_count;
        StackObject& destination = data()[static_cast<std::size_t>(writeIndex)];
        destination.flags = static_cast<std::uint8_t>(flags & 0x7Fu);
        destination.intValue = value;
        destination.text.Assign(text);
    }

    void StackObjectList::Insert(StackObject item)
    {
        appendFields(item.flags, item.intValue, item.text);
    }

    void StackObjectList::Push(const StackObject* item)
    {
        Insert(*item);
    }

    int ParseStackIntegerText(const char* text)
    {
        if (text[1] == 'x')
        {
            int value = 0;
            std::sscanf(text, "%i", &value);
            return value;
        }
        return std::atoi(text);
    }

    STRING IntToStackString(int value)
    {
        char buffer[0x80];
        std::snprintf(buffer, sizeof(buffer), "%d", value);
        if (buffer[0] == '\0')
            return STRING();
        return STRING(buffer);
    }


    StackObject::StackObject()
        : flags(0), intValue(0), text()
    {
    }


    StackObject::StackObject(const StackObject& other)
        : flags(other.flags), intValue(other.intValue), text(other.text)
    {
    }


    StackObject& StackObject::operator=(const StackObject& other)
    {
        if (this != &other)
        {
            flags = static_cast<std::uint8_t>(other.flags & 0x7Fu);
            intValue = other.intValue;
            text.Assign(other.text);
        }
        return *this;
    }

    StackObject::StackObject(int value)
    {
        assignInt(value);
    }


    StackObject::StackObject(int value, const STRING* context)
        : flags(STACK_OBJECT_INT), intValue(value), text()
    {
        (void)context;
    }


    StackObject::StackObject(const void* object, const STRING* context)
        : flags(static_cast<std::uint8_t>(STACK_OBJECT_INT | (object ? STACK_OBJECT_REF : 0))),
          intValue(static_cast<int>(static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(object)))),
          text()
    {
        (void)context;
    }

    StackObject::StackObject(std::uint8_t initialFlags, int value, const STRING& initialText)
    {
        assignFields(initialFlags, value, initialText);
    }


    int StackObject::Int() const
    {
        if (flags & STACK_OBJECT_STRING)
        {
            const char* const valueText = text.c_str();
            const unsigned char first = static_cast<unsigned char>(valueText[0]);
            const unsigned char second = static_cast<unsigned char>(valueText[1]);
            const bool startsWithNumber =
                std::isdigit(stackCtypeArgument(first)) != 0 ||
                (first == '-' &&
                 std::isdigit(stackCtypeArgument(second)) != 0);

            if (!startsWithNumber)
            {


                logAndShowError(g_fileLogger,
                    "!!!ERROR!!!SCRIPT: "
                    "\xEF\xF0\xE8\xF1\xE2\xE0\xE8\xE2\xE0\xED\xE8\xE5 "
                    "\xEF\xE5\xF0\xE5\xEC\xE5\xED\xED\xEE\xE9 int "
                    "\xF1\xF2\xF0\xEE\xEA\xE8 '%s'",
                    valueText);
            }

            return ParseStackIntegerText(valueText);
        }
        return intValue;
    }


    const STRING* StackObject::String()
    {
        if (flags & STACK_OBJECT_INT)
        {
            char buffer[0x80];
            text = STRING(_itoa(intValue, buffer, 10));
        }
        return &text;
    }

    void StackObject::assignInt(int value)
    {
        flags = STACK_OBJECT_INT;
        intValue = value;
        text = STRING();
    }

    void StackObject::assignFields(std::uint8_t initialFlags, int value, const STRING& initialText)
    {
        flags = initialFlags;
        intValue = value;
        text.Assign(initialText);
    }

    StackObject* StackObject::initializeReferenceValue(int value) noexcept
    {
        flags = static_cast<std::uint8_t>(STACK_OBJECT_INT | STACK_OBJECT_REF);
        intValue = value;
        text.ResetSharedEmptyWithoutRelease();
        if (value == 0)
            flags = STACK_OBJECT_INT;
        return this;
    }

    void StackObject::copyStorageFrom(const StackObject& other)
    {
        *this = other;
    }

    void StackObject::copyFrom(const StackObject& other)
    {
        copyStorageFrom(other);
    }


    void StackObject::Read(BaseStream* stream)
    {
        stream->read(&flags, 1);
        if ((flags & STACK_OBJECT_HAS_PAYLOAD) == 0)
            return;
        if (flags & STACK_OBJECT_STRING)
        {
            text.Read(stream);
            char* const bytes = const_cast<char*>(text.c_str());
            const std::size_t length = std::strlen(bytes);
            for (std::size_t i = 0; i < length; ++i)
                bytes[i] = static_cast<char>(static_cast<unsigned char>(bytes[i]) ^ 0x17u);
        }
        else
        {
            stream->read(&intValue, 4);
        }
    }


    void StackObject::Write(BaseStream* stream) const
    {
        stream->write(&flags, 1);
        if ((flags & STACK_OBJECT_HAS_PAYLOAD) == 0)
            return;
        if (flags & STACK_OBJECT_STRING)
        {


            STRING encoded(text);
            char* const bytes = const_cast<char*>(encoded.c_str());
            const std::size_t length = std::strlen(bytes);
            for (std::size_t i = 0; i < length; ++i)
                bytes[i] = static_cast<char>(static_cast<unsigned char>(bytes[i]) ^ 0x17u);
            stream->write(bytes, static_cast<unsigned>(length + 1u));
        }
        else
        {
            stream->write(&intValue, 4);
        }
    }


    void StackObject::BinarOperator(int operation, const StackObject& rhs)
    {
        const auto setNumericResult = [this](int value)
        {
            flags = STACK_OBJECT_INT;
            intValue = value;
        };


        if ((flags & STACK_OBJECT_STRING) != 0 &&
            (rhs.flags & STACK_OBJECT_STRING) != 0)
        {
            switch (static_cast<BinaryCommand>(operation))
            {
            case BinaryCommand::Add:
                text += rhs.text;
                flags = STACK_OBJECT_STRING;
                return;
            case BinaryCommand::Subtract:


                text.Replace(rhs.text.c_str(), "");
                flags = STACK_OBJECT_STRING;
                return;
            case BinaryCommand::Equal:
                setNumericResult(std::strcmp(text.c_str(), rhs.text.c_str()) == 0 ? 1 : 0);
                return;
            case BinaryCommand::NotEqual:
                setNumericResult(std::strcmp(text.c_str(), rhs.text.c_str()) != 0 ? 1 : 0);
                return;
            default:
                break;
            }
        }

        const int rhsValue = rhs.Int();
        if (flags & STACK_OBJECT_STRING)
            intValue = Int();

        switch (static_cast<BinaryCommand>(operation))
        {
        case BinaryCommand::Add:
            setNumericResult(intValue + rhsValue);
            break;
        case BinaryCommand::Subtract:
            setNumericResult(intValue - rhsValue);
            break;
        case BinaryCommand::Multiply:
            setNumericResult(intValue * rhsValue);
            break;
        case BinaryCommand::Divide:
            setNumericResult(rhsValue != 0 ? intValue / rhsValue : DivideByZeroSentinel);
            break;
        case BinaryCommand::Modulo:

            setNumericResult(intValue % rhsValue);
            break;
        case BinaryCommand::BitwiseOr:
            setNumericResult(intValue | rhsValue);
            break;
        case BinaryCommand::BitwiseXor:
            setNumericResult(intValue ^ rhsValue);
            break;
        case BinaryCommand::BitwiseAnd:
            setNumericResult(intValue & rhsValue);
            break;
        case BinaryCommand::ShiftLeft:
            setNumericResult(intValue << (rhsValue & 31));
            break;
        case BinaryCommand::ShiftRight:
            setNumericResult(intValue >> (rhsValue & 31));
            break;
        case BinaryCommand::LogicalAnd:
            setNumericResult((intValue != 0 && rhsValue != 0) ? 1 : 0);
            break;
        case BinaryCommand::LogicalOr:
            setNumericResult((intValue != 0 || rhsValue != 0) ? 1 : 0);
            break;
        case BinaryCommand::Less:
            setNumericResult(intValue < rhsValue ? 1 : 0);
            break;
        case BinaryCommand::LessEqual:
            setNumericResult(intValue <= rhsValue ? 1 : 0);
            break;
        case BinaryCommand::Greater:
            setNumericResult(intValue > rhsValue ? 1 : 0);
            break;
        case BinaryCommand::GreaterEqual:
            setNumericResult(intValue >= rhsValue ? 1 : 0);
            break;
        case BinaryCommand::Equal:
            if (isStringObject(rhs) && isStringObject(*this))
                setNumericResult(std::strcmp(text.c_str(), rhs.text.c_str()) == 0 ? 1 : 0);
            else
                setNumericResult(intValue == rhsValue ? 1 : 0);
            break;
        case BinaryCommand::NotEqual:
            if (isStringObject(rhs) && isStringObject(*this))
                setNumericResult(std::strcmp(text.c_str(), rhs.text.c_str()) != 0 ? 1 : 0);
            else
                setNumericResult(intValue != rhsValue ? 1 : 0);
            break;
        default:
            LOG::Write("!!!ERROE!!!LOGIC::Unknown Binary command %i", static_cast<int>(operation));
            flags = STACK_OBJECT_INT;
            break;
        }
    }

}}
