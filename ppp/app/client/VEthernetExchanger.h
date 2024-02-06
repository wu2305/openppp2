#pragma once

#include <ppp/app/protocol/VirtualEthernetLinklayer.h>
#include <ppp/app/protocol/VirtualEthernetMappingPort.h>
#include <ppp/Int128.h>
#include <ppp/net/Ipep.h>
#include <ppp/net/IPEndPoint.h>
#include <ppp/threading/Timer.h>
#include <ppp/auxiliary/UriAuxiliary.h>

namespace ppp {
    namespace app {
        namespace client {
            class VEthernetNetworkSwitcher;
            class VEthernetDatagramPort;

            class VEthernetExchanger : public ppp::app::protocol::VirtualEthernetLinklayer {
                friend class                                                            VEthernetDatagramPort;

            public:
                typedef std::shared_ptr<VEthernetNetworkSwitcher>                       VEthernetNetworkSwitcherPtr;
                typedef ppp::app::protocol::VirtualEthernetInformation                  VirtualEthernetInformation;
                typedef ppp::auxiliary::UriAuxiliary                                    UriAuxiliary;
                typedef UriAuxiliary::ProtocolType                                      ProtocolType;
                typedef ppp::threading::Timer                                           Timer;
                typedef std::shared_ptr<Timer>                                          TimerPtr;
                typedef ppp::unordered_map<void*, TimerPtr>                             TimerTable;
                typedef std::shared_ptr<VEthernetDatagramPort>                          VEthernetDatagramPortPtr;

            private:
                typedef ppp::unordered_map<boost::asio::ip::udp::endpoint,
                    VEthernetDatagramPortPtr>                                           VEthernetDatagramPortTable;
                typedef ppp::app::protocol::VirtualEthernetMappingPort                  VirtualEthernetMappingPort;
                typedef std::shared_ptr<VirtualEthernetMappingPort>                     VirtualEthernetMappingPortPtr;
                typedef ppp::unordered_map<uint32_t, VirtualEthernetMappingPortPtr>     VirtualEthernetMappingPortTable;

            public:
                VEthernetExchanger(
                    const VEthernetNetworkSwitcherPtr&                                  switcher,
                    const AppConfigurationPtr&                                          configuration,
                    const ContextPtr&                                                   context,
                    const Int128&                                                       id) noexcept;
                virtual ~VEthernetExchanger() noexcept;

            public:
                typedef enum {
                    NetworkState_Connecting,
                    NetworkState_Established,
                    NetworkState_Reconnecting,
                }                                                                       NetworkState;
                NetworkState                                                            GetNetworkState() noexcept;
                virtual VEthernetNetworkSwitcherPtr                                     GetSwitcher() noexcept;
                virtual bool                                                            Open() noexcept;
                virtual void                                                            Dispose() noexcept;
                virtual std::shared_ptr<VirtualEthernetInformation>                     GetInformation() noexcept;
                virtual ITransmissionPtr                                                GetTransmission() noexcept;
                virtual ITransmissionPtr                                                ConnectTransmission(const ContextPtr& context, YieldContext& y) noexcept;

            public:
                virtual bool                                                            Nat(const void* packet, int packet_size) noexcept;
                virtual bool                                                            Echo(int ack_id) noexcept;
                virtual bool                                                            Echo(const void* packet, int packet_size) noexcept;
                virtual bool                                                            SendTo(const boost::asio::ip::udp::endpoint& sourceEP, const boost::asio::ip::udp::endpoint& destinationEP, const void* packet, int packet_size) noexcept;
                virtual bool                                                            Update(UInt64 now) noexcept;
                virtual bool                                                            GetRemoteEndPoint(YieldContext* y, ppp::string& hostname, ppp::string& address, ppp::string& path, int& port, ProtocolType& protocol_type, ppp::string& server, boost::asio::ip::tcp::endpoint& remoteEP) noexcept;

            protected:
                virtual bool                                                            OnLan(const ITransmissionPtr& transmission, uint32_t ip, uint32_t mask, YieldContext& y) noexcept override;
                virtual bool                                                            OnNat(const ITransmissionPtr& transmission, Byte* packet, int packet_length, YieldContext& y) noexcept override;
                virtual bool                                                            OnInformation(const ITransmissionPtr& transmission, const VirtualEthernetInformation& information, YieldContext& y) noexcept override;
                virtual bool                                                            OnPush(const ITransmissionPtr& transmission, int connection_id, Byte* packet, int packet_length, YieldContext& y) noexcept override;
                virtual bool                                                            OnConnect(const ITransmissionPtr& transmission, int connection_id, const boost::asio::ip::tcp::endpoint& destinationEP, YieldContext& y) noexcept override;
                virtual bool                                                            OnConnectOK(const ITransmissionPtr& transmission, int connection_id, Byte error_code, YieldContext& y) noexcept override;
                virtual bool                                                            OnDisconnect(const ITransmissionPtr& transmission, int connection_id, YieldContext& y) noexcept override;
                virtual bool                                                            OnEcho(const ITransmissionPtr& transmission, int ack_id, YieldContext& y) noexcept override;
                virtual bool                                                            OnEcho(const ITransmissionPtr& transmission, Byte* packet, int packet_length, YieldContext& y) noexcept override;
                virtual bool                                                            OnSendTo(const ITransmissionPtr& transmission, const boost::asio::ip::udp::endpoint& sourceEP, const boost::asio::ip::udp::endpoint& destinationEP, Byte* packet, int packet_length, YieldContext& y) noexcept override;

