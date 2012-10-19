/*
 * Copyright (c) 2011, Prevas A/S
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of Prevas A/S nor the names of its
 *       contributors may be used to endorse or promote products derived from
 *       this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/**
 * DARC NetworkManager class
 *
 * \author Morten Kjaergaard
 */

#pragma once

#include <boost/regex.hpp>
//#include <darc/network/link_manager_callback_if.h>
//#include <darc/network/inbound_link.h>
//#include <darc/network/udp/protocol_manager.h>
//#include <darc/network/zmq/protocol_manager.h>

namespace darc
{
namespace network
{

typedef ID NodeID;
typedef ID ConnectionID;

class network_manager// : public network_managerCallbackIF
{
private:
  NodeID node_id_;

  // Protocol Managers
  //zeromq::ProtocolManager zmq_manager_;

  // Map "protocol" -> Manager
  typedef std::map<const std::string, ProtocolManagerBase*> ManagerProtocolMapType;
  ManagerProtocolMapType manager_protocol_map_;

  // Map "Inbound ConnectionID" -> Manager
  typedef std::map<const ConnectionID, ProtocolManagerBase*> ManagerConnectionMapType;
  ManagerConnectionMapType manager_connection_map_; 

  // Node -> Outbound connection map (handle this a little more intelligent, more connections per nodes, timeout etc)
  typedef std::map<const NodeID, const ConnectionID> NeighbourNodesType; // NodeID -> OutboundID
  NeighbourNodesType neighbour_nodes_;

  // Callbacks to handlers of certain packet types
  typedef boost::function< void(const packet::Header&, SharedBuffer, std::size_t) > PacketReceivedHandlerType;
  std::map< packet::Header::PayloadType, PacketReceivedHandlerType > packet_received_handlers_;

  // Event Callbacks
  typedef boost::function< void(const ID&) > NewRemoteNodeListenerType;
  typedef std::vector< NewRemoteNodeListenerType > NewRemoteNodeListenerListType;
  NewRemoteNodeListenerListType new_remote_node_listeners_;

public:
  network_manager(boost::asio::io_service * io_service, NodeID& node_id) :
    node_id_(node_id),
    udp_manager_(io_service, this),
    zmq_manager_(io_service, this)
  {
    // Link protocol names and protocol managers
    manager_protocol_map_["udp"] = &udp_manager_;
    manager_protocol_map_["zmq+tcp"] = &zmq_manager_;
  }

  void addNewRemoteNodeListener(NewRemoteNodeListenerType listener)
  {
    new_remote_node_listeners_.push_back(listener);
  }

  // Impl of CallbackIF
  const NodeID& getNodeID()
  {
    return node_id_;
  }

  void getRemoteNodeList(std::vector<NodeID>& node_list)
  {
    node_list.clear();
    for(NeighbourNodesType::iterator it = neighbour_nodes_.begin();
	it != neighbour_nodes_.end();
	it++)
    {
      node_list.push_back(it->first);
    }
  }

  void sendPacket(packet::Header::PayloadType type, const NodeID& recv_node_id, SharedBuffer buffer, std::size_t data_len)
  {
    // ID::null means we send to all nodes
    if( recv_node_id == ID::null() )
    {
      for( NeighbourNodesType::iterator it = neighbour_nodes_.begin(); it != neighbour_nodes_.end(); it++ )
      {
	zmq_manager_.sendPacket(it->second, type, it->first, buffer, data_len );
      }
    }
    else
    {
      NeighbourNodesType::iterator item = neighbour_nodes_.find(recv_node_id);
      if(item != neighbour_nodes_.end())
      {
	zmq_manager_.sendPacket(item->second, type, item->first, buffer, data_len );
      }
      else
      {
	DARC_WARNING("Trying to send packet to unknown node: %s", recv_node_id.short_string().c_str());
      }
    }
  }

  void registerPacketReceivedHandler(packet::Header::PayloadType type, PacketReceivedHandlerType handler)
  {
    packet_received_handlers_[type] = handler;
  }

  void accept(const std::string& url)
  {
    try
    {
      boost::smatch what;
      if(boost::regex_match( url, what, boost::regex("^(.+)://(.+)$") ))
      {
	ProtocolManagerBase * mngr = getManager(what[1]);
	if(mngr)
	{
	  ConnectionID inbound_id = mngr->accept(what[1], what[2]);
	  manager_connection_map_.insert(ManagerConnectionMapType::value_type(inbound_id, mngr));
	}
	else
	{
	  DARC_ERROR("Unsupported Protocol: %s in %s", std::string(what[1]).c_str(), url.c_str());
	}
      }
      else
      {
	DARC_ERROR("Invalid URL: %s", url.c_str());
      }
    }
    catch(std::exception& e) //todo: handle the possible exceptions
    {
      std::cout << e.what() << std::endl;
    }
  }

