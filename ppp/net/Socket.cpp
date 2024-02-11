// https://www-numi.fnal.gov/offline_software/srt_public_context/WebDocs/Errors/unix_system_errors.html
// #define ENOENT           2      /* No such file or directory */
// #define EAGAIN          11      /* Try again */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <iostream>
#include <assert.h>

#include <sys/types.h>

#if defined(_WIN32)
#include <stdint.h>
#include <WinSock2.h>
#include <WS2tcpip.h>

#pragma comment(lib, "ws2_32.lib")
#else
#include <netdb.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/poll.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>
#endif

#include <fcntl.h>
#include <errno.h>

#include <ppp/net/Socket.h>
#include <ppp/net/IPEndPoint.h>
#include <ppp/threading/Executors.h>

#if defined(_MACOS)
#include <errno.h>
#elif defined(_LINUX)
#include <error.h>
#endif

// https://elixir.bootlin.com/linux/latest/source/include/uapi/linux/ip.h#L26
// https://man7.org/linux/man-pages/man7/ip.7.html
#if defined(_WIN32)
#define IPTOS_TOS_MASK      0x1E
#define IPTOS_TOS(tos)      ((tos) & IPTOS_TOS_MASK)
#define IPTOS_LOWDELAY      0x10
#define IPTOS_THROUGHPUT    0x08
#define IPTOS_RELIABILITY   0x04
#define IPTOS_MINCOST       0x02
#endif

namespace ppp {
    namespace net {
        static bool Socket_ConvertSockaddrToEndpoint(const struct sockaddr* addr, boost::asio::ip::address& address, boost::asio::ip::tcp::endpoint& endpoint) noexcept {
            const struct sockaddr_in* in4 = (const struct sockaddr_in*)addr;
            const struct sockaddr_in6* in6 = (const struct sockaddr_in6*)addr;
            if (addr->sa_family == AF_INET) {
                address = boost::asio::ip::address_v4(ntohl(in4->sin_addr.s_addr));
                endpoint = boost::asio::ip::tcp::endpoint(address, ntohs(in4->sin_port));
                return true;
            }
            else if (addr->sa_family == AF_INET6) {
                boost::asio::ip::address_v6::bytes_type bytes;
                memcpy(bytes.data(), &in6->sin6_addr.s6_addr, bytes.size());

                address = boost::asio::ip::address_v6(bytes);
                endpoint = boost::asio::ip::tcp::endpoint(address, ntohs(in6->sin6_port));
                return true;
            }
            else {
                return false;
            }
        }

        static bool Socket_GetPeerNameOrGetSocketName(boost::asio::ip::tcp::endpoint& endpoint, int fd, bool getpeername_or_getsockname) noexcept {
            int err = -1;
            union {
                struct sockaddr_in in4;
                struct sockaddr_in6 in6;
            } address;

            socklen_t address_size = sizeof(address);
            if (getpeername_or_getsockname) {
                err = ::getpeername(fd, (struct sockaddr*)&address, &address_size);
            }
            else {
                err = ::getsockname(fd, (struct sockaddr*)&address, &address_size);
            }

            if (err != 0) {
                return false;
            }

            boost::asio::ip::address boost_address = boost::asio::ip::address_v4::any();
            return Socket_ConvertSockaddrToEndpoint((struct sockaddr*)&address, boost_address, endpoint);
        }

        boost::asio::ip::tcp::endpoint Socket::GetLocalEndPoint(int fd) noexcept {
            boost::asio::ip::tcp::endpoint endpoint;
            Socket_GetPeerNameOrGetSocketName(endpoint, fd, false);
            return endpoint;
        }

        boost::asio::ip::tcp::endpoint Socket::GetRemoteEndPoint(int fd) noexcept {
            boost::asio::ip::tcp::endpoint endpoint;
            Socket_GetPeerNameOrGetSocketName(endpoint, fd, true);
            return endpoint;
        }

        bool Socket::Poll(int s, int milliSeconds, SelectMode mode) noexcept {
            int64_t microSeconds = milliSeconds;
            microSeconds *= 1000;
            return Socket::PolH(s, microSeconds, mode);
        }

