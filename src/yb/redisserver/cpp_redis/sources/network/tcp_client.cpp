#include <condition_variable>
#include <netdb.h>
#include <cstring>

#include "cpp_redis/network/tcp_client.hpp"

namespace cpp_redis {

namespace network {

//! note that we call io_service::get_instance in the init list
//!
//! this will force force io_service instance creation
//! this is a workaround to handle static object destructions order
//!
//! that way, any object containing a tcp_client has an attribute (or through its attributes)
//! is guaranteed to be destructed before the io_service is destructed, even if it is global
tcp_client::tcp_client(void)
: m_io_service(io_service::get_instance())
, m_fd(-1)
, m_is_connected(false)
, m_receive_handler(nullptr)
, m_disconnection_handler(nullptr)
{}

tcp_client::~tcp_client(void) {
  if (m_is_connected)
    disconnect();
}

void
tcp_client::connect(const std::string& host, unsigned int port,
                    const disconnection_handler_t& disconnection_handler,
                    const receive_handler_t& receive_handler)
{
  if (m_is_connected)
    return ;

  //! create the socket
  m_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (m_fd < 0)
      throw redis_error("Can't open a socket");

  //! get the server's DNS entry
  struct hostent *server = gethostbyname(host.c_str());
  if (not server)
    throw redis_error("No such host: " + host);

  //! build the server's Internet address
  struct sockaddr_in server_addr;
  std::memset(&server_addr, 0, sizeof(server_addr));
  std::memcpy(&server_addr.sin_addr.s_addr, server->h_addr, server->h_length);
  server_addr.sin_port = htons(port);
  server_addr.sin_family = AF_INET;

  //! create a connection with the server
  if (::connect(m_fd, reinterpret_cast<const struct sockaddr *>(&server_addr), sizeof(server_addr)) < 0)
    throw redis_error("Fail to connect to " + host + ":" + std::to_string(port));

  //! add fd to the io_service and set the disconnection & recv handlers
  m_disconnection_handler = disconnection_handler;
  m_receive_handler = receive_handler;
  m_io_service.track(m_fd, std::bind(&tcp_client::io_service_disconnection_handler, this, std::placeholders::_1));
  m_is_connected = true;

  //! start async read
  async_read();
}

void
tcp_client::disconnect(void) {
  if (not m_is_connected)
    return ;

  m_is_connected = false;
  m_io_service.untrack(m_fd);
  close(m_fd);
  clear_buffer();
}

void
tcp_client::send(const std::string& buffer) {
  send(std::vector<char>{ buffer.begin(), buffer.end() });
}

void
tcp_client::send(const std::vector<char>& buffer) {
  if (not m_is_connected)
    throw redis_error("Not connected");

  if (not buffer.size())
    return ;

  std::lock_guard<std::mutex> lock(m_write_buffer_mutex);

  bool bytes_in_buffer = m_write_buffer.size() > 0;

  //! concat buffer
  m_write_buffer.insert(m_write_buffer.end(), buffer.begin(), buffer.end());

  //! if there were already bytes in buffer, simply return
  //! async_write callback will process the new buffer
  if (bytes_in_buffer)
    return ;

  async_write();
}

void
tcp_client::async_read(void) {
  m_io_service.async_read(m_fd, m_read_buffer, READ_SIZE,
    [&](std::size_t length) {
      if (m_receive_handler)
        if (not m_receive_handler(*this, { m_read_buffer.begin(), m_read_buffer.begin() + length })) {
          disconnect();
          return ;
        }

      //! clear read buffer keep waiting for incoming bytes
      m_read_buffer.clear();

      if (m_is_connected)
        async_read();
    });
}

void
tcp_client::async_write(void) {
  m_io_service.async_write(m_fd, m_write_buffer, m_write_buffer.size(),
    [&](std::size_t length) {
      std::lock_guard<std::mutex> lock(m_write_buffer_mutex);

      m_write_buffer.erase(m_write_buffer.begin(), m_write_buffer.begin() + length);

      if (m_is_connected and m_write_buffer.size())
        async_write();
    });
}

bool
tcp_client::is_connected(void) {
  return m_is_connected;
}

void
tcp_client::io_service_disconnection_handler(network::io_service&) {
  m_is_connected = false;
  close(m_fd);
  clear_buffer();

  if (m_disconnection_handler)
      m_disconnection_handler(*this);
}

void
tcp_client::clear_buffer(void) {
  std::lock_guard<std::mutex> lock(m_write_buffer_mutex);
  m_write_buffer.clear();
  m_read_buffer.clear();
}

} //! network

} //! cpp_redis
