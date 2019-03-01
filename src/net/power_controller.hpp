// -*- mode:c++; c-style:k&r; c-basic-offset:4; indent-tabs-mode: nil; -*-
// vi:set et cin sw=4 cino=>se0n0f0{0}0^0\:0=sl1g0hspst0+sc3C0/0(0u0U0w0m0:

#ifndef __POWER_CONTROLLER_HPP__
#define __POWER_CONTROLLER_HPP__

#include <map>
#include <iostream>
#include <boost/shared_ptr.hpp>
#include "logger.hpp"
#include "vcd.hpp"
#include "ingress.hpp"
#include "egress.hpp"
#include "statistics.hpp"

using namespace std;


class power_controller {
public:
    explicit power_controller(node_id id, boost::shared_ptr<tile_statistics> stats,
                              boost::shared_ptr<vcd_writer> vcd, logger &log) throw();
    const node_id &get_id() const throw();
    void add_ingress(node_id src, boost::shared_ptr<ingress> ingress)  ;
    void add_egress(node_id dst, boost::shared_ptr<egress> egress)  ;
    void adjust_power()  ;
private:
    const node_id id;
    typedef map<node_id, boost::shared_ptr<ingress> > ingresses_t;
    ingresses_t ingresses;
    boost::shared_ptr<tile_statistics> stats;
    boost::shared_ptr<vcd_writer> vcd;
    logger &log;
};

#endif // __POWER_CONTROLLER_HPP__