        bool Socket::PolH(int s, int64_t microSeconds, SelectMode mode) noexcept {
            if (s == -1) {
                return false;
            }

#if defined(_WIN32)
            struct fd_set fdset;
            FD_ZERO(&fdset);
            FD_SET(s, &fdset);

            struct timeval tv;
            if (microSeconds < 0) {
                tv.tv_sec = INFINITY;
                tv.tv_usec = INFINITY;
            }
            else {
                tv.tv_sec = microSeconds / 1000000;
                tv.tv_usec = microSeconds;
            }

            int hr = -1;
            if (mode == SelectMode_SelectRead) {
                hr = select(s + 1, &fdset, NULL, NULL, &tv) > 0;
            }
            elif(mode == SelectMode_SelectWrite) {
                hr = select(s + 1, NULL, &fdset, NULL, &tv) > 0;
            }
            else {
                hr = select(s + 1, NULL, NULL, &fdset, &tv) > 0;
            }

            if (hr > 0) {
                return FD_ISSET(s, &fdset);
            }
#else
            struct pollfd fds[1];
            memset(fds, 0, sizeof(fds));

            int events = POLLERR;
            if (mode == SelectMode_SelectRead) {
                events = POLLIN;
            }
            elif(mode == SelectMode_SelectWrite) {
                events = POLLOUT;
            }
            else {
                events = POLLERR;
            }

            fds->fd = s;
            fds->events = events;

            int hr;
            if (microSeconds < 0) {
                int timeout_ = (int)INFINITY;
                hr = poll(fds, 1, timeout_);
            }
            else {
                int timeout_ = (int)(microSeconds / 1000);
                hr = poll(fds, 1, timeout_);
            }

            if (hr > 0) {
                if ((fds->revents & events) == events) {
                    return true;
                }
            }
#endif
            return false;
        }

        void Socket::Cancel(const boost::asio::ip::udp::socket& socket) noexcept {
            boost::asio::ip::udp::socket& s = const_cast<boost::asio::ip::udp::socket&>(socket);
            if (s.is_open()) {
                boost::system::error_code ec;
                try {
                    s.cancel(ec);
                }
                catch (const std::exception&) {}
            }
        }

        void Socket::Cancel(const boost::asio::ip::tcp::socket& socket) noexcept {
            boost::asio::ip::tcp::socket& s = const_cast<boost::asio::ip::tcp::socket&>(socket);
            if (s.is_open()) {
                boost::system::error_code ec;
                try {
                    s.cancel(ec);
                }
                catch (const std::exception&) {}
            }
        }

        void Socket::Cancel(const boost::asio::ip::tcp::acceptor& acceptor) noexcept {
            boost::asio::ip::tcp::acceptor& s = const_cast<boost::asio::ip::tcp::acceptor&>(acceptor);
            if (s.is_open()) {
                boost::system::error_code ec;
                try {
                    s.cancel(ec);
                }
                catch (const std::exception&) {}
            }
        }

        void Socket::Cancel(const boost::asio::ip::udp::resolver& resolver) noexcept {
            boost::asio::ip::udp::resolver& s = const_cast<boost::asio::ip::udp::resolver&>(resolver);
            try {
                s.cancel();
            }
            catch (const std::exception&) {}
        }

        void Socket::Cancel(const boost::asio::ip::tcp::resolver& resolver) noexcept {
            boost::asio::ip::tcp::resolver& s = const_cast<boost::asio::ip::tcp::resolver&>(resolver);
            try {
                s.cancel();
            }
            catch (const std::exception&) {}
        }

        void Socket::Cancel(const boost::asio::deadline_timer& deadline_timer) noexcept {
            boost::asio::deadline_timer& t = const_cast<boost::asio::deadline_timer&>(deadline_timer);
            boost::system::error_code ec;
            try {
                t.cancel(ec);
            }
            catch (const std::exception&) {}
        }

        void Socket::Cancel(const std::shared_ptr<boost::asio::ip::udp::socket>& socket) noexcept {
            if (NULL != socket) {
                Cancel(*socket);
            }
        }

        void Socket::Cancel(const std::shared_ptr<boost::asio::ip::tcp::socket>& socket) noexcept {
            if (NULL != socket) {
                Cancel(*socket);
            }
        }

        void Socket::Cancel(const std::shared_ptr<boost::asio::ip::tcp::acceptor>& acceptor) noexcept {
            if (NULL != acceptor) {
                Cancel(*acceptor);
            }
        }

