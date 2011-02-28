// Copyright (C) 2010  Internet Systems Consortium, Inc. ("ISC")
//
// Permission to use, copy, modify, and/or distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.
//
// THE SOFTWARE IS PROVIDED "AS IS" AND ISC DISCLAIMS ALL WARRANTIES WITH
// REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
// AND FITNESS.  IN NO EVENT SHALL ISC BE LIABLE FOR ANY SPECIAL, DIRECT,
// INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
// LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE
// OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
// PERFORMANCE OF THIS SOFTWARE.

#ifndef __IO_ASIO_SOCKET_H
#define __IO_ASIO_SOCKET_H 1

// IMPORTANT NOTE: only very few ASIO headers files can be included in
// this file.  In particular, asio.hpp should never be included here.
// See the description of the namespace below.
#include <unistd.h>             // for some network system calls

#include <functional>
#include <string>

#include <exceptions/exceptions.h>
#include <coroutine.h>

#include <dns/buffer.h>

#include <asiolink/io_error.h>
#include <asiolink/io_socket.h>


namespace asiolink {

/// \brief Socket not open
///
/// Thrown on an attempt to do read/write to a socket that is not open.
class SocketNotOpen : public IOError {
public:
    SocketNotOpen(const char* file, size_t line, const char* what) :
        IOError(file, line, what) {}
};

/// \brief Error setting socket options
///
/// Thrown if attempt to change socket options fails.
class SocketSetError : public IOError {
public:
    SocketSetError(const char* file, size_t line, const char* what) :
        IOError(file, line, what) {}
};

/// \brief Buffer overflow
///
/// Thrown if an attempt is made to receive into an area beyond the end of
/// the receive data buffer.
class BufferOverflow : public IOError {
public:
    BufferOverflow(const char* file, size_t line, const char* what) :
        IOError(file, line, what) {}
};

/// Forward declaration of an IOEndpoint
class IOEndpoint;


/// \brief I/O Socket with asynchronous operations
///
/// This class is a wrapper for the ASIO socket classes such as
/// \c ip::tcp::socket and \c ip::udp::socket.
///
/// This is the basic IOSocket with additional operations - open, send, receive
/// and close.  Depending on how the asiolink code develops, it may be a
/// temporary class: its main use is to add the template parameter needed for
/// the derived classes UDPSocket and TCPSocket but without changing the
/// signature of the more basic IOSocket class.
///
/// We may revisit this decision when we generalize the wrapper and more
/// modules use it.  Also, at that point we may define a separate (visible)
/// derived class for testing purposes rather than providing factory methods
/// (i.e., getDummy variants below).
///
/// TODO: Check if IOAsioSocket class is still needed
///
/// \param C Template parameter identifying type of the callback object.

template <typename C>
class IOAsioSocket : public IOSocket {
    ///
    /// \name Constructors and Destructor
    ///
    /// Note: The copy constructor and the assignment operator are
    /// intentionally defined as private, making this class non-copyable.
    //@{
private:
    IOAsioSocket(const IOAsioSocket<C>& source);
    IOAsioSocket& operator=(const IOAsioSocket<C>& source);
protected:
    /// \brief The default constructor.
    ///
    /// This is intentionally defined as \c protected as this base class
    /// should never be instantiated (except as part of a derived class).
    IOAsioSocket() {}
public:
    /// The destructor.
    virtual ~IOAsioSocket() {}
    //@}

    /// \brief Return the "native" representation of the socket.
    ///
    /// In practice, this is the file descriptor of the socket for UNIX-like
    /// systems so the current implementation simply uses \c int as the type of
    /// the return value. We may have to need revisit this decision later.
    ///
    /// In general, the application should avoid using this method; it
    /// essentially discloses an implementation specific "handle" that can
    /// change the internal state of the socket (consider what would happen if
    /// the application closes it, for example).  But we sometimes need to
    /// perform very low-level operations that requires the native
    /// representation.  Passing the file descriptor to a different process is
    /// one example.  This method is provided as a necessary evil for such
    //// limited purposes.
    ///
    /// This method never throws an exception.
    ///
    /// \return The native representation of the socket.  This is the socket
    ///         file descriptor for UNIX-like systems.
    virtual int getNative() const = 0;

