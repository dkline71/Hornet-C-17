// -*- mode:c++; c-style:k&r; c-basic-offset:4; indent-tabs-mode: nil; -*-
// vi:set et cin sw=4 cino=>se0n0f0{0}0^0\:0=sl1g0hspst0+sc3C0/0(0u0U0w0m0:

#ifndef __SET_ROUTER_HPP__
#define __SET_ROUTER_HPP__

#include <map>
#include "boost/tuple/tuple.hpp"
#include "boost/tuple/tuple_comparison.hpp"
#include "router.hpp"
#include "random.hpp"



class set_router : public router {
public:
    set_router(node_id id, logger &log, boost::shared_ptr<random_gen> ran) throw();
    virtual ~set_router() throw();
    virtual void route()  ;
    virtual void add_egress(boost::shared_ptr<egress> egress)  ;
    void add_route(const node_id &prev, const flow_id &flow,
                   const vector<boost::tuple<node_id,flow_id,double> > &next_hops)
         ;
private:
    typedef boost::tuple<node_id, flow_id> route_query_t;
    // doubles in routes are cumulative propensities
    typedef vector<boost::tuple<node_id,flow_id,double> > route_nodes_t;
    typedef map<route_query_t, route_nodes_t> routes_t;
    routes_t routes;
    boost::shared_ptr<random_gen> ran;
    typedef map<node_id, boost::shared_ptr<egress> > egresses_t;
    egresses_t egresses;
};

#endif // __SET_ROUTER_HPP__

