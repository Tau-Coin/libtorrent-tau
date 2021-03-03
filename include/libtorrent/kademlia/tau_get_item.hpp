/*

Copyright (c) 2013, Steven Siloti
Copyright (c) 2013-2019, Arvid Norberg
Copyright (c) 2015, Thomas Yuan
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

#ifndef LIBTORRENT_TAU_GET_ITEM_HPP
#define LIBTORRENT_TAU_GET_ITEM_HPP

#include <libtorrent/kademlia/tau_find_data.hpp>
#include <libtorrent/kademlia/item.hpp>

#include <memory>

namespace libtorrent {
namespace dht {

class tau_get_item : public tau_find_data
{
public:
	using data_callback = std::function<void(item const&, bool)>;

	void got_data(bdecode_node const& v,
		public_key const& pk,
		sequence_number seq,
		signature const& sig);

	// for mutable items
	tau_get_item(node& dht_node
		, public_key const& pk
		, span<char const> salt
		, data_callback dcallback
		, nodes_callback ncallback
        , token_callback tcallback);

	char const* name() const override;

    // Added by TAU community.
    item get_item();

protected:
	observer_ptr new_observer(udp::endpoint const& ep
		, node_id const& id) override;
	bool invoke(observer_ptr o) override;
	void done() override;

	data_callback m_data_callback;
	item m_data;
	bool m_immutable;
};

class tau_get_item_observer : public tau_find_data_observer
{
public:
	tau_get_item_observer(
		std::shared_ptr<traversal_algorithm> algorithm
		, udp::endpoint const& ep, node_id const& id)
		: tau_find_data_observer(std::move(algorithm), ep, id)
	{}

	void reply(msg const&) override;
};

} // namespace dht
} // namespace libtorrent

#endif // LIBTORRENT_TAU_GET_ITEM_HPP
