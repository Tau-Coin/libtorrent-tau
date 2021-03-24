/*

Copyright (c) 2006, Daniel Wallin
Copyright (c) 2006, 2008-2010, 2013-2017, 2019, Arvid Norberg
Copyright (c) 2015, Thomas Yuan
Copyright (c) 2016-2017, Alden Torres
Copyright (c) 2017, Pavel Pimenov
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

#include <libtorrent/kademlia/tau_find_data.hpp>
#include <libtorrent/kademlia/node.hpp>
#include <libtorrent/kademlia/dht_observer.hpp>
#include <libtorrent/io.hpp>
#include <libtorrent/socket.hpp>
#include <libtorrent/socket_io.hpp>

#ifndef TORRENT_DISABLE_LOGGING
#include <libtorrent/hex.hpp> // to_hex
#endif

namespace libtorrent { namespace dht {

void tau_find_data_observer::reply(msg const& m)
{
	bdecode_node const r = m.message.dict_find_dict("r");
	if (!r)
	{
#ifndef TORRENT_DISABLE_LOGGING
		get_observer()->log(dht_logger::traversal, "[%u] missing response dict"
			, algorithm()->id());
#endif
		timeout();
		return;
	}

	bdecode_node const id = r.dict_find_string("id");
	if (!id || id.string_length() != 20)
	{
#ifndef TORRENT_DISABLE_LOGGING
		get_observer()->log(dht_logger::traversal, "[%u] invalid id in response"
			, algorithm()->id());
#endif
		timeout();
		return;
	}
	bdecode_node const token = r.dict_find_string("token");
	if (token)
	{
        /*
		static_cast<find_data*>(algorithm())->got_write_token(
			node_id(id.string_ptr()), token.string_value().to_string());
         */

        // Modified by TAU community.
        static_cast<tau_find_data*>(algorithm())->got_write_token(
            this->shared_from_this(), node_id(id.string_ptr()), token.string_value().to_string());
	}

	traversal_observer::reply(m);
	done();
}

/*
find_data::find_data(
	node& dht_node
	, node_id const& target
	, nodes_callback ncallback)
	: traversal_algorithm(dht_node, target)
	, m_nodes_callback(std::move(ncallback))
	, m_done(false)
{
}
*/

// Modified by TAU community.
tau_find_data::tau_find_data(
    node& dht_node
    , node_id const& target
    , nodes_callback ncallback
    , token_callback tcallback)
    : traversal_algorithm(dht_node, target)
    , m_nodes_callback(std::move(ncallback))
    , m_token_callback(std::move(tcallback))
    , m_done(false)
{
}


void tau_find_data::start()
{
    // first of all, init 'm_branch_factor'
    init();

    // if the user didn't add seed-nodes manually, grab alpha
    // nodes from routing table.
    if (m_results.empty())
    {
        std::vector<node_entry> nodes;

        m_node.m_table.find_node(target(), nodes,
            routing_table::include_failed, m_branch_factor);

        for (auto const& n : nodes)
        {
            add_entry(n.id, n.ep(), observer::flag_initial);
        }
    }

	// traversal_algorithm::start();

    // in case the routing table is empty, use the
    // router nodes in the table
    if (m_results.size() < m_branch_factor)
    {
        add_router_entries();
    }

    bool const is_done = add_requests();
    if (is_done) done();
}

bool tau_find_data::add_requests()
{
    if (m_done) return true;

    // this only counts outstanding requests at the top of the
    // target list. This is <= m_invoke count. m_invoke_count
    // is the total number of outstanding requests, including
    // old ones that may be waiting on nodes much farther behind
    // the current point we've reached in the search.
    int outstanding = 0;

    // Find the first node that hasn't already been queried.
    // and make sure that the 'm_branch_factor' top nodes
    // stay queried at all times (obviously ignoring failed nodes)
    // and without surpassing the 'result_target' nodes (i.e. k=8)
    // this is a slight variation of the original paper which instead
    // limits the number of outstanding requests, this limits the
    // number of good outstanding requests. It will use more traffic,
    // but is intended to speed up lookups
    for (auto i = m_results.begin()
        , end(m_results.end()); i != end
        && m_invoke_count <= 2 * m_branch_factor;
        ++i)
    {
        observer* o = i->get();
        if (o->flags & observer::flag_alive)
        {
            TORRENT_ASSERT(o->flags & observer::flag_queried);
            continue;
        }
        if (o->flags & observer::flag_queried)
        {
            // if it's queried, not alive and not failed, it
            // must be currently in flight
            if (!(o->flags & observer::flag_failed))
                ++outstanding;

            continue;
        }

#ifndef TORRENT_DISABLE_LOGGING
        dht_observer* logger = get_node().observer();
        if (logger != nullptr && logger->should_log(dht_logger::traversal))
        {
            logger->log(dht_logger::traversal
                , "[%u] INVOKE nodes-left: %d top-invoke-count: %d "
                "invoke-count: %d branch-factor: %d "
                "distance: %d id: %s addr: %s type: %s"
                , m_id, int(m_results.end() - i), outstanding, int(m_invoke_count)
                , int(m_branch_factor)
                , distance_exp(m_target, o->id()), aux::to_hex(o->id()).c_str()
                , print_address(o->target_addr()).c_str(), name());
        }
#endif

        o->flags |= observer::flag_queried;
        if (invoke(*i))
        {
            TORRENT_ASSERT(m_invoke_count < std::numeric_limits<std::int8_t>::max());
            ++m_invoke_count;
            ++outstanding;
        }
        else
        {
            o->flags |= observer::flag_failed;
        }
    }

    // if invoke count is 0, it means we didn't even find 'k'
    // working nodes, we still have to terminate though.
    return outstanding == 0 || m_invoke_count == 0;
}

