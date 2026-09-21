#include "compress.h"
#include <cstddef>
#include <cstdlib>
#include <new>

namespace as1 { namespace script
{
    void R_CODER::putByte(int value)
    {
        std::fputc(value & 0xFF, m_file);
    }


    void R_CODER::beginWrite(std::uint8_t firstByte, int position, FILE* file)
    {
        if (m_file)
            return;
        m_file = file;
        m_low = 0;
        m_range = 0x80000000u;
        m_currentByte = firstByte;
        m_step = 0;
        m_position = static_cast<std::uint32_t>(position);
    }

    void R_CODER::enc_normalize()
    {
        while (m_range <= 0x800000u)
        {
            std::uint32_t low = m_low;
            if (low >= 0x7F800000u)
            {
                if (low & 0x80000000u)
                {
                    putByte(static_cast<int>(m_currentByte) + 1);
                    while (m_step)
                    {
                        putByte(0);
                        --m_step;
                    }
                    low = m_low;
                    m_currentByte = static_cast<std::uint8_t>(low >> 23);
                }
                else
                {
                    ++m_step;
                }
            }
            else
            {
                putByte(m_currentByte);
                while (m_step)
                {
                    putByte(0xFF);
                    --m_step;
                }
                low = m_low;
                m_currentByte = static_cast<std::uint8_t>(low >> 23);
            }
            m_range <<= 8;
            m_low = (low & 0x7FFFFFu) << 8;
            ++m_position;
        }
    }


    int R_CODER::EncodeShift(int lowCount, int base, int shift)
    {
        enc_normalize();
        const std::uint32_t oldRange = m_range;
        const std::uint32_t step = oldRange >> (shift & 31);
        const std::uint32_t delta = static_cast<std::uint32_t>(base) * step;
        m_low += delta;
        if ((static_cast<std::uint32_t>(lowCount + base) >> (shift & 31)) != 0)
            m_range = oldRange - delta;
        else
            m_range = static_cast<std::uint32_t>(lowCount) * step;
        return static_cast<int>(step);
    }


    int R_CODER::finishWrite()
    {
        if (!m_file)
            return -1;
        enc_normalize();
        m_position += 5;
        const std::uint32_t low = m_low;
        std::uint32_t emit = (low & 0x7FFFFFu) >= ((m_position >> 1) & 0x7FFFFFu)
            ? (low >> 23) + 1
            : (low >> 23);
        const std::uint8_t finalByte = static_cast<std::uint8_t>(emit);
        if (emit <= 0xFFu)
        {
            putByte(m_currentByte);
            while (m_step)
            {
                putByte(0xFF);
                --m_step;
            }
        }
        else
        {
            putByte(static_cast<int>(m_currentByte) + 1);
            while (m_step)
            {
                putByte(0);
                --m_step;
            }
        }
        putByte(finalByte);
        putByte(static_cast<int>((m_position >> 16) & 0xFF));
        putByte(static_cast<int>((m_position >> 8) & 0xFF));
        putByte(static_cast<int>(m_position & 0xFF));
        return static_cast<int>(m_position);
    }

    int R_CODER::readByte()
    {
        return std::fgetc(m_file);
    }


    int R_CODER::beginRead(FILE* file)
    {
        if (m_file)
            return 0;
        m_file = file;
        const int first = readByte();
        if (first == EOF)
            return -1;
        const int second = readByte();
        m_currentByte = static_cast<std::uint8_t>(second);
        m_low = static_cast<std::uint32_t>(static_cast<std::uint8_t>(second)) >> 1;
        m_range = 0x80u;
        return first != 0 ? -1 : 0;
    }

    void R_CODER::dec_normalize()
    {
        while (m_range <= 0x800000u)
        {
            m_low = ((2u * m_low) | (m_currentByte & 1u)) << 7;
            const int ch = readByte();
            m_currentByte = static_cast<std::uint8_t>(ch);
            m_low |= static_cast<std::uint32_t>(m_currentByte) >> 1;
            m_range <<= 8;
        }
    }


    int R_CODER::decodeTarget(int shift)
    {
        while (m_range <= 0x800000u)
        {
            m_low = ((2u * m_low) | (m_currentByte & 1u)) << 7;
            const int ch = readByte();
            m_currentByte = static_cast<std::uint8_t>(ch);
            m_low |= static_cast<std::uint32_t>(m_currentByte) >> 1;
            m_range <<= 8;
        }
        const std::uint32_t step = m_range >> (shift & 31);
        m_step = step;
        std::uint32_t result = m_low / step;
        if ((result >> (shift & 31)) != 0)
            result = (1u << (shift & 31)) - 1u;
        return static_cast<int>(result);
    }


