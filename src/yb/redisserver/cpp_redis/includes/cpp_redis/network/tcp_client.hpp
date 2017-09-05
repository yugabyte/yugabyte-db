#pragma once

#include <string>
#include <vector>
#include <mutex>
#include <thread>
#include <atomic>
#include <stdexcept>

#include "cpp_redis/network/io_service.hpp"
#include "cpp_redis/redis_error.hpp"

#ifndef CPP_REDIS_READ_SIZE
# define CPP_REDIS_READ_SIZE 4096
#endif /* CPP_REDIS_READ_SIZE */

namespace cpp_redis {

namespace network {

//! tcp_client
//! async tcp client based on boost asio
class tcp_client {
public:
  //! ctor & dtor
  tcp_client(void);
  ~tcp_client(void);

  //! assignment operator & copy ctor
  tcp_client(const tcp_client&) = delete;
  tcp_client& operator=(const tcp_client&) = delete;

  //! returns whether the client is connected or not
  bool is_connected(void);

  //! handle connection & disconnection
  typedef std::function<void(tcp_client&)> disconnection_handler_t;
  typedef std::function<bool(tcp_client&, const std::vector<char>& buffer)> receive_handler_t;
  void connect(const std::string& host, unsigned int port,
               const disconnection_handler_t& disconnection_handler = nullptr,
               const receive_handler_t& receive_handler = nullptr);
  void disconnect(void);

  //! send data
  void send(const std::string& buffer);
  void send(const std::vector<char>& buffer);

private:
  //! make boost asio async read and write operations
  void async_read(void);
  void async_write(void);

  //! io service callback
  void io_service_disconnection_handler(io_service&);

  void clear_buffer(void);

private:
  //! io service instance
  io_service& m_io_service;

  //! socket fd
  int m_fd;

  //! is connected
  std::atomic_bool m_is_connected;

  //! buffers
  static const unsigned int READ_SIZE = CPP_REDIS_READ_SIZE;
  std::vector<char> m_read_buffer;
  std::vector<char> m_write_buffer;

  //! handlers
  receive_handler_t m_receive_handler;
  disconnection_handler_t m_disconnection_handler;

  //! thread safety
  std::mutex m_write_buffer_mutex;
};

} //! network

} //! cpp_redis
