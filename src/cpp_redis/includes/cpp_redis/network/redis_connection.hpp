#pragma once

#include <mutex>
#include <string>
#include <vector>
#include <functional>

#include "cpp_redis/network/tcp_client.hpp"
#include "cpp_redis/builders/reply_builder.hpp"

namespace cpp_redis {

namespace network {

class redis_connection {
public:
  //! ctor & dtor
  redis_connection(void);
  ~redis_connection(void);

  //! copy ctor & assignment operator
  redis_connection(const redis_connection&) = delete;
  redis_connection& operator=(const redis_connection&) = delete;

public:
  //! handle connection
  typedef std::function<void(redis_connection&)> disconnection_handler_t;
  typedef std::function<void(redis_connection&, reply&)> reply_callback_t;
  void connect(const std::string& host = "127.0.0.1", unsigned int port = 6379,
               const disconnection_handler_t& disconnection_handler = nullptr,
               const reply_callback_t& reply_callback = nullptr);
  void disconnect(void);
  bool is_connected(void);

  //! send cmd
  redis_connection& send(const std::vector<std::string>& redis_cmd);

  //! commit pipelined transaction
  redis_connection& commit(void);

private:
  //! receive & disconnection handlers
  bool tcp_client_receive_handler(network::tcp_client&, const std::vector<char>& buffer);
  void tcp_client_disconnection_handler(network::tcp_client&);

  std::string build_command(const std::vector<std::string>& redis_cmd);

private:
  //! tcp client for redis connection
  network::tcp_client m_client;

  //! reply callback
  reply_callback_t m_reply_callback;

  //! user defined disconnection handler
  disconnection_handler_t m_disconnection_handler;

  //! reply builder
  builders::reply_builder m_builder;

  //! internal buffer used for pipelining
  std::string m_buffer;

  //! protect internal buffer against race conditions
  std::mutex m_buffer_mutex;
};

} //! network

} //! cpp_redis
