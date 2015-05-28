/**
 * @file
 */

#include <cassert>
#include <cstring>
#include "recv_packet.h"
#include "recv_utils.h"
#include "common_defines.h"
#include "common_logging.h"
#include "common_endian.h"

namespace spead2
{
namespace recv
{

/**
 * Retrieve bits [first, first+cnt) from a field.
 *
 * @pre 0 &lt;= @a first &lt; @a first + @a cnt &lt;= @a n and @a cnt &lt; @a n,
 * where @a n is the number of bits in T.
 */
template<typename T>
static inline T extract_bits(T value, int first, int cnt)
{
    assert(0 <= first && first + cnt <= 8 * sizeof(T));
    assert(cnt > 0 && cnt < 8 * sizeof(T));
    return (value >> first) & ((T(1) << cnt) - 1);
}

std::size_t decode_packet(packet_header &out, const uint8_t *data, std::size_t max_size)
{
    if (max_size < 8)
    {
        log_debug("packet rejected because too small (%d bytes)", max_size);
        return 0;
    }
    std::uint64_t header = load_be<std::uint64_t>(data);
    if (extract_bits(header, 48, 16) != magic_version)
    {
        log_debug("packet rejected because magic or version did not match");
        return 0;
    }
    int item_id_bits = extract_bits(header, 40, 8) * 8;
    int heap_address_bits = extract_bits(header, 32, 8) * 8;
    if (item_id_bits == 0 || heap_address_bits == 0)
    {
        log_debug("packet rejected because flavour is invalid");
        return 0;
    }
    if (item_id_bits + heap_address_bits != 8 * sizeof(item_pointer_t))
    {
        log_debug("packet rejected because flavour is not SPEAD-64-*");
        return 0;
    }

    out.n_items = extract_bits(header, 0, 16);
    if (std::size_t(out.n_items) * sizeof(item_pointer_t) + 8 > max_size)
    {
        log_debug("packet rejected because the items overflow the packet");
        return 0;
    }

    // Mark specials as not found
    out.heap_cnt = -1;
    out.heap_length = -1;
    out.payload_offset = -1;
    out.payload_length = -1;
    // Load for special items
    pointer_decoder decoder(heap_address_bits);
    for (int i = 0; i < out.n_items; i++)
    {
        item_pointer_t pointer = load_be<item_pointer_t>(data + 8 + i * sizeof(item_pointer_t));
        if (decoder.is_immediate(pointer))
        {
            switch (decoder.get_id(pointer))
            {
            case HEAP_CNT_ID:
                out.heap_cnt = decoder.get_immediate(pointer);
                break;
            case HEAP_LENGTH_ID:
                out.heap_length = decoder.get_immediate(pointer);
                break;
            case PAYLOAD_OFFSET_ID:
                out.payload_offset = decoder.get_immediate(pointer);
                break;
            case PAYLOAD_LENGTH_ID:
                out.payload_length = decoder.get_immediate(pointer);
                break;
            default:
                break;
            }
        }
    }
    if (out.heap_cnt == -1 || out.payload_offset == -1 || out.payload_length == -1)
    {
        log_debug("packet rejected because it does not have required items");
        return 0;
    }
    std::size_t size = out.payload_length + out.n_items * sizeof(item_pointer_t) + 8;
    if (size > max_size)
    {
        log_debug("packet rejected because payload length overflows packet size (%d > %d)",
                  size, max_size);
        return 0;
    }
    if (out.heap_length >= 0 && out.payload_offset + out.payload_length > out.heap_length)
    {
        log_debug("packet rejected because payload would overflow given heap length");
        return 0;
    }

    out.pointers = data + 8;
    out.payload = out.pointers + out.n_items * sizeof(item_pointer_t);
    out.heap_address_bits = heap_address_bits;
    return size;
}

} // namespace recv
} // namespace spead2
