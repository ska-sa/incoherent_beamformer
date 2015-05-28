/**
 * @file
 */

#ifndef SPEAD2_RECV_STREAM
#define SPEAD2_RECV_STREAM

#include <cstddef>
#include <deque>
#include <memory>
#include <utility>
#include <functional>
#include <future>
#include <mutex>
#include <type_traits>
#include <boost/asio.hpp>
#include "recv_live_heap.h"
#include "recv_reader.h"
#include "common_memory_pool.h"

namespace spead2
{

class thread_pool;

namespace recv
{

struct packet_header;

/**
 * Encapsulation of a SPEAD stream. Packets are fed in through @ref add_packet.
 * The base class does nothing with heaps; subclasses will typically override
 * @ref heap_ready and @ref stop_received to do further processing.
 *
 * A collection of partial heaps is kept. Heaps are removed from this collection
 * and passed to @ref heap_ready when
 * - They are known to be complete (a heap length header is present and all the
 *   corresponding payload has been received); or
 * - Too many heaps are live: the one with the lowest ID is aged out, even if
 *   incomplete
 * - The stream is stopped
 *
 * This class is @em not thread-safe. Almost all use cases (possibly excluding
 * testing) will derive from @ref stream.
 */
class stream_base
{
private:
    /**
     * Maximum number of live heaps permitted. Temporarily one more might be
     * present immediate prior to one being ejected.
     */
    std::size_t max_heaps;
    /// Live heaps, ordered by heap ID
    std::deque<live_heap> heaps;
    /// @ref stop_received has been called, either externally or by stream control
    bool stopped = false;
    /// Protocol bugs to be compatible with
    bug_compat_mask bug_compat;
    /// Memory pool used by heaps
    std::shared_ptr<memory_pool> pool;

    /**
     * Callback called when a heap is being ejected from the live list.
     * The heap might or might not be complete.
     */
    virtual void heap_ready(live_heap &&) {}

    // Prevent copying
    stream_base(const stream_base &) = delete;
    stream_base &operator=(const stream_base &) = delete;

public:
    static constexpr std::size_t default_max_heaps = 4;

    /**
     * Constructor.
     *
     * @param bug_compat   Protocol bugs to have compatibility with
     * @param max_heaps    Maximum number of live (in-flight) heaps held in the stream
     */
    explicit stream_base(bug_compat_mask bug_compat = 0, std::size_t max_heaps = default_max_heaps);
    virtual ~stream_base() = default;

    /**
     * Change the maximum heap count. This will not immediately cause heaps to
     * be ejected if over the limit, but will prevent any increase until the
     * number is back under the limit.
     */
    void set_max_heaps(std::size_t max_heaps);

    /**
     * Set a pool to use for allocating heap memory.
     */
    void set_memory_pool(std::shared_ptr<memory_pool> pool);

    /**
     * Add a packet that was received, and which has been examined by @a
     * decode_packet, and returns @c true if it is consumed. Even though @a
     * decode_packet does some basic sanity-checking, it may still be rejected
     * by @ref live_heap::add_packet e.g., because it is a duplicate.
     *
     * It is an error to call this after the stream has been stopped.
     */
    bool add_packet(const packet_header &packet);
    /**
     * Shut down the stream. This calls @ref flush.  Subclasses may override
     * this to achieve additional effects, but must chain to the base
     * implementation.
     *
     * It is undefined what happens if @ref add_packet is called after a stream
     * is stopped.
     */
    virtual void stop_received();

    // TODO: not thread-safe: needs to query via the strand
    bool is_stopped() const { return stopped; }

    bug_compat_mask get_bug_compat() const { return bug_compat; }

    /// Flush the collection of live heaps, passing them to @ref heap_ready.
    void flush();
};

/**
 * Stream that is fed by subclasses of @ref reader. Unless otherwise specified,
 * methods in @ref stream_base may only be called while holding the strand
 * contained in this class. The public interface functions must be called
 * from outside the strand (and outside the threads associated with the
 * io_service), but are not thread-safe relative to each other.
 *
 * This class is thread-safe. This is achieved mostly by having operations run
 * as completion handlers on a strand. The exception is @ref stop, which uses a
 * @c once to ensure that only the first call actually runs.
 */
class stream : protected stream_base
{
private:
    friend class reader;

    /**
     * Serialization of access.
     */
    boost::asio::io_service::strand strand;
    /**
     * Readers providing the stream data.
     */
    std::vector<std::unique_ptr<reader> > readers;

    /// Ensure that @ref stop is only run once
    std::once_flag stop_once;

    template<typename T, typename... Args>
    void emplace_reader_callback(Args&&... args)
    {
        if (!is_stopped())
        {
            readers.reserve(readers.size() + 1);
            reader *r = new T(*this, std::forward<Args>(args)...);
            std::unique_ptr<reader> ptr(r);
            readers.push_back(std::move(ptr));
        }
    }

protected:
    virtual void stop_received() override;

    /**
     * Schedule execution of the function object @a callback through the @c
     * io_service using the strand, and block until it completes. If the
     * function throws an exception, it is rethrown in this thread.
     */
    template<typename F>
    typename std::result_of<F()>::type run_in_strand(F &&func)
    {
        typedef typename std::result_of<F()>::type return_type;
        std::packaged_task<return_type()> task(std::forward<F>(func));
        auto future = task.get_future();
        get_strand().dispatch([&task]
        {
            /* This is subtle: task lives on the run_in_strand stack frame, so
             * we have to be very careful not to touch it after that function
             * exits. Calling task() directly can continue to touch task even
             * after it has unblocked the future. But the move constructor for
             * packaged_task will take over the shared state for the future.
             */
            std::packaged_task<return_type()> my_task(std::move(task));
            my_task();
        });
        return future.get();
    }

    /// Actual implementation of @ref stop
    void stop_impl();

public:
    using stream_base::get_bug_compat;
    using stream_base::default_max_heaps;

    boost::asio::io_service::strand &get_strand() { return strand; }

    // TODO: introduce constant for default max_heaps
    explicit stream(boost::asio::io_service &service, bug_compat_mask bug_compat = 0, std::size_t max_heaps = default_max_heaps);
    explicit stream(thread_pool &pool, bug_compat_mask bug_compat = 0, std::size_t max_heaps = default_max_heaps);
    virtual ~stream() override;

    void set_max_heaps(std::size_t max_heaps);
    void set_memory_pool(std::shared_ptr<memory_pool> pool);

    /**
     * Add a new reader by passing its constructor arguments, excluding
     * the initial @a stream argument.
     */
    template<typename T, typename... Args>
    void emplace_reader(Args&&... args)
    {
        // This would probably work better with a lambda (better forwarding),
        // but GCC 4.8 has a bug with accessing parameter packs inside a
        // lambda.
        run_in_strand(std::bind(
                &stream::emplace_reader_callback<T, const Args&...>,
                this, std::forward<Args>(args)...));
    }

    /**
     * Stop the stream and block until all the readers have wound up. After
     * calling this there should be no more outstanding completion handlers
     * in the thread pool.
     */
    void stop();
};

/**
 * Push packets found in a block of memory to a stream. Returns a pointer to
 * after the last packet found in the stream. Processing stops as soon as
 * after @ref decode_packet fails (because there is no way to find the next
 * packet after a corrupt one), but packets may still be rejected by the stream.
 *
 * The stream is @em not stopped.
 */
const std::uint8_t *mem_to_stream(stream_base &s, const std::uint8_t *ptr, std::size_t length);

} // namespace recv
} // namespace spead2

#endif // SPEAD2_RECV_STREAM
