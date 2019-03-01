// -*- mode:c++; c-style:k&r; c-basic-offset:4; indent-tabs-mode: nil; -*-
// vi:set et cin sw=4 cino=>se0n0f0{0}0^0\:0=sl1g0hspst0+sc3C0/0(0u0U0w0m0:

#ifndef __SET_CHANNEL_ALLOC_HPP__
#define __SET_CHANNEL_ALLOC_HPP__

#include <map>
#include <boost/tuple/tuple.hpp>
#include <boost/tuple/tuple_comparison.hpp>
#include "channel_alloc.hpp"
#include "egress.hpp"
#include "random.hpp"

class set_channel_alloc : public channel_alloc {
public:
    set_channel_alloc(node_id src, bool one_queue_per_flow,
                      bool one_flow_per_queue,
                      boost::shared_ptr<tile_statistics> stats,
                      logger &log, boost::shared_ptr<random_gen> ran) throw();
    virtual ~set_channel_alloc() throw();
    virtual void allocate()  ;
    virtual void add_egress(boost::shared_ptr<egress> egress)  ;
    void add_route(const node_id &src, const flow_id &f, const node_id &dst, 
                   const flow_id &nf,
                   const vector<boost::tuple<virtual_queue_id,double> > &qs)
         ;
private:
    typedef map<node_id, boost::shared_ptr<egress> > egresses_t;
    egresses_t egresses;
    typedef boost::tuple<node_id, flow_id, node_id, flow_id> route_query_t;
    typedef vector<boost::tuple<boost::shared_ptr<virtual_queue>, double> > route_queues_t;
    typedef map<route_query_t, route_queues_t> routes_t;
    routes_t routes;
    boost::shared_ptr<tile_statistics> stats;
    boost::shared_ptr<random_gen> ran;
    typedef vector<uint32_t> va_req_t;
};

#endif // __SET_CHANNEL_ALLOC_HPP__