void tau_find_data::finished(observer_ptr o)
{
#if TORRENT_USE_ASSERTS
    auto i = std::find(m_results.begin(), m_results.end(), o);
    TORRENT_ASSERT(i != m_results.end() || m_results.size() == 100);
#endif

    TORRENT_ASSERT(o->flags & observer::flag_queried);
    o->flags |= observer::flag_alive;

    ++m_responses;

    bool const is_done = add_requests();
    if (is_done) done();
}

void tau_find_data::failed(observer_ptr o, traversal_flags_t const flags)
{
    // don't tell the routing table about
    // node ids that we just generated ourself
    if (!(o->flags & observer::flag_no_id))
        m_node.m_table.node_failed(o->id(), o->target_ep());

    if (m_results.empty()) return;

    TORRENT_ASSERT(o->flags & observer::flag_queried);
    if (flags & short_timeout)
    {
        // short timeout means that it has been more than
        // two seconds since we sent the request, and that
        // we'll most likely not get a response. But, in case
        // we do get a late response, keep the handler
        // around for some more, but open up the slot
        // by increasing the branch factor
        if (!(o->flags & observer::flag_short_timeout)
            && m_branch_factor < std::numeric_limits<std::int8_t>::max())
        {
            o->flags |= observer::flag_short_timeout;
        }
#ifndef TORRENT_DISABLE_LOGGING
        log_timeout(o, "1ST_");
#endif
    }
    else
    {
        o->flags |= observer::flag_failed;

#ifndef TORRENT_DISABLE_LOGGING
        log_timeout(o,"");
#endif

        ++m_timeouts;
    }

    bool const is_done = add_requests();
    if (is_done) done();
}

/*
void find_data::got_write_token(node_id const& n, std::string write_token)
{
#ifndef TORRENT_DISABLE_LOGGING
	auto logger = get_node().observer();
	if (logger != nullptr && logger->should_log(dht_logger::traversal))
	{
		logger->log(dht_logger::traversal
			, "[%u] adding write token '%s' under id '%s'"
			, id(), aux::to_hex(write_token).c_str()
			, aux::to_hex(n).c_str());
	}
#endif
	m_write_tokens[n] = std::move(write_token);
}
*/

// Modified by TAU community.
void tau_find_data::got_write_token(observer_ptr o, node_id const& n, std::string write_token)
{
#ifndef TORRENT_DISABLE_LOGGING
    auto logger = get_node().observer();
    if (logger != nullptr && logger->should_log(dht_logger::traversal))
    {
        logger->log(dht_logger::traversal
            , "[%u] adding write token '%s' under id '%s'"
            , id(), aux::to_hex(write_token).c_str()
            , aux::to_hex(o->id()).c_str());
    }
#endif
    if (m_token_callback) {
        m_token_callback(std::make_pair(node_entry(o->id(), o->target_ep()), write_token));
    }

    m_write_tokens[n] = std::move(write_token);
}

observer_ptr tau_find_data::new_observer(udp::endpoint const& ep
	, node_id const& id)
{
	auto o = m_node.m_rpc.allocate_observer<tau_find_data_observer>(self(), ep, id);
#if TORRENT_USE_ASSERTS
	if (o) o->m_in_constructor = false;
#endif
	return o;
}

char const* tau_find_data::name() const { return "tau_find_data"; }

void tau_find_data::done()
{
	m_done = true;

#ifndef TORRENT_DISABLE_LOGGING
	auto logger = get_node().observer();
	if (logger != nullptr)
	{
		logger->log(dht_logger::traversal, "[%u] %s DONE"
			, id(), name());
	}
#endif

	std::vector<std::pair<node_entry, std::string>> results;
	int num_results = m_node.m_table.bucket_size();
	for (auto i = m_results.begin()
		, end(m_results.end()); i != end && num_results > 0; ++i)
	{
		observer_ptr const& o = *i;
		if (!(o->flags & observer::flag_alive))
		{
#ifndef TORRENT_DISABLE_LOGGING
			if (logger != nullptr && logger->should_log(dht_logger::traversal))
			{
				logger->log(dht_logger::traversal, "[%u] not alive: %s"
					, id(), print_endpoint(o->target_ep()).c_str());
			}
#endif
			continue;
		}
		auto j = m_write_tokens.find(o->id());
		if (j == m_write_tokens.end())
		{
#ifndef TORRENT_DISABLE_LOGGING
			if (logger != nullptr && logger->should_log(dht_logger::traversal))
			{
				logger->log(dht_logger::traversal, "[%u] no write token: %s"
					, id(), print_endpoint(o->target_ep()).c_str());
			}
#endif
			continue;
		}
		results.emplace_back(node_entry(o->id(), o->target_ep()), j->second);
#ifndef TORRENT_DISABLE_LOGGING
		if (logger != nullptr && logger->should_log(dht_logger::traversal))
		{
			logger->log(dht_logger::traversal, "[%u] %s"
				, id(), print_endpoint(o->target_ep()).c_str());
		}
#endif
		--num_results;
	}

	if (m_nodes_callback) m_nodes_callback(results);

	traversal_algorithm::done();
}

} } // namespace libtorrent::dht
