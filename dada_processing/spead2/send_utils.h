/**
 * @file
 *
 * Miscellaneous utilities for encoding SPEAD data.
 */

#ifndef SPEAD2_SEND_UTILS_H
#define SPEAD2_SEND_UTILS_H

#include <cstdint>
#include <cassert>
#include "common_defines.h"

namespace spead2
{
namespace send
{

class pointer_encoder
{
private:
    int heap_address_bits;

public:
    explicit pointer_encoder(int heap_address_bits)
        : heap_address_bits(heap_address_bits)
    {
        assert(heap_address_bits > 0);
        assert(heap_address_bits < 8 * sizeof(item_pointer_t));
        assert(heap_address_bits % 8 == 0);
    }

    item_pointer_t encode_immediate(s_item_pointer_t id, s_item_pointer_t value) const
    {
        assert(id >= 0 && id < (s_item_pointer_t(1) << (8 * sizeof(item_pointer_t) - 1 - heap_address_bits)));
        assert(value >= 0 && value < (s_item_pointer_t(1) << heap_address_bits));
        return immediate_mask | (id << heap_address_bits) | value;
    }

    item_pointer_t encode_address(s_item_pointer_t id, s_item_pointer_t address) const
    {
        assert(id >= 0 && id < (s_item_pointer_t(1) << (8 * sizeof(item_pointer_t) - 1 - heap_address_bits)));
        assert(address >= 0 && address < (s_item_pointer_t(1) << heap_address_bits));
        return (id << heap_address_bits) | address;
    }
};

} // namespace send
} // namespace spead2

#endif // SPEAD2_SEND_UTILS_H
