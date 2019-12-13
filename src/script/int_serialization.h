// Copyright (c) 2019 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file
// LICENSE.

#pragma once

#include <cassert>
#include <cstdint>
#include <iterator>
#include <vector>

namespace bsv
{
    inline auto abs(const int64_t value)
    {
        const bool neg = value < 0;
        uint64_t absvalue = neg ? -(static_cast<uint64_t>(value)) : value;
        return absvalue;
    }

    // O models Concept of OutputIterator
    template <typename T, typename O>
    inline void serialize(const T& value, O o)
    {
        if(value == 0)
            return;

        const bool neg = value < 0;
        auto absvalue = abs(value);
        while(absvalue != 0)
        {
            auto tmp = absvalue & 0xff;
            absvalue >>= 8;
            if(absvalue != 0) // test that this is not the last byte
                *o = tmp;
            else
            {
                // - If the most significant byte is >= 0x80 and the value is
                // positive, push a new zero-byte to make the significant byte <
                // 0x80 again.
                // - If the most significant byte is >= 0x80 and the value is
                // negative, push a new 0x80 byte that will be popped off when
                // converting to an integral.
                // - If the most significant byte is < 0x80 and the value is
                // negative, add 0x80 to it, since it will be subtracted and
                // interpreted as a negative when converting to an integral.
                if((tmp & 0x80) != 0)
                {
                    *o = tmp;
                    ++o;
                    *o = neg ? 0x80 : 0;
                }
                else
                    *o = neg ? tmp | 0x80 : tmp;
            }
            ++o;
        }
    }

    // I Models the FwdIterator concept
    // T Models the Integer concept
    template <typename T, typename I>
    inline T deserialize(I f, I l, std::forward_iterator_tag)
    {
        // pre-condition: bounded_range(f, l) and f < l
        assert(f != l);

        T result{0};
        int i{};
        while(f != l)
        {
            T tmp{*f};
            if(next(f) == l)
            {
                // this is the last byte of input
                if(*f & 0x80)
                {
                    // and it's negative
                    tmp &= 0x7f;
                    tmp <<= (8 * i);
                    result |= tmp;
                    return -result;
                }
            }
            tmp <<= (8 * i);
            result |= tmp;

            f = next(f);
            ++i;
        }
        return result;
    }

    // I Models the BidirectionalIterator concept
    // T Models the Integer concept
    template <typename T, typename I>
    inline T deserialize(I f, I l, std::bidirectional_iterator_tag)
    {
        // pre-condition: bounded_range(f, l) and f < l
        assert(f != l);

        l = prev(l);
        T result{*l};
        bool negative{};
        if(*l & 0x80)
        {
            result &= 0x7f;
            negative = true;
        }

        if(f == l)
            return negative ? -result : result;

        do
        {
            l = prev(l);
            result <<= 8;
            result |= T{*l};
        } while(f != l);

        return negative ? -result : result;
    }


    // I Models the RandomAccess Iterator concept
    // T Models the Integer concept
    template <typename T, typename I>
    inline T deserialize(I f, I l, std::random_access_iterator_tag)
    {
        // pre-condition: bounded_range(f, l) and f < l
        assert(f != l);

        const auto d{static_cast<size_t>(distance(f, l))};

        T result{0};
        for(size_t i{0}; i < d - 1; ++i)
        {
            T tmp{*f};
            tmp <<= (8 * i);
            result |= tmp;

            f = next(f);
        }

        if(d > sizeof(T))
        {
            // assert(*f == 0x80);
            return result;
        }

        bool negative{};
        T tmp{*f};
        if(tmp & 0x80)
        {
            negative = true;
            tmp &= 0x7f;
        }

        tmp <<= (8 * (d - 1));
        result |= tmp;
        return negative ? -result : result;
    }

    // I Models the Iterator concept
    // T Models the Integer concept
    template <typename T, typename I>
    inline T deserialize(I f, I l)
    {
        // pre-condition: bounded_range(f, l) and f < l
        assert(f != l);
        return deserialize<T>(
            f, l, typename std::iterator_traits<I>::iterator_category());
    }

    inline bool IsMinimallyEncoded(const std::vector<uint8_t>& vch,
                                   size_t nMaxNumSize)
    {
        if(vch.size() > nMaxNumSize)
        {
            return false;
        }

        if(vch.size() > 0)
        {
            // Check that the number is encoded with the minimum possible number
            // of bytes.
            //
            // If the most-significant-byte - excluding the sign bit - is zero
            // then we're not minimal. Note how this test also rejects the
            // negative-zero encoding, 0x80.
            if((vch.back() & 0x7f) == 0)
            {
                // One exception: if there's more than one byte and the most
                // significant bit of the second-most-significant-byte is set it
                // would conflict with the sign bit. An example of this case is
                // +-255, which encode to 0xff00 and 0xff80 respectively.
                // (big-endian).
                if(vch.size() <= 1 || (vch[vch.size() - 2] & 0x80) == 0)
                {
                    return false;
                }
            }
        }

        return true;
    }

    inline bool MinimallyEncode(std::vector<uint8_t>& data)
    {
        if(data.size() == 0)
        {
            return false;
        }

        // If the last byte is not 0x00 or 0x80, we are minimally encoded.
        uint8_t last = data.back();
        if(last & 0x7f)
        {
            return false;
        }

        // If the script is one byte long, then we have a zero, which encodes as
        // an empty array.
        if(data.size() == 1)
        {
            data = {};
            return true;
        }

        // If the next byte has it sign bit set, then we are minimaly encoded.
        if(data[data.size() - 2] & 0x80)
        {
            return false;
        }

        // We are not minimally encoded, we need to figure out how much to trim.
        for(size_t i = data.size() - 1; i > 0; i--)
        {
            // We found a non zero byte, time to encode.
            if(data[i - 1] != 0)
            {
                if(data[i - 1] & 0x80)
                {
                    // We found a byte with it sign bit set so we need one more
                    // byte.
                    data[i++] = last;
                }
                else
                {
                    // the sign bit is clear, we can use it.
                    data[i - 1] |= last;
                }

                data.resize(i);
                return true;
            }
        }

        // If we the whole thing is zeros, then we have a zero.
        data = {};
        return true;
    }
}

