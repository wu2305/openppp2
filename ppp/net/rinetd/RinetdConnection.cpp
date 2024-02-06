#include <ppp/net/rinetd/RinetdConnection.h>
#include <ppp/net/Socket.h>
#include <ppp/net/IPEndPoint.h>
#include <ppp/threading/Executors.h>
#include <ppp/threading/BufferswapAllocator.h>

namespace ppp {
    namespace net {
        namespace rinetd {
            RinetdConnection::RinetdConnection(const std::shared_ptr<ppp::configurations::AppConfiguration>& configuration, const std::shared_ptr<boost::asio::io_context>& context, const std::shared_ptr<boost::asio::ip::tcp::socket>& local_socket) noexcept
                : disposed_(false)
                , timeout_(0)
                , context_(context)
                , local_socket_(local_socket)
                , configuration_(configuration) {

            }

            RinetdConnection::~RinetdConnection() noexcept {
                Finalize();
            }

            std::shared_ptr<boost::asio::io_context> RinetdConnection::GetContext() noexcept {
                return context_;
            }

            std::shared_ptr<boost::asio::ip::tcp::socket> RinetdConnection::GetLocalSocket() noexcept {
                return local_socket_;
            }

            std::shared_ptr<boost::asio::ip::tcp::socket> RinetdConnection::GetRemoteSocket() noexcept {
                return remote_socket_;
            }

            std::shared_ptr<ppp::configurations::AppConfiguration> RinetdConnection::GetConfiguration() noexcept {
                return configuration_;
            }

            void RinetdConnection::Dispose() noexcept {
                auto self = shared_from_this();
                std::shared_ptr<boost::asio::io_context> context = GetContext();
                context->post(
                    [self, this]() noexcept {
                        Finalize();
                    });
            }

            void RinetdConnection::Update() noexcept {
                if (remote_buffer_) {
                    timeout_ = ppp::threading::Executors::GetTickCount() + (UInt64)configuration_->tcp.inactive.timeout * 1000;
                }
                else {
                    timeout_ = ppp::threading::Executors::GetTickCount() + (UInt64)configuration_->tcp.connect.timeout * 1000;
                }
            }

            std::shared_ptr<Byte> RinetdConnection::GetLocalBuffer() noexcept {
                return local_buffer_;
            }

            std::shared_ptr<Byte> RinetdConnection::GetRemoteBuffer() noexcept {
                return remote_buffer_;
            }

            void RinetdConnection::Finalize() noexcept {
                exchangeof(disposed_, true); {
                    ppp::net::Socket::Closesocket(local_socket_);
                    ppp::net::Socket::Closesocket(remote_socket_);
                }
            }
 
            bool RinetdConnection::Run(const boost::asio::ip::tcp::endpoint& remoteEP) noexcept {
                if (disposed_) {
                    return false;
                }

                if (remote_socket_) {
                    return false;
                }

                boost::asio::ip::address remoteIP = remoteEP.address();
                if (remoteIP.is_unspecified()) {
                    return false;
                }

                if (remoteIP.is_multicast()) {
                    return false;
                }

                if (ppp::net::IPEndPoint::IsInvalid(remoteIP)) {
                    return false;
                }

                int remotePort = remoteEP.port();
                if (remotePort <= ppp::net::IPEndPoint::MinPort || remotePort > ppp::net::IPEndPoint::MaxPort) {
                    return false;
                }

                std::shared_ptr<boost::asio::ip::tcp::socket> socket = make_shared_object<boost::asio::ip::tcp::socket>(*context_);
                remote_socket_= socket;
                
                if (NULL == socket) {
                    return false;
                }

                boost::system::error_code ec;
                socket->open(remoteEP.protocol(), ec);
                if (ec) {
                    return false;
                }

#if defined(_LINUX)
                // If IPV4 is not a loop IP address, it needs to be linked to a physical network adapter. 
                // IPV6 does not need to be linked, because VPN is IPV4, 
                // And IPV6 does not affect the physical layer network communication of the VPN.
                if (remoteIP.is_v4() && !remoteIP.is_loopback()) {
                    if (auto protector_network = ProtectorNetwork; NULL != protector_network) {
                        if (!protector_network->Protect(socket->native_handle())) {
                            return false;
                        }
                    }
                }
#endif

                std::shared_ptr<ppp::configurations::AppConfiguration> configuration = GetConfiguration();
                ppp::net::Socket::AdjustSocketOptional(*socket, remoteIP.is_v4(), configuration->tcp.fast_open, configuration->tcp.turbo);

                std::shared_ptr<RinetdConnection> self = shared_from_this();
                socket->async_connect(remoteEP,
                    [self, this, socket, configuration](boost::system::error_code ec) noexcept {
                        bool ok = ec == boost::system::errc::success;
                        while (ok) {
                            ok = false;
                            if (disposed_) {
                                break;
                            }

                            local_buffer_ = ppp::threading::BufferswapAllocator::MakeByteArray(configuration->GetBufferAllocator(), PPP_BUFFER_SIZE);
                            if (NULL == local_buffer_) {
                                break;
                            }

                            remote_buffer_ = ppp::threading::BufferswapAllocator::MakeByteArray(configuration->GetBufferAllocator(), PPP_BUFFER_SIZE);
                            if (NULL == remote_buffer_) {
                                break;
                            }

                            ok = ForwardXToY(local_socket_.get(), remote_socket_.get(), local_buffer_.get()) && ForwardXToY(remote_socket_.get(), local_socket_.get(), remote_buffer_.get());
                            if (ok) {
                                Update();
                            }
                            break;
                        }

                        if (!ok) {
                            Dispose();
                        }
                    });

                Update();
                return true;
            }

            bool RinetdConnection::ForwardXToY(boost::asio::ip::tcp::socket* socket, boost::asio::ip::tcp::socket* to, Byte* buffer) noexcept {
                if (disposed_) {
                    return false;
                }

                bool opened = socket->is_open();
                if (!opened) {
                    return false;
                }

                std::shared_ptr<RinetdConnection> self = shared_from_this();
                socket->async_receive(boost::asio::buffer(buffer, PPP_BUFFER_SIZE),
                    [self, this, socket, to, buffer](const boost::system::error_code& ec, uint32_t sz) noexcept {
                        int bytes_transferred = std::max<int>(-1, ec ? -1 : sz);
                        if (bytes_transferred < 1) {
                            Dispose();
                            return;
                        }

                        boost::asio::async_write(*to, boost::asio::buffer(buffer, bytes_transferred),
                            [self, this, socket, to, buffer](const boost::system::error_code& ec, uint32_t sz) noexcept {
                                bool ok = ec == boost::system::errc::success;
                                if (ok) {
                                    ok = ForwardXToY(socket, to, buffer);
                                }

                                if (ok) {
                                    Update();
                                }
                                else {
                                    Dispose();
                                }
                            });
                    });
                return true;
            }
        }
    }
}