    /// \brief Return the transport protocol of the socket.
    ///
    /// Currently, it returns \c IPPROTO_UDP for UDP sockets, and
    /// \c IPPROTO_TCP for TCP sockets.
    ///
    /// This method never throws an exception.
    ///
    /// \return \c IPPROTO_UDP for UDP sockets, \c IPPROTO_TCP for TCP sockets
    virtual int getProtocol() const = 0;

    /// \brief Is Open() synchronous?
    ///
    /// On a TCP socket, an "open" operation is a call to the socket's "open()"
    /// method followed by a connection to the remote system: it is an
    /// asynchronous operation.  On a UDP socket, it is just a call to "open()"
    /// and completes synchronously.
    ///
    /// For TCP, signalling of the completion of the operation is done by
    /// by calling the callback function in the normal way.  This could be done
    /// for UDP (by posting en event on the event queue); however, that will
    /// incur additional overhead in the most common case.  So we give the
    /// caller the choice for calling this open() method synchronously or
    /// asynchronously.
    ///
    /// Owing to the way that the stackless coroutines are implemented, we need
    /// to know _before_ executing the "open" function whether or not it is
    /// asynchronous.  So this method is called to provide that information.
    ///
    /// (The reason there is a need to know is because the call to open() passes
    /// in the state of the coroutine at the time the call is made.  On an
    /// asynchronous I/O, we need to set the state to point to the statement
    /// after the call to open() _before_ we pass the corouine to the open()
    /// call.  Unfortunately, the macros that set the state of the coroutine
    /// also yield control - which we don't want to do if the open is
    /// synchronous.  Hence we need to know before we make the call to open()
    /// whether that call will complete asynchronously.)
    virtual bool isOpenSynchronous() const = 0;

    /// \brief Open AsioSocket
    ///
    /// Opens the socket for asynchronous I/O.  The open will complete
    /// synchronously on UCP or asynchronously on TCP (in which case a callback
    /// will be queued).
    ///
    /// \param endpoint Pointer to the endpoint object.  This is ignored for
    ///        a UDP socket (the target is specified in the send call), but
    ///        should be of type TCPEndpoint for a TCP connection.
    /// \param callback I/O Completion callback, called when the operation has
    ///        completed, but only if the operation was asynchronous. (It is
    ///        ignored on a UDP socket.)
    virtual void open(const IOEndpoint* endpoint, C& callback) = 0;

    /// \brief Send Asynchronously
    ///
    /// This corresponds to async_send_to() for UDP sockets and async_send()
    /// for TCP.  In both cases an endpoint argument is supplied indicating the
    /// target of the send - this is ignored for TCP.
    ///
    /// \param data Data to send
    /// \param length Length of data to send
    /// \param endpoint Target of the send
    /// \param callback Callback object.
    virtual void asyncSend(const void* data, size_t length,
                           const IOEndpoint* endpoint, C& callback) = 0;

    /// \brief Receive Asynchronously
    ///
    /// This corresponds to async_receive_from() for UDP sockets and
    /// async_receive() for TCP.  In both cases, an endpoint argument is
    /// supplied to receive the source of the communication.  For TCP it will
    /// be filled in with details of the connection.
    ///
    /// \param data Buffer to receive incoming message
    /// \param length Length of the data buffer
    /// \param offset Offset into buffer where data is to be put
    /// \param endpoint Source of the communication
    /// \param callback Callback object
    virtual void asyncReceive(void* data, size_t length, size_t offset,
                              IOEndpoint* endpoint, C& callback) = 0;

    /// \brief Checks if the data received is complete.
    ///
    /// This applies to TCP receives, where the data is a byte stream and a
    /// receive is not guaranteed to receive the entire message.  DNS messages
    /// over TCP are prefixed by a two-byte count field.  This method takes the
    /// amount received so far and checks if the message is complete.
    ///
    /// For a UDP receive, all the data is received in one I/O, so this is
    /// effectively a no-op).
    ///
    /// \param data Data buffer containing data to date
    /// \param length Total amount of data in the buffer.
    ///
    /// \return true if the receive is complete, false if another receive is
    ///         needed.
    virtual bool receiveComplete(const void* data, size_t length) = 0;

