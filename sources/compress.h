#pragma once
#include "core/resource_filter.h"
#include <cstdint>
#include <cstdio>

namespace as1
{
    namespace script
    {
        class R_CODER
        {
        public:
            void beginWrite(std::uint8_t firstByte, int position, FILE* file);
            int EncodeShift(int lowCount, int base, int shift);
            int finishWrite();

            int beginRead(FILE* file);
            int decodeTarget(int shift);
            int removeDecodedRange(int count, int base, unsigned total);

            std::uint32_t low() const { return m_low; }
            std::uint32_t range() const { return m_range; }
            std::uint32_t step() const { return m_step; }
            std::uint32_t position() const { return m_position; }
            std::uint8_t currentByte() const { return m_currentByte; }
            FILE* file() const { return m_file; }

        private:
            friend class QS1_CODER;
            void enc_normalize();
            void dec_normalize();
            void putByte(int value);
            int readByte();

            std::uint32_t m_low = 0;
            std::uint32_t m_range = 0;
            std::uint32_t m_step = 0;
            std::uint8_t m_currentByte = 0;
            std::uint32_t m_position = 0;
            FILE* m_file = nullptr;
        };


        class QSMODEL
        {
        public:
            int noSym;
            int left;
            int nextLeft;
            int rescaleInterval;
            int targetRescale;
            int increment;
            int searchShift;
            std::uint16_t* cumulative;
            std::uint16_t* frequency;
            std::uint16_t* lookup;


            QSMODEL();

            ~QSMODEL();

            void dorescale();

            void Init(int symbols, int shift, int rescale, int* initial);

            void Reset(int* initial);

            int GetSym(int count);

            void GetFreq(int sym, int* freq, int* cumulativeFreq);

            void Update(int sym);
        };

        class QS1_CODER : public as1::Filter
        {
        public:
            int byteInWord;
            QSMODEL model[256];

            explicit QS1_CODER(int mode);
            ~QS1_CODER() override;
            void Reset() override;
            int Encode(const void* data, unsigned long size, FILE* file) override;
            int Decode(void* data, unsigned long size, FILE* file) override;
        };

    }
}
