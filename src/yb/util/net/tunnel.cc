// Copyright (c) YugaByte, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
// in compliance with the License.  You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied.  See the License for the specific language governing permissions and limitations
// under the License.
//

#include "yb/util/net/tunnel.h"

#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/write.hpp>
#include <boost/optional.hpp>

#include "yb/util/logging.h"
#include "yb/util/size_literals.h"
#include "yb/util/status.h"
#include "yb/util/status_format.h"

using namespace std::placeholders;

namespace yb {

class TunnelConnection;

typedef std::shared_ptr<class TunnelConnection> TunnelConnectionPtr;

struct SemiTunnel {
  boost::asio::ip::tcp::socket* input;
  boost::asio::ip::tcp::socket* output;
  std::vector<char>* buffer;
  TunnelConnectionPtr self;
};

class TunnelConnection : public std::enable_shared_from_this<TunnelConnection> {
 public:
  explicit TunnelConnection(IoService* io_service, boost::asio::ip::tcp::socket* socket)
      : inbound_socket_(std::move(*socket)), outbound_socket_(*io_service), strand_(*io_service) {
  }

  void Start(const Endpoint& dest) {
    boost::system::error_code ec;
    auto remote = inbound_socket_.remote_endpoint(ec);
    auto inbound = inbound_socket_.local_endpoint(ec);
    log_prefix_ = Format("$0 => $1 => $2: ", remote, inbound, dest);
    outbound_socket_.async_connect(
        dest,
        strand_.wrap(std::bind(&TunnelConnection::HandleConnect, this, _1, shared_from_this())));
  }

  void Shutdown() {
    strand_.dispatch([this, shared_self = shared_from_this()] {
      boost::system::error_code ec;
      inbound_socket_.shutdown(boost::asio::ip::tcp::socket::shutdown_type::shutdown_both, ec);
      LOG_IF_WITH_PREFIX(INFO, ec) << "Shutdown failed: " << ec.message();
      outbound_socket_.shutdown(boost::asio::ip::tcp::socket::shutdown_type::shutdown_both, ec);
      LOG_IF_WITH_PREFIX(INFO, ec) << "Shutdown failed: " << ec.message();
    });
  }

 private:
  void HandleConnect(const boost::system::error_code& ec, const TunnelConnectionPtr& self) {
    if (ec) {
      LOG_WITH_PREFIX(WARNING) << "Connect failed: " << ec.message();
      return;
    }

    if (VLOG_IS_ON(2)) {
      boost::system::error_code endpoint_ec;
      VLOG_WITH_PREFIX(2) << "Connected: " << outbound_socket_.local_endpoint(endpoint_ec);
    }

    in2out_buffer_.resize(4_KB);
    out2in_buffer_.resize(4_KB);
    StartRead({&inbound_socket_, &outbound_socket_, &in2out_buffer_, self});
    StartRead({&outbound_socket_, &inbound_socket_, &out2in_buffer_, self});
  }

  void StartRead(const SemiTunnel& semi_tunnel) {
    semi_tunnel.input->async_read_some(
        boost::asio::buffer(*semi_tunnel.buffer),
        strand_.wrap(std::bind(&TunnelConnection::HandleRead, this, _1, _2, semi_tunnel)));
  }

  void HandleRead(const boost::system::error_code& ec, size_t transferred,
                  const SemiTunnel& semi_tunnel) {
    if (ec) {
      VLOG_WITH_PREFIX(1) << "Read failed: " << ec.message();
      return;
    }

    async_write(
        *semi_tunnel.output, boost::asio::buffer(semi_tunnel.buffer->data(), transferred),
        strand_.wrap(std::bind(&TunnelConnection::HandleWrite, this, _1, _2, semi_tunnel)));
  }

  void HandleWrite(const boost::system::error_code& ec, size_t transferred,
                   const SemiTunnel& semi_tunnel) {
    if (ec) {
      VLOG_WITH_PREFIX(1) << "Write failed: " << ec.message();
      return;
    }

    StartRead(semi_tunnel);
  }

  const std::string& LogPrefix() const {
    return log_prefix_;
  }

  boost::asio::ip::tcp::socket inbound_socket_;
  boost::asio::ip::tcp::socket outbound_socket_;
  boost::asio::io_context::strand strand_;
  std::vector<char> in2out_buffer_;
  std::vector<char> out2in_buffer_;
  std::string log_prefix_;
};

class Tunnel::Impl {
 public:
  explicit Impl(boost::asio::io_context* io_context)
      : io_context_(*io_context), strand_(*io_context) {}

