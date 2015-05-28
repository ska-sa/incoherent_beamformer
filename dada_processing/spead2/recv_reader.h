/**
 * @file
 */

#ifndef SPEAD2_RECV_READER_H
#define SPEAD2_RECV_READER_H

#include <boost/asio.hpp>
#include <future>

namespace spead2
{
namespace recv
{

class stream;
class stream_base;

/**
 * Abstract base class for asynchronously reading data and passing it into
 * a stream. Subclasses will usually override @ref stop.
 *
 * The lifecycle of a reader is:
 * - construction (strand held)
 * - stop (strand held)
 * - join (strand not held)
 * - destruction (strand held)
 */
class reader
{
private:
    stream &owner;  ///< Owning stream
    std::promise<void> stopped_promise; ///< Promise filled when last completion handler done

protected:
    /// Called by last completion handler
    void stopped();

public:
    explicit reader(stream &owner) : owner(owner) {}
    virtual ~reader() = default;

    /// Retrieve the wrapped stream
    stream &get_stream() const { return owner; }

    /**
     * Retrieve the wrapped stream's base class. This must only be used when
     * the stream's strand is held.
     */
    stream_base &get_stream_base() const;

    /// Retrieve the io_service corresponding to the owner
    boost::asio::io_service &get_io_service();

    /**
     * Cancel any pending asynchronous operations. This is called with the
     * owner's strand held. This function does not need to wait for
     * completion handlers to run, but if there are any, the destructor must
     * wait for them.
     */
    virtual void stop() = 0;

    /**
     * Block until @ref stopped has been called by the last completion
     * handler. This function is called outside the strand.
     */
    void join();
};

} // namespace recv
} // namespace spead2

#endif // SPEAD2_RECV_READER_H
