// Copyright 2009-2018 Toby Schneider (http://gobysoft.org/index.wt/people/toby)
//                     GobySoft, LLC (2013-)
//                     Massachusetts Institute of Technology (2007-2014)
//                     Community contributors (see AUTHORS file)
//
//
// This file is part of the Goby Underwater Autonomy Project Libraries
// ("The Goby Libraries").
//
// The Goby Libraries are free software: you can redistribute them and/or modify
// them under the terms of the GNU Lesser General Public License as published by
// the Free Software Foundation, either version 2.1 of the License, or
// (at your option) any later version.
//
// The Goby Libraries are distributed in the hope that they will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public License
// along with Goby.  If not, see <http://www.gnu.org/licenses/>.

#include "transport-interprocess-zeromq.h"

using goby::glog;
using namespace goby::common::logger;

void goby::setup_socket(zmq::socket_t& socket, const goby::common::protobuf::ZeroMQServiceConfig::Socket& cfg)
{
    int send_hwm = cfg.send_queue_size();
    int receive_hwm = cfg.receive_queue_size();
    socket.setsockopt(ZMQ_SNDHWM, &send_hwm, sizeof(send_hwm));
    socket.setsockopt(ZMQ_RCVHWM, &receive_hwm, sizeof(receive_hwm));

    bool bind = (cfg.connect_or_bind() == common::protobuf::ZeroMQServiceConfig::Socket::BIND);

    std::string endpoint;
    switch(cfg.transport())
    {
        case common::protobuf::ZeroMQServiceConfig::Socket::IPC: endpoint = "ipc://" + cfg.socket_name(); break;                    
        case common::protobuf::ZeroMQServiceConfig::Socket::TCP: endpoint = "tcp://" + (bind ? std::string("*") : cfg.ethernet_address()) + ":" + std::to_string(cfg.ethernet_port()); break;
        default:
            throw(std::runtime_error("Unsupported transport type: " +  common::protobuf::ZeroMQServiceConfig::Socket::Transport_Name(cfg.transport())));
            break;
    }
    
    if(bind)
	socket.bind(endpoint.c_str());
    else	   
	socket.connect(endpoint.c_str());
}

//
// ZMQMainThread
//

goby::ZMQMainThread::ZMQMainThread(zmq::context_t& context) :
    control_socket_(context, ZMQ_PAIR),
    publish_socket_(context, ZMQ_PUB)
{
    control_socket_.bind("inproc://control");
    
}

bool goby::ZMQMainThread::recv(protobuf::InprocControl* control_msg, int flags)
{
    zmq::message_t zmq_msg;
    bool message_received = false;
    if(control_socket_.recv(&zmq_msg, flags))
    {
	control_msg->ParseFromArray((char*)zmq_msg.data(), zmq_msg.size());	
	glog.is(DEBUG3) && glog << "Main thread received control msg: " << control_msg->DebugString() << std::endl;
	message_received = true;
    }

    return message_received;
}

void goby::ZMQMainThread::set_publish_cfg(const goby::common::protobuf::ZeroMQServiceConfig::Socket& cfg)   
{
    setup_socket(publish_socket_, cfg);
    publish_socket_configured_ = true;

    // TODO: remove in favor of more explicit synchronization, if possible    
    usleep(100000); // avoids "slow joiner" on initial publications
    
    // publish any queued up messages
    for(auto& pub_pair : publish_queue_)
	publish(pub_pair.first, &pub_pair.second[0], pub_pair.second.size());
    publish_queue_.clear();
}

void goby::ZMQMainThread::publish(const std::string& identifier, const char* bytes, int size)
{
    if(publish_socket_configured_)
    {
	zmq::message_t msg(identifier.size() + size);
	memcpy(msg.data(), identifier.data(), identifier.size());
	memcpy(static_cast<char*>(msg.data())+identifier.size(),
	       bytes, size);
	publish_socket_.send(msg);
	
	glog.is(DEBUG3) && glog << "Published " << size << " bytes to [" << identifier.substr(0, identifier.size()-1) << "]" << std::endl;

    }
    else
    {
	publish_queue_.push_back(std::make_pair(identifier, std::vector<char>(bytes, bytes+size)));
    }
}

void goby::ZMQMainThread::subscribe(const std::string& identifier)
{
    protobuf::InprocControl control;
    control.set_type(protobuf::InprocControl::SUBSCRIBE);
    control.set_subscription_identifier(identifier);
    send_control_msg(control);
}

void goby::ZMQMainThread::unsubscribe(const std::string& identifier)
{
    protobuf::InprocControl control;
    control.set_type(protobuf::InprocControl::UNSUBSCRIBE);
    control.set_subscription_identifier(identifier);
    send_control_msg(control);
}
void goby::ZMQMainThread::reader_shutdown()
{
    protobuf::InprocControl control;
    control.set_type(protobuf::InprocControl::SHUTDOWN);
    send_control_msg(control);
}