            protected:
                virtual VEthernetDatagramPortPtr                                        NewDatagramPort(const ITransmissionPtr& transmission, const boost::asio::ip::udp::endpoint& sourceEP) noexcept;
                virtual VEthernetDatagramPortPtr                                        GetDatagramPort(const boost::asio::ip::udp::endpoint& sourceEP) noexcept;
                virtual VEthernetDatagramPortPtr                                        ReleaseDatagramPort(const boost::asio::ip::udp::endpoint& sourceEP) noexcept;

            protected:
                virtual ITransmissionPtr                                                NewTransmission(
                    const ContextPtr&                                                   context,
                    const std::shared_ptr<boost::asio::ip::tcp::socket>&                socket,
                    ProtocolType                                                        protocol_type,
                    const ppp::string&                                                  host,
                    const ppp::string&                                                  path) noexcept;
                virtual ITransmissionPtr                                                OpenTransmission(const ContextPtr& context, YieldContext& y) noexcept;

            protected:
                virtual std::shared_ptr<boost::asio::ip::tcp::socket>                   NewAsynchronousSocket(const ContextPtr& context, const boost::asio::ip::tcp& protocol) noexcept;
                virtual bool                                                            Loopback(const ContextPtr& context, YieldContext& y) noexcept;
                virtual bool                                                            PacketInput(const ITransmissionPtr& transmission, Byte* p, int packet_length, YieldContext& y) noexcept;

            private:
                void                                                                    Finalize() noexcept;
                void                                                                    ExchangeToEstablishState() noexcept;
                void                                                                    ExchangeToConnectingState() noexcept;
                void                                                                    ExchangeToReconnectingState() noexcept;
                int                                                                     EchoLanToRemoteExchanger(const ITransmissionPtr& transmission, YieldContext& y) noexcept;
                bool                                                                    SendEchoKeepAlivePacket(UInt64 now, bool immediately) noexcept;
                bool                                                                    ReceiveFromDestination(const boost::asio::ip::udp::endpoint& sourceEP, const boost::asio::ip::udp::endpoint& destinationEP, Byte* packet, int packet_length) noexcept;
                VEthernetDatagramPortPtr                                                AddNewDatagramPort(const ITransmissionPtr& transmission, const boost::asio::ip::udp::endpoint& sourceEP) noexcept;

            private:
                template <typename TTransmission>
                typename std::enable_if<std::is_base_of<ITransmission, TTransmission>::value, std::shared_ptr<TTransmission>/**/>::type
                NewWebsocketTransmission(const ContextPtr& context, const std::shared_ptr<boost::asio::ip::tcp::socket>& socket, const ppp::string& host, const ppp::string& path) noexcept {
                    std::shared_ptr<ppp::configurations::AppConfiguration> configuration = GetConfiguration();
                    auto transmission = make_shared_object<TTransmission>(context, socket, configuration);
                    if (NULL == transmission) {
                        return NULL;
                    }
                    
                    if (host.size() > 0 && path.size() > 0) {
                        transmission->Host = host;
                        transmission->Path = path;
                    }
                    return transmission;
                }

            private:
                VirtualEthernetMappingPortPtr                                           GetMappingPort(bool in, bool tcp, int remote_port) noexcept;
                VirtualEthernetMappingPortPtr                                           NewMappingPort(bool in, bool tcp, int remote_port) noexcept;
                bool                                                                    RegisterMappingPort(ppp::configurations::AppConfiguration::MappingConfiguration& mapping) noexcept;
                void                                                                    UnregisterAllMappingPorts() noexcept;
                bool                                                                    RegisterAllMappingPorts() noexcept;

            private:
                virtual bool                                                            OnFrpSendTo(const ITransmissionPtr& transmission, bool in, int remote_port, const boost::asio::ip::udp::endpoint& sourceEP, Byte* packet, int packet_length, YieldContext& y) noexcept override;
                virtual bool                                                            OnFrpConnect(const ITransmissionPtr& transmission, int connection_id, bool in, int remote_port, YieldContext& y) noexcept override;
                virtual bool                                                            OnFrpDisconnect(const ITransmissionPtr& transmission, int connection_id, bool in, int remote_port) noexcept override;
                virtual bool                                                            OnFrpPush(const ITransmissionPtr& transmission, int connection_id, bool in, int remote_port, const void* packet, int packet_length) noexcept override;

            private:
                bool                                                                    disposed_;
                UInt64                                                                  sekap_last_;
                UInt64                                                                  sekap_next_;
                VEthernetNetworkSwitcherPtr                                             switcher_;
                std::shared_ptr<VirtualEthernetInformation>                             information_;
                VEthernetDatagramPortTable                                              datagrams_;
                ITransmissionPtr                                                        transmission_;
                std::atomic<NetworkState>                                               network_state_;
                VirtualEthernetMappingPortTable                                         mappings_;
            };
        }
    }
}