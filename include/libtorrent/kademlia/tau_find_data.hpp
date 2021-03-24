/*

Copyright (c) 2006, Daniel Wallin
Copyright (c) 2006-2010, 2013-2019, Arvid Norberg
Copyright (c) 2016, Alden Torres
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:

    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in
      the documentation and/or other materials provided with the distribution.
    * Neither the name of the author nor the names of its
      contributors may be used to endorse or promote products derived
      from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.

*/

#ifndef TAU_FIND_DATA_210301_HPP
#define TAU_FIND_DATA_210301_HPP

#include <libtorrent/kademlia/traversal_algorithm.hpp>
#include <libtorrent/kademlia/node_id.hpp>
#include <libtorrent/kademlia/observer.hpp>
#include <libtorrent/kademlia/msg.hpp>

#include <vector>
#include <map>

namespace libtorrent {
namespace dht {

class node;

// -------- tau find data -----------

struct tau_find_data : traversal_algorithm
{
	using nodes_callback = std::function<void(std::vector<std::pair<node_entry, std::string>> const&)>;

    // Added by TAU community.
    using token_callback = std::function<void(std::pair<node_entry, std::string> const&)>;

    /*
	find_data(node& dht_node, node_id const& target
		, nodes_callback ncallback);
     */

    // Modified by TAU community.
    tau_find_data(node& dht_node, node_id const& target
        , nodes_callback ncallback, token_callback tcallback);


	//void got_write_token(node_id const& n, std::string write_token);

    // Modified by TAU community.
    void got_write_token(observer_ptr o, node_id const& n, std::string write_token);

    void finished(observer_ptr o) override;

    void failed(observer_ptr o, traversal_flags_t flags = {}) override;

	void start() override;

	char const* name() const override;

protected:

    bool add_requests() override;
	void done() override;
	observer_ptr new_observer(udp::endpoint const& ep
		, node_id const& id) override;

	nodes_callback m_nodes_callback;

    // Added by TAU community.
    token_callback m_token_callback;

	std::map<node_id, std::string> m_write_tokens;
	bool m_done;
};

struct tau_find_data_observer : traversal_observer
{
	tau_find_data_observer(
		std::shared_ptr<traversal_algorithm> algorithm
		, udp::endpoint const& ep, node_id const& id)
		: traversal_observer(std::move(algorithm), ep, id)
	{}

	void reply(msg const&) override;
};

} // namespace dht
} // namespace libtorrent

#endif // TAU_FIND_DATA_210301_HPP