    /// \brief Append Normalized Data
    ///
    /// When a UDP buffer is received, the entire buffer contains the data.
    /// When a TCP buffer is received, the first two bytes of the buffer hold
    /// a length count.  This method removes those bytes from the buffer.
    ///
    /// \param inbuf Input buffer.  This contains the data received over the
    ///        network connection.
    /// \param length Amount of data in the input buffer.  If TCP, this includes
    ///        the two-byte count field.
    /// \param outbuf Pointer to output buffer to which the data will be
    ///        appended.
    virtual void appendNormalizedData(const void* inbuf, size_t length,
        isc::dns::OutputBufferPtr outbuf) = 0;

    /// \brief Cancel I/O On AsioSocket
    virtual void cancel() = 0;

    /// \brief Close socket
    virtual void close() = 0;
};


#include "io_socket.h"

/// \brief The \c DummyAsioSocket class is a concrete derived class of
/// \c IOAsioSocket that is not associated with any real socket.
///
/// This main purpose of this class is tests, where it may be desirable to
/// instantiate an \c IOAsioSocket object without involving system resource
/// allocation such as real network sockets.
///
/// \param C Template parameter identifying type of the callback object.

template <typename C>
class DummyAsioSocket : public IOAsioSocket<C> {
private:
    DummyAsioSocket(const DummyAsioSocket<C>& source);
    DummyAsioSocket& operator=(const DummyAsioSocket<C>& source);
public:
    /// \brief Constructor from the protocol number.
    ///
    /// The protocol must validly identify a standard network protocol.
    /// For example, to specify TCP \c protocol must be \c IPPROTO_TCP.
    ///
    /// \param protocol The network protocol number for the socket.
    DummyAsioSocket(const int protocol) : protocol_(protocol) {}

    /// \brief A dummy derived method of \c IOAsioSocket::getNative().
    ///
    /// \return Always returns -1 as the object is not associated with a real
    /// (native) socket.
    virtual int getNative() const { return (-1); }

    /// \brief A dummy derived method of \c IOAsioSocket::getProtocol().
    ///
    /// \return Protocol socket was created with
    virtual int getProtocol() const { return (protocol_); }


    /// \brief Is socket opening synchronous?
    ///
    /// \return true - it is for this class.
    bool isOpenSynchronous() const {
        return true;
    }

    /// \brief Open AsioSocket
    ///
    /// A call that is a no-op on UDP sockets, this opens a connection to the
    /// system identified by the given endpoint.
    ///
    /// \param endpoint Unused
    /// \param callback Unused.
    ///false indicating that the operation completed synchronously.
    virtual bool open(const IOEndpoint*, C&) {
        return (false);
    }

    /// \brief Send Asynchronously
    ///
    /// Must be supplied as it is abstract in the base class.
    ///
    /// \param data Unused
    /// \param length Unused
    /// \param endpoint Unused
    /// \param callback Unused
    virtual void asyncSend(const void*, size_t, const IOEndpoint*, C&) {
    }

    /// \brief Receive Asynchronously
    ///
    /// Must be supplied as it is abstract in the base class.
    ///
    /// \param data Unused
    /// \param length Unused
    /// \param offset Unused
    /// \param endpoint Unused
    /// \param callback Unused
    virtual void asyncReceive(void* data, size_t, size_t, IOEndpoint*, C&) {
    }

    /// \brief Checks if the data received is complete.
    ///
    /// \param data Unused
    /// \param length Unused
    /// \param cumulative Unused
    ///
    /// \return Always true
    virtual bool receiveComplete(void*, size_t, size_t&) {
        return (true);
    }
    /// \brief Append Normalized Data
    ///
    /// \param inbuf Unused.
    /// \param length Unused.
    /// \param outbuf unused.
    virtual void appendNormalizedData(const void*, size_t,
                                      isc::dns::OutputBufferPtr)
    {
    }

    /// \brief Cancel I/O On AsioSocket
    ///
    /// Must be supplied as it is abstract in the base class.
    virtual void cancel() {
    }

    /// \brief Close socket
    ///
    /// Must be supplied as it is abstract in the base class.
    virtual void close() {
    }

private:
    const int protocol_;
};

} // namespace asiolink

#endif // __IO_ASIO_SOCKET_H