        bool Socket::Closesocket(const std::shared_ptr<boost::asio::ip::udp::socket>& socket) noexcept {
            if (NULL != socket) {
                return Closesocket(*socket);
            }
            else {
                return false;
            }
        }

        bool Socket::Closesocket(const std::shared_ptr<boost::asio::ip::tcp::socket>& socket) noexcept {
            if (NULL != socket) {
                return Closesocket(*socket);
            }
            else {
                return false;
            }
        }

        bool Socket::Closesocket(const std::shared_ptr<boost::asio::ip::tcp::acceptor>& acceptor) noexcept {
            if (NULL != acceptor) {
                return Closesocket(*acceptor);
            }
            else {
                return false;
            }
        }

        void Socket::Shutdown(int fd) noexcept {
            if (fd != -1) {
                int how;
#if defined(_WIN32)
                how = SD_SEND;
#else
                how = SHUT_WR;
#endif

                ::shutdown(fd, how);
            }
        }

        void Socket::Closesocket(int fd) noexcept {
            if (fd != -1) {
#if defined(_WIN32)
                ::closesocket(fd);
#else   
                ::close(fd);
#endif
            }
        }

        int Socket::GetDefaultTTL() noexcept {
            static constexpr int DFL_TTL = 64;

            int ttl = DFL_TTL;
            int fd = (int)socket(AF_INET, SOCK_DGRAM, 0);
            if (fd != -1) {
#if defined(_MACOS)
                socklen_t len = sizeof(ttl);
                if (getsockopt(fd, IPPROTO_IP, IP_TTL, &ttl, &len) < 0) {
                    int mib[] = { CTL_NET, IPPROTO_IP, IPCTL_DEFTTL };
                    if (sysctl(mib, 3, &ttl, (size_t*)&len, NULL, 0) < 0 || ttl < 1) {
                        ttl = DFL_TTL;
                    }
                }
                elif(ttl < 1) {
                    ttl = DFL_TTL;
                }
#else
                socklen_t len = sizeof(ttl);
                if (getsockopt(fd, SOL_IP, IP_TTL, (char*)&ttl, &len) < 0 || ttl < 1) {
                    ttl = DFL_TTL;
                }
#endif

#if defined(_WIN32)
                ::closesocket((SOCKET)fd);
#else
                ::close(fd);
#endif
            }

            return std::min<int>(ttl, UINT8_MAX);
        }

        bool Socket::SetTypeOfService(int fd, int tos) noexcept {
            if (fd == -1) {
                return false;
            }

            if (tos < 0) {
                tos = 0x68; // FLASH
            }

            Byte b = tos; /* IPTOS_LOWDELAY */
#if defined(_MACOS)
            return ::setsockopt(fd, IPPROTO_IP, IP_TOS, (char*)&b, sizeof(b)) == 0;
#else
            return ::setsockopt(fd, SOL_IP, IP_TOS, (char*)&b, sizeof(b)) == 0;
#endif
        }

        bool Socket::SetSignalPipeline(int fd, bool sigpipe) noexcept {
            int err = 0;
            if (fd == -1) {
                return false;
            }

#if defined(SO_NOSIGPIPE)
            int opt = sigpipe ? 0 : 1;
            err = ::setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, (char*)&opt, sizeof(opt));
#endif
            return err == 0;
        }