  void connect(const std::string& url)
  {
    try
    {
      boost::smatch what;
      if( boost::regex_match(url, what, boost::regex("^(.+)://(.+)$")) )
      {
	ProtocolManagerBase * mngr = getManager(what[1]);
	if(mngr)
	{
	  mngr->connect(what[1], what[2]);
	}
	else
	{
	  DARC_ERROR("Unsupported Protocol: %s in %s", std::string(what[1]).c_str(), url.c_str());
	}
      }
      else
      {
	DARC_ERROR("Invalid URL: %s", url.c_str());
      }
    }
    catch(std::exception& e) //todo: handle the possible exceptions
    {
      std::cout << e.what() << std::endl;
    }
  }

private:
  void handleDiscoverPacket(const NodeID& sender_node_id, InboundLink * source_link, SharedBuffer buffer, std::size_t data_len)
  {
    packet::Discover discover;
    discover.read(buffer.data(), data_len);
    source_link->sendDiscoverReply(discover.link_id, sender_node_id);
    DARC_INFO("DISCOVER from node %s (%s) on inbound %s",
	      sender_node_id.short_string().c_str(),
	      discover.link_id.short_string().c_str(),
	      "todo");
	       
    // If we received a DISCOVER from a node we dont know that we have a direct link to, send a DISCOVER back
    if(neighbour_nodes_.count(sender_node_id) == 0)
    {
      source_link->sendDiscoverToAll();
    }
  }

  void handleDiscoverReplyPacket(const NodeID& sender_node_id, const ConnectionID& inbound_id, SharedBuffer buffer, std::size_t data_len)
  {
    // todo: check that we have such inbound link!
    packet::DiscoverReply discover_reply;
    discover_reply.read(buffer.data(), data_len);
    DARC_INFO("Found Node %s on outbound connection %s", sender_node_id.short_string().c_str(), discover_reply.link_id.short_string().c_str() );
    neighbour_nodes_.insert(NeighbourNodesType::value_type(sender_node_id, discover_reply.link_id));
    // Notify
    for(NewRemoteNodeListenerListType::iterator it = new_remote_node_listeners_.begin();
	it != new_remote_node_listeners_.end();
	it++)
    {
      (*it)(sender_node_id);
    }

  }

  void receiveHandler(const ConnectionID& inbound_id, InboundLink * source_link, SharedBuffer buffer, std::size_t data_len)
  {
    assert(false);
  }

  void receiveHandler(const ConnectionID& inbound_id,
		      InboundLink * source_link,
		      SharedBuffer header_buffer,
		      std::size_t header_data_len,
		      SharedBuffer buffer,
		      std::size_t data_len)

  {
    packet::Header header;
    header.read(header_buffer.data(), header_data_len);

    // Discard packages not to us, or from self, e.g. due to multicasting
    if((header.recv_node_id != ID::null() && header.recv_node_id != getNodeID()) ||
       header.sender_node_id == getNodeID())
    {
      return;
    }

    // Switch on packet type
    switch(header.payload_type)
    {
      case packet::Header::MSG_SUBSCRIBE:
      case packet::Header::MSG_PUBLISH_INFO:
      case packet::Header::MSG_PACKET:
      case packet::Header::PROCEDURE_ADVERTISE:
      case packet::Header::PROCEDURE_CALL:
      case packet::Header::PROCEDURE_FEEDBACK:
      case packet::Header::PROCEDURE_RESULT:
      {
	// todo: verify we have a listener
	packet_received_handlers_[header.payload_type]( header, buffer, data_len );
	break;
      }
      case packet::Header::DISCOVER_PACKET:
      {
	handleDiscoverPacket(header.sender_node_id, source_link, buffer, data_len);
	break;
      }
      case packet::Header::DISCOVER_REPLY_PACKET:
      {
	handleDiscoverReplyPacket(header.sender_node_id, inbound_id, buffer, data_len);
	break;
      }
      default:
      {
	DARC_WARNING("Unknown packet type received %u", header.payload_type);
	break;
      }
    }
  }

  // Get the protocol manager from a protocol name
  ProtocolManagerBase * getManager(const std::string& protocol)
  {
    ManagerProtocolMapType::iterator elem = manager_protocol_map_.find(protocol);
    if( elem != manager_protocol_map_.end() )
    {
      return elem->second;
    }
    else
    {
      return 0;
    }
  }

};

}
}

#endif