    int R_CODER::removeDecodedRange(int count, int base, unsigned total)
    {
        const std::uint32_t step = m_step;
        const std::uint32_t delta = static_cast<std::uint32_t>(base) * step;
        m_low -= delta;
        if (static_cast<unsigned>(count + base) >= total)
            m_range -= delta;
        else
            m_range = static_cast<std::uint32_t>(count) * step;
        return static_cast<int>(delta);
    }


    QSMODEL::QSMODEL()
        : cumulative(nullptr), frequency(nullptr), lookup(nullptr)
    {
        Init(257, 12, 2000, nullptr);
    }


    QSMODEL::~QSMODEL()
    {
        if (cumulative)
            ::operator delete(cumulative);
        cumulative = nullptr;
        if (frequency)
            ::operator delete(frequency);
        frequency = nullptr;
        if (lookup)
            ::operator delete(lookup);
        lookup = nullptr;
    }


    void QSMODEL::dorescale()
    {
        if (nextLeft)
        {
            ++increment;
            left = nextLeft;
            nextLeft = 0;
            return;
        }

        if (rescaleInterval < targetRescale)
        {
            rescaleInterval *= 2;
            if (rescaleInterval > targetRescale)
                rescaleInterval = targetRescale;
        }

        int current = static_cast<int>(cumulative[noSym]);
        int missing = current;
        for (int i = noSym - 1; i != 0; --i)
        {
            int value = static_cast<int>(frequency[i]);
            current -= value;
            cumulative[i] = static_cast<std::uint16_t>(current);
            value = (value | 2) >> 1;
            missing -= value;
            frequency[i] = static_cast<std::uint16_t>(value);
        }

        if (current != static_cast<int>(frequency[0]))
        {
            std::fprintf(stderr, "BUG: rescaling left %d total frequency\n",
                static_cast<int>(reinterpret_cast<std::intptr_t>(cumulative)));
            std::exit(1);
        }

        frequency[0] = static_cast<std::uint16_t>((static_cast<int>(frequency[0]) >> 1) | 1);
        missing -= static_cast<int>(frequency[0]);
        increment = missing / rescaleInterval;
        nextLeft = missing % rescaleInterval;
        left = rescaleInterval - nextLeft;

        if (lookup)
        {
            int i = noSym;
            while (i)
            {
                const int end = (static_cast<int>(cumulative[i]) - 1) >> searchShift;
                --i;
                int begin = static_cast<int>(cumulative[i]) >> searchShift;
                while (begin <= end)
                {
                    lookup[begin] = static_cast<std::uint16_t>(i);
                    ++begin;
                }
            }
        }
    }


    void QSMODEL::Init(int symbols, int shift, int rescale, int* initial)
    {
        targetRescale = rescale;
        noSym = symbols;
        searchShift = shift - 7;
        if (searchShift < 0)
            searchShift = 0;

        if (cumulative)
            ::operator delete(cumulative);
        cumulative = static_cast<std::uint16_t*>(::operator new(static_cast<std::size_t>(2 * symbols + 2)));
        if (frequency)
            ::operator delete(frequency);
        frequency = static_cast<std::uint16_t*>(::operator new(static_cast<std::size_t>(2 * symbols + 2)));
        if (lookup)
            ::operator delete(lookup);
        lookup = static_cast<std::uint16_t*>(::operator new(0x102u));

        cumulative[symbols] = static_cast<std::uint16_t>(1 << shift);
        cumulative[0] = 0;
        if (lookup)
            lookup[128] = static_cast<std::uint16_t>(symbols - 1);
        Reset(initial);
    }


    void QSMODEL::Reset(int* initial)
    {
        rescaleInterval = (noSym >> 4) | 2;
        nextLeft = 0;
        if (!initial)
        {
            const int initialValue = static_cast<int>(cumulative[noSym]) / noSym;
            const int remainder = static_cast<int>(cumulative[noSym]) % noSym;
            int i = 0;
            for (; i < remainder; ++i)
                frequency[i] = static_cast<std::uint16_t>(initialValue + 1);
            for (; i < noSym; ++i)
                frequency[i] = static_cast<std::uint16_t>(initialValue);
        }
        else
        {
            for (int i = 0; i < noSym; ++i)
                frequency[i] = static_cast<std::uint16_t>(initial[i]);
        }
        dorescale();
    }


