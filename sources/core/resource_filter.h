#pragma once
#include <cstdio>

namespace as1
{


    class Filter
    {
    public:
        virtual ~Filter() = default;
        virtual void StartEncoding(FILE*) {}
        virtual int StartDecoding(FILE*) { return 0; }
        virtual int EndEncoding() { return 0; }
        virtual void EndDecoding() {}
        virtual void EncodeByte(int) {}
        virtual int DecodeByte() { return -1; }
        virtual void Reset() {}
        virtual int Encode(const void* data, unsigned long size, FILE* file) = 0;
        virtual int Decode(void* data, unsigned long size, FILE* file) = 0;
    };
}
