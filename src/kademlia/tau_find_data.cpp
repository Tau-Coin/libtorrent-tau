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
#include <libtorrent/random.hpp>
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
	init();
}

void tau_find_data::set_branch_factor(int branch_factor)
{
     m_branch_factor = aux::numeric_cast<std::int8_t>(branch_factor);
}

void tau_find_data::start()
{
    // if the user didn't add seed-nodes manually, grab alpha
    // nodes from routing table.
    if (m_results.empty())
    {
        std::vector<node_entry> nodes;

        m_node.m_table.find_node(target(), nodes, routing_table::include_failed);

        for (auto const& n : nodes)
        {
            add_entry(n.id, n.ep(), observer::flag_initial);
        }
    }

	// traversal_algorithm::start();

	if (m_results.size() < 3) {
		add_router_entries();
	}

	bool const is_done = add_requests();
	if (is_done) done();
}

/*
bool tau_find_data::add_requests()
{
    if (m_done) return true;

    // invoke count limit: alpha + beta
    constexpr int beta = 1;

    // this only counts outstanding requests at the top of the
    // target list. This is <= m_invoke count. m_invoke_count
    // is the total number of outstanding requests, including
    // old ones that may be waiting on nodes much farther behind
    // the current point we've reached in the search.
    int outstanding = 0;

    // this only counts invoking requests for once calling this function.
    int invokes = 0;

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
        && invokes < 1
        && m_invoke_count < (beta + m_branch_factor);
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
            ++invokes;
        }
        else
        {
            o->flags |= observer::flag_failed;
        }
    }

    // 1. m_responses + m_timeouts >= (8 + m_branch_factor)
    //     we have invoked enough requests and all requests were all processed.
    // 2. m_timeouts == m_invoke_count
    //     all the requests were timeout.
    // 3. m_responses + m_timeouts = m_results.size()
    //     the total size of m_results is less than (8 + m_branch_factor)
    //     and all requests were all processed.
    // 4. if invoke count is 0, it means we didn't even find any
    //     working nodes, we still have to terminate though.
    return (outstanding == 0 && (m_responses + m_timeouts >= (beta + m_branch_factor)))
            || (outstanding == 0 && m_invoke_count != 0 && m_timeouts == m_invoke_count)
            || (outstanding == 0 && m_invoke_count != 0
                    && (m_responses + m_timeouts
                            == aux::numeric_cast<std::int16_t>(m_results.size())))
            || m_invoke_count == 0;
}
*/