    int QSMODEL::GetSym(int count)
    {
        const std::uint16_t* probe = lookup + (count >> searchShift);
        int lo = static_cast<int>(*probe);
        int hi = static_cast<int>(*(probe + 1)) + 1;
        while (lo + 1 < hi)
        {
            const int mid = (hi + lo) >> 1;
            if (count < static_cast<int>(cumulative[mid]))
                hi = mid;
            else
                lo = mid;
        }
        return lo;
    }


    void QSMODEL::GetFreq(int sym, int* freqOut, int* cumulativeFreq)
    {
        *cumulativeFreq = static_cast<int>(cumulative[sym]);
        *freqOut = static_cast<int>(cumulative[sym + 1]) - *cumulativeFreq;
    }


    void QSMODEL::Update(int sym)
    {
        if (left <= 0)
            dorescale();
        --left;
        frequency[sym] = static_cast<std::uint16_t>(static_cast<int>(frequency[sym]) + increment);
    }

    QS1_CODER::QS1_CODER(int mode)
        : byteInWord(mode)
    {
    }

    QS1_CODER::~QS1_CODER()
    {
    }

    void QS1_CODER::Reset()
    {
        for (QSMODEL& m : model)
            m.Reset(nullptr);
    }

    int QS1_CODER::Encode(const void* data, unsigned long size, FILE* file)
    {
        if (!file || size == 0)
            return 0;
        if (size < 0x0A)
            return static_cast<int>(std::fwrite(data, 1, size, file));

        const auto* bytes = static_cast<const std::uint8_t*>(data);
        R_CODER stream;
        stream.beginWrite(0, 0, file);
        const std::size_t half = (size + 1) >> 1;
        std::size_t physical = 0;
        int previous = 0;
        for (std::size_t logical = 0; logical < size; ++logical)
        {
            int symbol = 0;
            if (byteInWord == 2)
            {
                if (logical < half)
                    symbol = bytes[physical];
                else
                    symbol = bytes[physical - 2 * half + 1];
            }
            else
            {
                symbol = bytes[logical];
            }
            physical += 2;
            QSMODEL& modelRef = model[previous];
            int syFreq = 0;
            int ltFreq = 0;
            modelRef.GetFreq(symbol, &syFreq, &ltFreq);
            stream.EncodeShift(syFreq, ltFreq, 12);
            modelRef.Update(symbol);
            previous = symbol;
        }

        QSMODEL& finalModel = model[previous];
        int syFreq = 0;
        int ltFreq = 0;
        finalModel.GetFreq(256, &syFreq, &ltFreq);
        stream.EncodeShift(syFreq, ltFreq, 12);
        return stream.finishWrite();
    }

    int QS1_CODER::Decode(void* data, unsigned long size, FILE* file)
    {
        if (!file || size == 0)
            return 0;
        if (size < 0x0A)
            return static_cast<int>(std::fread(data, 1, size, file));

        auto* bytes = static_cast<std::uint8_t*>(data);
        R_CODER stream;
        if (stream.beginRead(file) < 0)
            return 0;

        const std::size_t half = (size + 1) >> 1;
        std::size_t physical = 0;
        std::size_t written = 0;
        int previous = 0;
        for (;;)
        {
            const int target = stream.decodeTarget(12);
            QSMODEL& modelRef = model[previous];
            const int symbol = modelRef.GetSym(target);
            if (symbol == 256)
                break;
            if (written >= size)
            {
                std::fprintf(stderr, "!!!ERROR!!! decode");
                break;
            }

            if (byteInWord == 2)
            {
                if (written < half)
                    bytes[physical] = static_cast<std::uint8_t>(symbol);
                else
                    bytes[physical - 2 * half + 1] = static_cast<std::uint8_t>(symbol);
            }
            else
            {
                bytes[written] = static_cast<std::uint8_t>(symbol);
            }

            ++written;
            physical += 2;
            int syFreq = 0;
            int ltFreq = 0;
            modelRef.GetFreq(symbol, &syFreq, &ltFreq);
            stream.removeDecodedRange(syFreq, ltFreq, 0x1000u);
            modelRef.Update(symbol);
            previous = symbol;
        }

        QSMODEL& finalModel = model[previous];
        int syFreq = 0;
        int ltFreq = 0;
        finalModel.GetFreq(256, &syFreq, &ltFreq);
        stream.removeDecodedRange(syFreq, ltFreq, 0x1000u);

        stream.dec_normalize();
        return static_cast<int>(written);
    }


} }
