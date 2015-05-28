/**
 * @file
 */

#ifndef SPEAD2_SEND_STREAM_H
#define SPEAD2_SEND_STREAM_H

#include <functional>
#include <utility>
#include <vector>
#include <memory>
#include <queue>
#include <chrono>
#include <mutex>
#include <condition_variable>
#include <stdexcept>
#include <boost/asio.hpp>
#include <boost/asio/high_resolution_timer.hpp>
#include <boost/system/error_code.hpp>
#include "send_heap.h"
#include "send_packet.h"
#include "common_logging.h"
#include "common_defines.h"

namespace spead2
{
namespace send
{

class stream_config
{
public:
    static constexpr std::size_t default_max_packet_size = 1472;
    static constexpr std::size_t default_max_heaps = 4;
    static constexpr std::size_t default_burst_size = 65536;

    void set_max_packet_size(std::size_t max_packet_size);
    std::size_t get_max_packet_size() const;
    void set_rate(double rate);
    double get_rate() const;
    void set_burst_size(std::size_t burst_size);
    std::size_t get_burst_size() const;
    void set_max_heaps(std::size_t max_heaps);
    std::size_t get_max_heaps() const;

    explicit stream_config(
        std::size_t max_packet_size = default_max_packet_size,
        double rate = 0.0,
        std::size_t burst_size = default_burst_size,
        std::size_t max_heaps = default_max_heaps);

private:
    std::size_t max_packet_size = default_max_packet_size;
    double rate = 0.0;
    std::size_t burst_size = default_burst_size;
    std::size_t max_heaps = default_max_heaps;
};

/**
 * Stream that sends packets at a maximum rate. It also serialises heaps so
 * that only one heap is being sent at a time. Heaps are placed in a queue, and if
 * the queue becomes too long heaps are discarded.
 */
template<typename Derived>
class stream
{
private:
    typedef std::function<void(const boost::system::error_code &ec, item_pointer_t bytes_transferred)> completion_handler;
    typedef boost::asio::basic_waitable_timer<std::chrono::high_resolution_clock> timer_type;

    struct queue_item
    {
        const heap &h;
        completion_handler handler;

        queue_item() = default;
        queue_item(const heap &h, completion_handler &&handler)
            : h(std::move(h)), handler(std::move(handler))
        {
        }
    };

    boost::asio::io_service &io_service;
    const stream_config config;
    const double seconds_per_byte;

    /**
     * Protects access to @a queue. All other members are either const or are
     * only accessed only by completion handlers, and there is only ever one
     * scheduled at a time.
     */
    std::mutex queue_mutex;
    std::queue<queue_item> queue;
    timer_type timer;
    timer_type::time_point send_time;
    /// Number of bytes sent in the current heap
    item_pointer_t heap_bytes = 0;
    /// Number of bytes sent since last sleep
    std::size_t rate_bytes = 0;
    std::unique_ptr<packet_generator> gen; // TODO: make this inlinable
    /// Signalled whenever a heap is popped from the queue
    std::condition_variable heap_popped;

    void send_next_packet()
    {
        bool again;
        do
        {
            assert(gen);
            again = false;
            packet pkt = gen->next_packet();
            if (pkt.buffers.empty())
            {
                // Reached the end of a heap. Pop the current one, and start the
                // next one if there is one.
                completion_handler handler;
                bool empty;

                gen.reset();
                std::unique_lock<std::mutex> lock(queue_mutex);
                handler = std::move(queue.front().handler);
                queue.pop();
                empty = queue.empty();
                if (!empty)
                    gen.reset(new packet_generator(queue.front().h, config.get_max_packet_size()));
                else
                    gen.reset();

                std::size_t old_heap_bytes = heap_bytes;
                heap_bytes = 0;
                heap_popped.notify_all();
                again = !empty;  // Start again on the next heap
                lock.unlock();

                /* At this point it is not safe to touch *this at all, because
                 * if the queue is empty, the destructor is free to complete
                 * and take the memory out from under us.
                 */
                handler(boost::system::error_code(), old_heap_bytes);
            }
            else
            {
                static_cast<Derived *>(this)->async_send_packet(
                    pkt,
                    [this] (const boost::system::error_code &ec, std::size_t bytes_transferred)
                    {
                        // TODO: log the error? Abort on error?
                        bool sleeping = false;
                        rate_bytes += bytes_transferred;
                        heap_bytes += bytes_transferred;
                        if (rate_bytes >= config.get_burst_size())
                        {
                            std::chrono::duration<double> wait(rate_bytes * seconds_per_byte);
                            send_time += std::chrono::duration_cast<timer_type::clock_type::duration>(wait);
                            rate_bytes = 0;
                            auto now = timer_type::clock_type::now();
                            if (now < send_time)
                            {
                                sleeping = true;
                                timer.expires_at(send_time);
                                timer.async_wait([this] (const boost::system::error_code &error)
                                {
                                    send_next_packet();
                                });
                            }
                            else
                            {
                                // If we fall behind, don't try to make it up
                                send_time = now;
                            }
                        }
                        if (!sleeping)
                            send_next_packet();
                    });
            }
        } while (again);
    }

public:
    stream(
        boost::asio::io_service &io_service,
        const stream_config &config = stream_config()) :
            io_service(io_service),
            config(config),
            seconds_per_byte(config.get_rate() > 0.0 ? 1.0 / config.get_rate() : 0.0),
            timer(io_service)
    {
    }

    boost::asio::io_service &get_io_service() const { return io_service; }

    /**
     * Send @a h asynchronously, with @a handler called on completion. The
     * caller must ensure that @a h remains valid (as well as any memory it
     * points to) until @a handler is called.
     */
    void async_send_heap(const heap &h, completion_handler handler)
    {
        std::unique_lock<std::mutex> lock(queue_mutex);
        if (queue.size() >= config.get_max_heaps())
        {
            log_warning("async_send_heap: dropping heap because queue is full");
            // TODO: send an error code to the handler
            handler(boost::asio::error::would_block, 0);
            return;
        }
        bool empty = queue.empty();
        queue.emplace(h, std::move(handler));
        if (empty)
        {
            assert(!gen);
            gen.reset(new packet_generator(queue.front().h, config.get_max_packet_size()));
        }
        lock.unlock();

        /* If it is not empty, the new heap will be started as a continuation
         * of the previous one.
         */
        if (empty)
        {
            send_time = timer_type::clock_type::now();
            io_service.dispatch([this] { send_next_packet(); });
        }
    }

    /**
     * Block until all enqueued heaps have been sent. This function is
     * thread-safe, but can be live-locked if more heaps are added while it is
     * running.
     */
    void flush()
    {
        std::unique_lock<std::mutex> lock(queue_mutex);
        while (!queue.empty())
        {
            heap_popped.wait(lock);
        }
    }

    ~stream()
    {
        // TODO: add a stop member function and use that install
        flush();
    }
};

} // namespace send
} // namespace spead2

#endif // SPEAD2_SEND_STREAM_H