bool tau_find_data::add_requests()
{
    if (m_done) return true;

    // invoke count limit: alpha + beta
    constexpr int beta = 1;

    // this only counts outstanding requests at the top of the
    // target list. This is <= m_invoke count. m_invoke_count
    // is the total number of outstanding requests, including
    // old ones that may be waiting on nodes much farther behind
    // the current point we've reached in the search.
    int outstanding = 0;

    int invoke_range = beta + m_branch_factor > 8 ? int(beta + m_branch_factor) : 8;

    int has_invoked = 0;

    // if the first 'invoke_range' nodes are all invoked, just return true;
    int j = 0;

    if (m_invoke_count < (beta + m_branch_factor))
    {
        for (auto i = m_results.begin(), end(m_results.end());
            i != end && j < invoke_range;
            ++i)
        {
            j++;

            observer* o = i->get();
            if (o->flags & observer::flag_alive)
            {
                TORRENT_ASSERT(o->flags & observer::flag_queried);
                has_invoked++;
                continue;
            }
            if (o->flags & observer::flag_queried)
            {
                // if it's queried, not alive and not failed, it
                // must be currently in flight
                if (!(o->flags & observer::flag_failed))
                    ++outstanding;

                has_invoked++;
                continue;
            }
        }

        if (outstanding == 0
            && (has_invoked >= invoke_range || has_invoked == int(m_results.size())))
        {
            return true;
        }
        else if (outstanding != 0
            && (has_invoked >= invoke_range || has_invoked == int(m_results.size())))
        {
            return false;
        }
    }

    // this only counts invoking requests for once calling this function.
    int invokes = 0;

    std::uint32_t random_max = int(m_results.size()) >= invoke_range ?
        std::uint32_t(invoke_range) - 1 : std::uint32_t(m_results.size()) - 1;

    // Find the first node that hasn't already been queried.
    // and make sure that the 'm_branch_factor' top nodes
    // stay queried at all times (obviously ignoring failed nodes)
    // and without surpassing the 'result_target' nodes (i.e. k=8)
    // this is a slight variation of the original paper which instead
    // limits the number of outstanding requests, this limits the
    // number of good outstanding requests. It will use more traffic,
    // but is intended to speed up lookups
    while (invokes < 1
        && m_invoke_count < (beta + m_branch_factor)
        && m_invoke_count < (aux::numeric_cast<std::int16_t>(m_results.size()))
        && m_responses + m_timeouts + outstanding
                < (aux::numeric_cast<std::int16_t>(m_results.size()))
    )
    {
        // generate random
        std::uint32_t const r = random(random_max);
        observer* o = (m_results.begin() + r)->get();

        if (o->flags & observer::flag_alive)
        {
            TORRENT_ASSERT(o->flags & observer::flag_queried);
            continue;
        }

        if (o->flags & observer::flag_queried)
        {
            continue;
        }

#ifndef TORRENT_DISABLE_LOGGING
        dht_observer* logger = get_node().observer();
        if (logger != nullptr && logger->should_log(dht_logger::traversal))
        {
            logger->log(dht_logger::traversal
                , "[%u] INVOKE node-index: %d top-invoke-count: %d "
                "invoke-count: %d branch-factor: %d "
                "distance: %d id: %s addr: %s type: %s"
                , m_id, r, outstanding, int(m_invoke_count)
                , int(m_branch_factor)
                , distance_exp(m_target, o->id()), aux::to_hex(o->id()).c_str()
                , print_address(o->target_addr()).c_str(), name());
        }
#endif

        o->flags |= observer::flag_queried;
        if (invoke(*(m_results.begin() + r)))
        {
            TORRENT_ASSERT(m_invoke_count < std::numeric_limits<std::int8_t>::max());
            ++outstanding;
            ++invokes;
        }
        else
        {
            o->flags |= observer::flag_failed;

            if (!(o->flags & observer::flag_no_id))
                m_node.m_table.node_failed(o->id(), o->target_ep());
        }

        ++m_invoke_count;
    }

    // 1. m_responses + m_timeouts >= (8 + m_branch_factor)
    //     we have invoked enough requests and all requests were all processed.
    // 2. m_timeouts == m_invoke_count
    //     all the requests were timeout.
    // 3. m_responses + m_timeouts = m_results.size()
    //     the total size of m_results is less than (8 + m_branch_factor)
    //     and all requests were all processed.
    // 4. if invoke count is 0, it means we didn't even find any
    //     working nodes, we still have to terminate though.
    return (outstanding == 0 && (m_responses + m_timeouts >= (beta + m_branch_factor)))
            || (outstanding == 0 && m_invoke_count != 0 && m_timeouts == m_invoke_count)
            || (outstanding == 0 && m_invoke_count != 0
                    && (m_responses + m_timeouts
                            == aux::numeric_cast<std::int16_t>(m_results.size())))
            || m_invoke_count == 0;
}

void tau_find_data::traverse(node_id const& id, udp::endpoint const& addr)
{
    if (m_done) return;

#ifndef TORRENT_DISABLE_LOGGING
    dht_observer* logger = get_node().observer();
    if (logger != nullptr && logger->should_log(dht_logger::traversal) && id.is_all_zeros())
    {
        logger->log(dht_logger::traversal
            , "[%u] WARNING node returned a list which included a node with id 0"
            , m_id);
    }
#endif

    // let the routing table know this node may exist
    m_node.m_table.heard_about(id, addr);

    // add
    std::set<node_entry> rb;
    m_node.m_table.get_replacements(rb);

    node_entry * existing;
    std::tie(existing, std::ignore, std::ignore) = m_node.m_table.find_node(addr);

#ifndef TORRENT_DISABLE_LOGGING
	//dht_observer* logger = get_node().observer();
	if (logger != nullptr && logger->should_log(dht_logger::traversal))
	{
		if (existing != nullptr)
		{
            logger->log(dht_logger::traversal
                , "[%u] NODE id: %s addr: %s distance: %d allow-invoke: %s type: %s"
                , m_id, aux::to_hex(id).c_str(), print_endpoint(addr).c_str()
                , distance_exp(m_target, id)
                , existing->allow_invoke() ? "true" : "false", name());
        }
		else
		{
            logger->log(dht_logger::traversal
                , "[%u] NODE id: %s addr: %s not found, type: %s"
                , m_id, aux::to_hex(id).c_str(), print_endpoint(addr).c_str()
                , name());
		}
	}
#endif

	if (existing != nullptr && existing->allow_invoke())
	{
		add_entry(id, addr, {});
	}
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

        node_entry * existing;
        std::tie(existing, std::ignore, std::ignore) = m_node.m_table.find_node(o->target_ep());
        if (existing != nullptr)
        {
            existing->invoke_failed();
        }
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
