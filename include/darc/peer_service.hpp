#pragma once

#include <boost/function.hpp>
#include <boost/bind.hpp>
#include <darc/buffer/shared_buffer.hpp>
#include <darc/id.hpp>
#include <darc/peer.hpp>

namespace darc
{

typedef uint32_t service_type;

class peer_service
{
protected:
  typedef boost::function<void(const darc::ID&,
			       service_type,
			       darc::buffer::shared_buffer)> send_to_function_type;
  peer& peer_;

public:
  virtual void recv(const darc::ID& src_peer_id,
		    service_type,
		    darc::buffer::shared_buffer data) = 0;

  peer_service(darc::peer& p) :
    peer_(p)
  {}

  void send_to(const ID& peer_id, service_type service, darc::buffer::shared_buffer data)
  {
    peer_.send_to(peer_id, service, data);
  }


};

}