void goby::ZMQMainThread::send_control_msg(const protobuf::InprocControl& control)
{
    zmq::message_t zmq_control_msg(control.ByteSize());
    control.SerializeToArray((char*)zmq_control_msg.data(), zmq_control_msg.size());            
    control_socket_.send(zmq_control_msg);
}

//
// ZMQReadThread
//
goby::ZMQReadThread::ZMQReadThread(const protobuf::InterProcessPortalConfig& cfg, zmq::context_t& context, std::atomic<bool>& alive, std::shared_ptr<std::condition_variable_any> poller_cv) :
    cfg_(cfg),
    control_socket_(context, ZMQ_PAIR),
    subscribe_socket_(context, ZMQ_SUB),
    manager_socket_(context, ZMQ_REQ),
    alive_(alive),
    poller_cv_(poller_cv)
{
    poll_items_.resize(NUMBER_SOCKETS); 
    poll_items_[SOCKET_CONTROL] = { (void*)control_socket_, 0, ZMQ_POLLIN, 0 };
    poll_items_[SOCKET_MANAGER] = { (void*)manager_socket_, 0, ZMQ_POLLIN, 0 }; 
    poll_items_[SOCKET_SUBSCRIBE] = { (void*)subscribe_socket_, 0, ZMQ_POLLIN, 0 }; 

    control_socket_.connect("inproc://control");
    
    goby::common::protobuf::ZeroMQServiceConfig::Socket query_socket;
    query_socket.set_socket_type(common::protobuf::ZeroMQServiceConfig::Socket::REQUEST);
    query_socket.set_socket_id(SOCKET_MANAGER);

    switch(cfg_.transport())
    {
    case protobuf::InterProcessPortalConfig::IPC:
	query_socket.set_transport(common::protobuf::ZeroMQServiceConfig::Socket::IPC);
	query_socket.set_socket_name((cfg_.has_socket_name() ? cfg_.socket_name() : "/tmp/goby_" + cfg_.platform()) + ".manager");
	break;
    case protobuf::InterProcessPortalConfig::TCP:
	query_socket.set_transport(common::protobuf::ZeroMQServiceConfig::Socket::TCP);
	query_socket.set_ethernet_address(cfg_.ipv4_address());
	query_socket.set_ethernet_port(cfg_.tcp_port());
	break;
    }
    query_socket.set_connect_or_bind(common::protobuf::ZeroMQServiceConfig::Socket::CONNECT);
    setup_socket(manager_socket_, query_socket);    
}

void goby::ZMQReadThread::run()
{
    while(alive_)
    {
	if(have_pubsub_sockets_)
	{
	    poll(); 
	}
	else
	{
	    protobuf::ZMQManagerRequest req;
	    req.set_request(protobuf::PROVIDE_PUB_SUB_SOCKETS);

	    zmq::message_t msg(req.ByteSize());
	    req.SerializeToArray(static_cast<char*>(msg.data()), req.ByteSize());
	    manager_socket_.send(msg);
	    
	    
            
            auto start = std::chrono::system_clock::now();
            while(!have_pubsub_sockets_ && (start + std::chrono::seconds(cfg_.manager_timeout_seconds()) > std::chrono::system_clock::now()))
		poll(cfg_.manager_timeout_seconds()*1000);	    
	
	    if(!have_pubsub_sockets_)
		goby::glog.is(goby::common::logger::DIE) && goby::glog << "No response from gobyd: " << cfg_.ShortDebugString() << std::endl;
	}
    }
}

void goby::ZMQReadThread::poll(long timeout_ms)
{
    zmq::poll(&poll_items_[0], poll_items_.size(), timeout_ms);

    for(int i = 0, n = poll_items_.size(); i < n; ++i)
    {
        if (poll_items_[i].revents & ZMQ_POLLIN) 
        {
            zmq::message_t zmq_msg;
	    switch(i)
	    {
	    case SOCKET_CONTROL:
		if(control_socket_.recv(&zmq_msg))
		    control_data(zmq_msg);
		break;
	    case SOCKET_SUBSCRIBE:
		if(subscribe_socket_.recv(&zmq_msg))
		    subscribe_data(zmq_msg);
		break;
	    case SOCKET_MANAGER:
		if(manager_socket_.recv(&zmq_msg))
		    manager_data(zmq_msg);
		break;
	    }
	}
    }

}