  ~Impl() {
    LOG_IF(DFATAL, !closing_.load(std::memory_order_acquire))
        << "Tunnel shutdown has not been started";
  }

  Status Start(const Endpoint& local, const Endpoint& remote,
               AddressChecker address_checker) {
    auto acceptor = std::make_shared<boost::asio::ip::tcp::acceptor>(io_context_);
    boost::system::error_code ec;

    LOG(INFO) << "Starting tunnel: " << local << " => " << remote;

    acceptor->open(local.protocol(), ec);
    if (ec) {
      return STATUS_FORMAT(NetworkError, "Open failed: $0", ec.message());
    }
    acceptor->set_option(boost::asio::socket_base::reuse_address(true), ec);
    if (ec) {
      return STATUS_FORMAT(NetworkError, "Reuse address failed: $0", ec.message());
    }
    acceptor->bind(local, ec);
    if (ec) {
      return STATUS_FORMAT(NetworkError, "Bind failed: $0", ec.message());
    }
    acceptor->listen(boost::asio::ip::tcp::socket::max_listen_connections, ec);
    if (ec) {
      return STATUS_FORMAT(NetworkError, "Listen failed: $0", ec.message());
    }
    strand_.dispatch([
        this, acceptor, local, remote, address_checker]() {
      local_ = local;
      remote_ = remote;
      address_checker_ = address_checker;
      acceptor_.emplace(std::move(*acceptor));
      StartAccept();
    });
    return Status::OK();
  }

  void Shutdown() {
    closing_.store(true, std::memory_order_release);
    strand_.dispatch([this] {
      LOG(INFO) << "Shutdown tunnel: " << local_ << " => " << remote_;
      if (acceptor_) {
        boost::system::error_code ec;
        acceptor_->cancel(ec);
        LOG_IF(WARNING, ec) << "Cancel failed: " << ec.message();
        acceptor_->close(ec);
        LOG_IF(WARNING, ec) << "Close failed: " << ec.message();
      }

      for (auto& connection : connections_) {
        auto shared_connection = connection.lock();
        if (shared_connection) {
          shared_connection->Shutdown();
        }
      }
      connections_.clear();
    });
  }

 private:
  void StartAccept() {
    socket_.emplace(io_context_);
    acceptor_->async_accept(*socket_, strand_.wrap(std::bind(&Impl::HandleAccept, this, _1)));
  }

  void HandleAccept(const boost::system::error_code& ec) {
    if (ec) {
      LOG_IF(WARNING, ec != boost::asio::error::operation_aborted)
          << "Accept failed: " << ec.message();
      return;
    }

    if (!CheckAddress()) {
      boost::system::error_code ec;
      socket_->close(ec);
      LOG_IF(WARNING, ec) << "Close failed: " << ec.message();
      StartAccept();
      return;
    }

    auto connection = std::make_shared<TunnelConnection>(&io_context_, socket_.get_ptr());
    connection->Start(remote_);
    bool found = false;
    for (auto& weak_connection : connections_) {
      auto shared_connection = weak_connection.lock();
      if (!shared_connection) {
        found = true;
        weak_connection = connection;
        break;
      }
    }
    if (!found) {
      connections_.push_back(connection);
    }
    StartAccept();
  }

  bool CheckAddress() {
    if (!address_checker_) {
      return true;
    }

    boost::system::error_code ec;
    auto endpoint = socket_->remote_endpoint(ec);

    if (ec) {
      LOG(WARNING) << "Cannot get remote endpoint: " << ec.message();
      return true;
    }

    return address_checker_(endpoint.address());
  }

  boost::asio::io_context& io_context_;
  boost::asio::io_context::strand strand_;
  AddressChecker address_checker_;
  Endpoint local_;
  Endpoint remote_;
  boost::optional<boost::asio::ip::tcp::acceptor> acceptor_;
  boost::optional<boost::asio::ip::tcp::socket> socket_;
  std::vector<std::weak_ptr<TunnelConnection>> connections_;
  std::atomic<bool> closing_{false};
};

Tunnel::Tunnel(boost::asio::io_context* io_context) : impl_(new Impl(io_context)) {
}

Tunnel::~Tunnel() {
}

Status Tunnel::Start(const Endpoint& local, const Endpoint& remote,
                     AddressChecker address_checker) {
  return impl_->Start(local, remote, std::move(address_checker));
}

void Tunnel::Shutdown() {
  impl_->Shutdown();
}

} // namespace yb