        bool Socket::ReuseSocketAddress(int fd, bool reuse) noexcept {
            if (fd == -1) {
                return false;
            }

            int flag = reuse ? 1 : 0;
            return ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (char*)&flag, sizeof(flag)) == 0;
        }

        // If the socket function is not supported by the QEMU user mode VM, 
        // The does not apply.  
        // Otherwise, the QEMU user mode VM frantically reports error logs.
        static class SOCKET_RESTRICTIONS final {
        public:
            bool IP_TOS_ON;
            bool IPV6_TCLASS_ON;

        public:
            SOCKET_RESTRICTIONS() noexcept 
                : IP_TOS_ON(true)
                , IPV6_TCLASS_ON(true) {
#if defined(_LINUX)
                int fd = (int)socket(AF_INET, SOCK_STREAM, 0);
                if (fd != -1) {
                    ValidV4(fd);
                    Socket::Closesocket(fd);
                }

                fd = (int)socket(AF_INET6, SOCK_STREAM, 0);
                if (fd != -1) {
                    ValidV6(fd);
                    Socket::Closesocket(fd);
                }

                ClearConsoleOutputCharacter();
#endif
            }

#if defined(_LINUX)
        private:
            bool ValidV4(int sockfd) noexcept {
                uint8_t tos = IPTOS_LOWDELAY;
                int err = ::setsockopt(sockfd, SOL_IP, IP_TOS, (char*)&tos, sizeof(tos));
                if (err > -1) {
                    return true;
                }

                IP_TOS_ON = false;
                return false;
            }

            bool ValidV6(int sockfd) noexcept {
                uint8_t tos = IPTOS_LOWDELAY;
#if IPV6_TCLASS
                int err = ::setsockopt(sockfd, IPPROTO_IPV6, IPV6_TCLASS, (char*)&tos, sizeof(tos));
                if (err > -1) {
                    return true;
                }
#endif

                IPV6_TCLASS_ON = false;
                return false;
            }
#endif  
        } SOCKET_RESTRICTIONS_;
        
        void Socket::AdjustDefaultSocketOptional(int sockfd, bool in4) noexcept {
            if (sockfd != -1) {
                uint8_t tos = IPTOS_LOWDELAY;
                if (in4) {
                    if (SOCKET_RESTRICTIONS_.IP_TOS_ON) {
#if defined(_MACOS)
                        ::setsockopt(sockfd, IPPROTO_IP, IP_TOS, (char*)&tos, sizeof(tos));
#else
                        ::setsockopt(sockfd, SOL_IP, IP_TOS, (char*)&tos, sizeof(tos));
#endif
                    }

#if IP_DONTFRAGMENT
                    int dont_frag = IP_PMTUDISC_DO;
                    ::setsockopt(sockfd, IPPROTO_IP, IP_DONTFRAGMENT, (char*)&dont_frag, sizeof(dont_frag));
#elif IP_PMTUDISC_WANT
                    int dont_frag = IP_PMTUDISC_WANT;
                    ::setsockopt(sockfd, IPPROTO_IP, IP_MTU_DISCOVER, &dont_frag, sizeof(dont_frag));
#endif
                }
                else {
                    // linux-user: Add missing IP_TOS, IPV6_TCLASS and IPV6_RECVTCLASS sockopts
                    // QEMU:
                    // https://patchwork.kernel.org/project/qemu-devel/patch/20170311195906.GA13187@ls3530.fritz.box/
#if IPV6_TCLASS
                    if (SOCKET_RESTRICTIONS_.IPV6_TCLASS_ON) {
                        ::setsockopt(sockfd, IPPROTO_IPV6, IPV6_TCLASS, (char*)&tos, sizeof(tos)); /* SOL_IPV6 */
                    }
#endif

#if IPV6_MTU_DISCOVER && IPV6_PMTUDISC_WANT
                    int dont_frag = IPV6_PMTUDISC_WANT;
                    ::setsockopt(sockfd, IPPROTO_IPV6, IPV6_MTU_DISCOVER, &dont_frag, sizeof(dont_frag));
#endif
                }

#if defined(SO_NOSIGPIPE)
                int no_sigpipe = 1;
                ::setsockopt(sockfd, SOL_SOCKET, SO_NOSIGPIPE, &no_sigpipe, sizeof(no_sigpipe));
#endif
            }
        }

        // https://source.android.google.cn/devices/tech/debug/native-crash?hl=zh-cn
        // https://android.googlesource.com/platform/bionic/+/master/docs/fdsan.md
        bool Socket::Closesocket(const boost::asio::ip::tcp::socket& socket) noexcept {
            boost::asio::ip::tcp::socket& s = const_cast<boost::asio::ip::tcp::socket&>(socket);
            if (s.is_open()) {
                boost::system::error_code ec;
                try {
                    s.shutdown(boost::asio::ip::tcp::socket::shutdown_send, ec);
                }
                catch (const std::exception&) {}

                try {
                    s.close(ec);
                    return ec == boost::system::errc::success;
                }
                catch (const std::exception&) {}
            }
            return false;
        }

        bool Socket::Closesocket(const boost::asio::ip::tcp::acceptor& acceptor) noexcept {
            boost::asio::ip::tcp::acceptor& s = const_cast<boost::asio::ip::tcp::acceptor&>(acceptor);
            if (s.is_open()) {
                boost::system::error_code ec;
                try {
                    s.close(ec);
                    return ec == boost::system::errc::success;
                }
                catch (const std::exception&) {}
            }
            return false;
        }

        bool Socket::Closesocket(const boost::asio::ip::udp::socket& socket) noexcept {
            boost::asio::ip::udp::socket& s = const_cast<boost::asio::ip::udp::socket&>(socket);
            if (s.is_open()) {
                boost::system::error_code ec;
                try {
                    s.close(ec);
                    return ec == boost::system::errc::success;
                }
                catch (const std::exception&) {}
            }
            return false;
        }

        int Socket::GetHandle(const boost::asio::ip::tcp::socket& socket) noexcept {
            boost::asio::ip::tcp::socket& s = const_cast<boost::asio::ip::tcp::socket&>(socket);
            if (!socket.is_open()) {
                return -1;
            }

            try {
                int ndfs = s.native_handle();
                return ndfs;
            }
            catch (const std::exception&) {
                return -1;
            }
        }

        int Socket::GetHandle(const boost::asio::ip::tcp::acceptor& acceptor) noexcept {
            boost::asio::ip::tcp::acceptor& s = const_cast<boost::asio::ip::tcp::acceptor&>(acceptor);
            if (!s.is_open()) {
                return -1;
            }

            try {
                int ndfs = s.native_handle();
                return ndfs;
            }
            catch (const std::exception&) {
                return -1;
            }
        }

        int Socket::GetHandle(const boost::asio::ip::udp::socket& socket) noexcept {
            boost::asio::ip::udp::socket& s = const_cast<boost::asio::ip::udp::socket&>(socket);
            if (!s.is_open()) {
                return -1;
            }

            try {
                int ndfs = s.native_handle();
                return ndfs;
            }
            catch (const std::exception&) {
                return -1;
            }
        }

        bool Socket::AcceptLoopbackAsync(
            const AsioTcpAcceptor&                                  acceptor,
            const std::shared_ptr<AcceptLoopbackCallback>&          callback,
            const std::shared_ptr<GetContextCallback>&              context) noexcept {
            if (!acceptor || !acceptor->is_open()) {
                return false;
            }

            if (!callback) {
                Closesocket(acceptor);
                return false;
            }

            const AsioTcpAcceptor                         acceptor_ = acceptor;
            const std::shared_ptr<AcceptLoopbackCallback> callback_ = callback;
            const std::shared_ptr<AcceptLoopbackCallback> cb = make_shared_object<AcceptLoopbackCallback>(
                [acceptor_, callback_](const AsioContext& context, const AsioTcpSocket& socket) noexcept {
                    return (*callback_)(context, socket);
                });
            if (!cb) {
                Closesocket(acceptor);
                return false;
            }

            bool opened = Socket::AcceptLoopbackAsync(*acceptor, cb, context);
            if (opened) {
                return true;
            }
            else {
                Closesocket(acceptor);
                return false;
            }
        }

        bool Socket::AcceptLoopbackAsync(
            const boost::asio::ip::tcp::acceptor&                   acceptor,
            const std::shared_ptr<AcceptLoopbackCallback>&          callback,
            const std::shared_ptr<GetContextCallback>&              context) noexcept {
            if (!acceptor.is_open()) {
                return false;
            }

            if (!callback) {
                Closesocket(acceptor);
                return false;
            }

            const AsioContext context_ = context ? (*context)() : ppp::threading::Executors::GetExecutor();
            if (!context_) {
                Closesocket(acceptor);
                return false;
            }

            boost::asio::ip::tcp::acceptor* const         acceptor_ = addressof(acceptor);
            const std::shared_ptr<AcceptLoopbackCallback> accept_ = callback;
            const AsioTcpSocket                           socket_ = make_shared_object<boost::asio::ip::tcp::socket>(*context_);
            if (NULL == socket_) {
                Closesocket(acceptor);
                return false;
            }

            boost::asio::post(acceptor_->get_executor(),
                [context_, acceptor_, accept_, socket_]() noexcept {
                    acceptor_->async_accept(*socket_,
                        [context_, acceptor_, accept_, socket_](boost::system::error_code ec) noexcept {
                            if (ec == boost::system::errc::operation_canceled) {
                                return;
                            }

                            bool success = false;
                            do { /* boost::system::errc::connection_aborted */
                                if (ec) { /* ECONNABORTED */
                                    break;
                                }

                                boost::asio::ip::tcp::endpoint remoteEP = socket_->local_endpoint(ec);
                                if (ec) {
                                    break;
                                }

                                int handle_ = socket_->native_handle();
                                Socket::AdjustDefaultSocketOptional(handle_, remoteEP.address().is_v4());
                                Socket::SetTypeOfService(handle_);
                                Socket::SetSignalPipeline(handle_, false);
                                Socket::ReuseSocketAddress(handle_, true);

                                /* Accept Socket?? */
                                success = (*accept_)(context_, socket_);
                            } while (false);

                            // The accepted Socket should be closed if it is rejected or fails to process,
                            // But it should not be executed on the Context worker thread that does not belong to it. 
                            // It needs to be delegated to the past processing for better security and programming paradigm.
                            if (!success) {
                                context_->post(
                                    [context_, socket_]() noexcept {
                                        Closesocket(socket_);
                                    });
                            }

                            // Unable to continue to initiate the next asynchronous task accepting a Socket from an arbitrary 
                            // Network client needs to be delegated to the receiver for close processing on the context.
                            AcceptLoopbackAsync(*acceptor_, forward0f(accept_));
                        });
                });
            return true;
        }

        bool Socket::OpenAcceptor(
            const boost::asio::ip::tcp::acceptor&                   acceptor,
            const boost::asio::ip::address&                         listenIP,
            int                                                     listenPort,
            int                                                     backlog,
            bool                                                    fastOpen,
            bool                                                    noDelay) noexcept {
            typedef ppp::net::IPEndPoint IPEndPoint;

            if (listenPort < IPEndPoint::MinPort || listenPort > IPEndPoint::MaxPort) {
                listenPort = IPEndPoint::MinPort;
            }

            boost::asio::ip::address address_ = listenIP;
            if (!address_.is_unspecified()) {
                if (IPEndPoint::IsInvalid(address_)) {
                    address_ = boost::asio::ip::address_v6::any();
                }
            }

            boost::asio::ip::tcp::acceptor& acceptor_ = const_cast<boost::asio::ip::tcp::acceptor&>(acceptor);
            if (acceptor_.is_open()) {
                return false;
            }

            boost::system::error_code ec;
            if (address_.is_v4()) {
                acceptor_.open(boost::asio::ip::tcp::v4(), ec);
            }
            else {
                acceptor_.open(boost::asio::ip::tcp::v6(), ec);
            }

            if (ec) {
                return false;
            }

            int handle = acceptor_.native_handle();
            ppp::net::Socket::AdjustDefaultSocketOptional(handle, address_.is_v4());
            ppp::net::Socket::SetTypeOfService(handle);
            ppp::net::Socket::SetSignalPipeline(handle, false);
            ppp::net::Socket::ReuseSocketAddress(handle, true);

            acceptor_.set_option(boost::asio::ip::tcp::acceptor::reuse_address(true), ec);
            if (ec) {
                return false;
            }

            acceptor_.set_option(boost::asio::ip::tcp::no_delay(noDelay), ec);
            acceptor_.set_option(boost::asio::detail::socket_option::boolean<IPPROTO_TCP, TCP_FASTOPEN>(fastOpen), ec);

            acceptor_.bind(boost::asio::ip::tcp::endpoint(address_, listenPort), ec);
            if (ec) {
                if (listenPort != IPEndPoint::MinPort) {
                    acceptor_.bind(boost::asio::ip::tcp::endpoint(address_, IPEndPoint::MinPort), ec);
                    if (ec) {
                        return false;
                    }
                }
            }

            if (backlog < 1) {
                backlog = 511;
            }

            acceptor_.listen(backlog, ec);
            if (ec) {
                return false;
            }
            return true;
        }

        bool Socket::OpenSocket(
            const boost::asio::ip::udp::socket&                     socket,
            const boost::asio::ip::address&                         listenIP,
            int                                                     listenPort) noexcept {
            typedef ppp::net::IPEndPoint IPEndPoint;

            if (listenPort < IPEndPoint::MinPort || listenPort > IPEndPoint::MaxPort) {
                listenPort = IPEndPoint::MinPort;
            }

            boost::asio::ip::address address_ = listenIP;
            if (!address_.is_unspecified()) {
                if (IPEndPoint::IsInvalid(address_)) {
                    address_ = boost::asio::ip::address_v6::any();
                }
            }

            boost::asio::ip::udp::socket& socket_ = const_cast<boost::asio::ip::udp::socket&>(socket);
            if (socket_.is_open()) {
                return false;
            }

            boost::system::error_code ec;
            if (address_.is_v4()) {
                socket_.open(boost::asio::ip::udp::v4(), ec);
            }
            else {
                socket_.open(boost::asio::ip::udp::v6(), ec);
            }

            if (ec) {
                return false;
            }

            int handle = socket_.native_handle();
            ppp::net::Socket::AdjustDefaultSocketOptional(handle, address_.is_v4());
            ppp::net::Socket::SetTypeOfService(handle);
            ppp::net::Socket::SetSignalPipeline(handle, false);
            ppp::net::Socket::ReuseSocketAddress(handle, true);

            socket_.set_option(boost::asio::ip::udp::socket::reuse_address(true), ec);
            if (ec) {
                return false;
            }

            socket_.bind(boost::asio::ip::udp::endpoint(address_, listenPort), ec);
            if (ec) {
                if (listenPort != IPEndPoint::MinPort) {
                    socket_.bind(boost::asio::ip::udp::endpoint(address_, IPEndPoint::MinPort), ec);
                    if (ec) {
                        return false;
                    }
                }
            }
            return true;
        }

        void Socket::AdjustSocketOptional(const boost::asio::ip::tcp::socket& socket, bool in4, bool fastOpen, bool noDealy) noexcept {
            boost::asio::ip::tcp::socket& s = const_cast<boost::asio::ip::tcp::socket&>(socket);
            if (s.is_open()) {
                int handle = s.native_handle();
                ppp::net::Socket::AdjustDefaultSocketOptional(handle, in4);
                ppp::net::Socket::SetTypeOfService(handle);
                ppp::net::Socket::SetSignalPipeline(handle, false);
                ppp::net::Socket::ReuseSocketAddress(handle, true);

                boost::system::error_code ec;
                s.set_option(boost::asio::ip::tcp::no_delay(noDealy), ec);
                s.set_option(boost::asio::detail::socket_option::boolean<IPPROTO_TCP, TCP_FASTOPEN>(fastOpen), ec);
            }
        }

        void Socket::AdjustSocketOptional(const boost::asio::ip::udp::socket& socket, bool in4) noexcept {
            boost::asio::ip::udp::socket& s = const_cast<boost::asio::ip::udp::socket&>(socket);
            if (s.is_open()) {
                int handle = s.native_handle();
                ppp::net::Socket::AdjustDefaultSocketOptional(handle, in4);
                ppp::net::Socket::SetTypeOfService(handle);
                ppp::net::Socket::SetSignalPipeline(handle, false);
                ppp::net::Socket::ReuseSocketAddress(handle, true);
            }
        }

        bool Socket::SetNonblocking(int fd, bool nonblocking) noexcept {
            if (fd == -1) {
                return false;
            }

#if defined(_WIN32)
            u_long flags = nonblocking ? 1 : 0;
            return ioctlsocket(fd, FIONBIO, &flags) == 0;
#else
            int flags = fcntl(fd, F_GETFD, 0);
            if (flags == -1) {
                return false;
            }

            if (nonblocking) {
                flags |= O_NONBLOCK;
            }
            else {
                flags &= ~O_NONBLOCK;
            }
            return fcntl(fd, F_SETFL, flags) == 0;
#endif
        }

        bool Socket::Closestream(const std::shared_ptr<boost::asio::posix::stream_descriptor>& stream) noexcept {
            if (NULL == stream) {
                return false;
            }

            boost::system::error_code ec;
            if (stream->is_open()) {
                try {
                    stream->cancel(ec);
                }
                catch (const std::exception&) {}
            }

            try {
                stream->close(ec);
                if (ec == boost::system::errc::success) {
                    return true;
                }
            }
            catch (const std::exception&) {}
            return false;
        }
    }
}