void goby::ZMQReadThread::control_data(const zmq::message_t& zmq_msg)
{
    // command from the main thread
    protobuf::InprocControl control_msg;
    control_msg.ParseFromArray((char*)zmq_msg.data(), zmq_msg.size());	

    switch(control_msg.type())
    {
        case protobuf::InprocControl::SUBSCRIBE:
	{
	    auto& zmq_filter = control_msg.subscription_identifier();
	    subscribe_socket_.setsockopt(ZMQ_SUBSCRIBE, zmq_filter.c_str(), zmq_filter.size());
    
	    glog.is(DEBUG2) &&
		glog << "subscribed with identifier: [" << zmq_filter << "]" << std::endl ;
	    break;
	}
        case protobuf::InprocControl::UNSUBSCRIBE:
	{
	    auto& zmq_filter = control_msg.subscription_identifier();
	    glog.is(DEBUG2) &&
		glog << "unsubscribing with identifier: [" << zmq_filter << "]" << std::endl ;

	    subscribe_socket_.setsockopt(ZMQ_UNSUBSCRIBE, zmq_filter.c_str(), zmq_filter.size());
	    
	    break;	   
	}
        case protobuf::InprocControl::SHUTDOWN:
	{
	    alive_ = false;
	}
        default: break;
    }
    
}
void goby::ZMQReadThread::subscribe_data(const zmq::message_t& zmq_msg)
{   
    // data from goby - forward to the main thread
    protobuf::InprocControl control;
    control.set_type(protobuf::InprocControl::RECEIVE);
    control.set_received_data(std::string((char*)zmq_msg.data(), zmq_msg.size()));    
    send_control_msg(control);
}
void goby::ZMQReadThread::manager_data(const zmq::message_t& zmq_msg)
{
    // manager (gobyd) reply
    protobuf::ZMQManagerResponse response;
    response.ParseFromArray(zmq_msg.data(), zmq_msg.size());
    if(response.request() == protobuf::PROVIDE_PUB_SUB_SOCKETS)
    {
	if(response.subscribe_socket().transport() == common::protobuf::ZeroMQServiceConfig::Socket::TCP)
	    response.mutable_subscribe_socket()->set_ethernet_address(cfg_.ipv4_address());
	if(response.publish_socket().transport() == common::protobuf::ZeroMQServiceConfig::Socket::TCP)
	    response.mutable_publish_socket()->set_ethernet_address(cfg_.ipv4_address());

	setup_socket(subscribe_socket_, response.subscribe_socket());

	protobuf::InprocControl control;
	control.set_type(protobuf::InprocControl::PUB_CONFIGURATION);
	*control.mutable_publish_socket() = response.publish_socket();
	send_control_msg(control);

	have_pubsub_sockets_ = true;

	glog.is(DEBUG3) && glog << "Received manager sockets: " << response.DebugString() << std::endl;
    }
}

void goby::ZMQReadThread::send_control_msg(const protobuf::InprocControl& control)
{
    zmq::message_t zmq_control_msg(control.ByteSize());
    control.SerializeToArray((char*)zmq_control_msg.data(), zmq_control_msg.size());            
    control_socket_.send(zmq_control_msg);
    poller_cv_->notify_all();
}

//
// ZMQRouter
//

unsigned goby::ZMQRouter::last_port(zmq::socket_t& socket)
{
    size_t last_endpoint_size = 100;
    char last_endpoint[last_endpoint_size];
    int rc = zmq_getsockopt ((void*)socket, ZMQ_LAST_ENDPOINT, &last_endpoint, &last_endpoint_size);
                
    if(rc != 0)
	throw(std::runtime_error("Could not retrieve ZMQ_LAST_ENDPOINT"));

    std::string last_ep(last_endpoint);
    unsigned port = std::stoi(last_ep.substr(last_ep.find_last_of(":")+1));
    return port;
}


void goby::ZMQRouter::run()
{
    zmq::socket_t frontend(context_, ZMQ_XPUB);
    zmq::socket_t backend(context_, ZMQ_XSUB);

    int send_hwm = cfg_.send_queue_size();
    int receive_hwm = cfg_.receive_queue_size();
    frontend.setsockopt(ZMQ_SNDHWM, &send_hwm, sizeof(send_hwm));
    backend.setsockopt(ZMQ_SNDHWM, &send_hwm, sizeof(send_hwm));
    frontend.setsockopt(ZMQ_RCVHWM, &receive_hwm, sizeof(receive_hwm));
    backend.setsockopt(ZMQ_RCVHWM, &receive_hwm, sizeof(receive_hwm));

    switch(cfg_.transport())
    {
    case goby::protobuf::InterProcessPortalConfig::IPC:
    {
	std::string xpub_sock_name = "ipc://" + (cfg_.has_socket_name() ? cfg_.socket_name() : "/tmp/goby_" + cfg_.platform()) + ".xpub";
	std::string xsub_sock_name = "ipc://" + (cfg_.has_socket_name() ? cfg_.socket_name() : "/tmp/goby_" + cfg_.platform()) + ".xsub";
	frontend.bind(xpub_sock_name.c_str());
	backend.bind(xsub_sock_name.c_str());
	break;
    }
    case goby::protobuf::InterProcessPortalConfig::TCP:
    {
	frontend.bind("tcp://*:0");
	backend.bind("tcp://*:0");
	pub_port = last_port(frontend);    
	sub_port = last_port(backend);
	break;
    }
    }
    try
    {
	zmq::proxy((void*)frontend, (void*)backend, nullptr);
    }
    catch(const zmq::error_t& e)
    {
	// context terminated
	if(e.num() == ETERM)
	    return;
	else
	    throw(e);
    }
}

//
// ZMQRouter
//
void goby::ZMQManager::run()
{
    zmq::socket_t socket (context_, ZMQ_REP);

    switch(cfg_.transport())
    {
    case goby::protobuf::InterProcessPortalConfig::IPC:
    {
	std::string sock_name = "ipc://" + ((cfg_.has_socket_name() ? cfg_.socket_name() : "/tmp/goby_" + cfg_.platform()) + ".manager");
	socket.bind(sock_name.c_str());
	break;
    }
    case goby::protobuf::InterProcessPortalConfig::TCP:
    {
	std::string sock_name = "tcp://*:" + std::to_string(cfg_.tcp_port());
	socket.bind(sock_name.c_str());
	break;
    }
    }
    
    try
    {
	while (true)
	{
	    zmq::message_t request;
	    socket.recv (&request);

	    goby::protobuf::ZMQManagerRequest pb_request;
	    pb_request.ParseFromArray((char*)request.data(), request.size());

	    while(cfg_.transport() == goby::protobuf::InterProcessPortalConfig::TCP && (router_.pub_port == 0 || router_.sub_port == 0))
		usleep(1e4);

	    goby::protobuf::ZMQManagerResponse pb_response;
	    pb_response.set_request(pb_request.request());

	    if(pb_request.request() == goby::protobuf::PROVIDE_PUB_SUB_SOCKETS)
	    {
		goby::common::protobuf::ZeroMQServiceConfig::Socket* subscribe_socket = pb_response.mutable_subscribe_socket();
		goby::common::protobuf::ZeroMQServiceConfig::Socket* publish_socket = pb_response.mutable_publish_socket();
		subscribe_socket->set_socket_type(goby::common::protobuf::ZeroMQServiceConfig::Socket::SUBSCRIBE);
		publish_socket->set_socket_type(goby::common::protobuf::ZeroMQServiceConfig::Socket::PUBLISH);
		subscribe_socket->set_connect_or_bind(goby::common::protobuf::ZeroMQServiceConfig::Socket::CONNECT);
		publish_socket->set_connect_or_bind(goby::common::protobuf::ZeroMQServiceConfig::Socket::CONNECT);

		subscribe_socket->set_send_queue_size(cfg_.send_queue_size());
		subscribe_socket->set_receive_queue_size(cfg_.receive_queue_size());
		publish_socket->set_send_queue_size(cfg_.send_queue_size());
		publish_socket->set_receive_queue_size(cfg_.receive_queue_size());
                
		switch(cfg_.transport())
		{
		case goby::protobuf::InterProcessPortalConfig::IPC:
		    subscribe_socket->set_transport(goby::common::protobuf::ZeroMQServiceConfig::Socket::IPC);
		    publish_socket->set_transport(goby::common::protobuf::ZeroMQServiceConfig::Socket::IPC);
		    subscribe_socket->set_socket_name((cfg_.has_socket_name() ? cfg_.socket_name() : "/tmp/goby_" + cfg_.platform()) + ".xpub");
		    publish_socket->set_socket_name((cfg_.has_socket_name() ? cfg_.socket_name() : "/tmp/goby_" + cfg_.platform()) + ".xsub");
		    break;
		case goby::protobuf::InterProcessPortalConfig::TCP:
		    subscribe_socket->set_transport(goby::common::protobuf::ZeroMQServiceConfig::Socket::TCP);
		    publish_socket->set_transport(goby::common::protobuf::ZeroMQServiceConfig::Socket::TCP);
		    subscribe_socket->set_ethernet_port(router_.pub_port); // our publish is their subscribe
		    publish_socket->set_ethernet_port(router_.sub_port);
		    break;
		}
	    }
            
	    zmq::message_t reply(pb_response.ByteSize());
	    pb_response.SerializeToArray((char*)reply.data(), reply.size());            
	    socket.send(reply);            
	}
    }
    catch(const zmq::error_t& e)
    {
	// context terminated
	if(e.num() == ETERM)
	    return;
	else
	    throw(e);
    }
}
