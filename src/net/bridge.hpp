// -*- mode:c++; c-style:k&r; c-basic-offset:4; indent-tabs-mode: nil; -*-
// vi:set et cin sw=4 cino=>se0n0f0{0}0^0\:0=sl1g0hspst0+sc3C0/0(0u0U0w0m0:

#ifndef __BRIDGE_HPP__
#define __BRIDGE_HPP__

#include <vector>
#include <map>
#include <utility>
#include <iostream>
#include <fstream>
#include <boost/shared_ptr.hpp>
#include "error.hpp"
#include "logger.hpp"
#include "id_factory.hpp"
#include "statistics.hpp"
#include "virtual_queue.hpp"
#include "router.hpp"
#include "bridge_channel_alloc.hpp"
#include "node.hpp"
#include "dma.hpp"

using namespace std;


class node;

class bridge {
public:
    explicit bridge(boost::shared_ptr<node> n,
                    boost::shared_ptr<bridge_channel_alloc> bridge_vca,
                    const set<virtual_queue_id> &node_vq_ids, unsigned n2b_bw,
                    const set<virtual_queue_id> &bridge_vq_ids, unsigned b2n_bw,
                    unsigned flits_per_queue, unsigned b2n_bw_to_xbar,
                    bool one_queue_per_flow, bool one_flow_per_queue,
                    boost::shared_ptr<id_factory<packet_id> > packet_id_factory,
                    boost::shared_ptr<tile_statistics> stats,
                    boost::shared_ptr<vcd_writer> vcd,
                    logger &new_log)  ;
    const node_id &get_id() const throw();
    uint32_t get_transmission_done(uint32_t transmission) throw();
    uint32_t get_waiting_queues() throw();
    uint32_t get_queue_flow_id(uint32_t queue)  ;
    uint32_t get_queue_length(uint32_t queue)  ;
    // both() send and receive() return 0 on failure (no transfer started)
    // or transfer ID which can be used to check for completion later
    // which can be used later to check status
    uint32_t send(uint32_t flow_id, void *src, uint32_t num_flits,
                  bool count_in_stats)  ;
    uint32_t send(uint32_t flow_id, void *src, uint32_t num_flits,
                  packet_id pid, bool count_in_stats)  ;
    uint32_t receive(void *dst, uint32_t queue, uint32_t num_flits,
                     packet_id *pid=NULL)  ;
    void tick_positive_edge()  ;
    void tick_negative_edge()  ;
    bool is_drained() const throw();
    boost::shared_ptr<egress> get_egress() throw();
    boost::shared_ptr<ingress> get_ingress() throw();
    boost::shared_ptr<vector<uint32_t> > get_ingress_queue_ids() throw();
private:
    const boost::shared_ptr<ingress_dma_channel> &get_ingress_dma(uint32_t) const
         ;
    const boost::shared_ptr<egress_dma_channel> &get_egress_dma(uint32_t) const
         ;
private:
    typedef map<virtual_queue_id, boost::shared_ptr<ingress_dma_channel> >
        ingress_dmas_t;
    typedef map<virtual_queue_id, boost::shared_ptr<egress_dma_channel> >
        egress_dmas_t;

    const node_id id;
    boost::shared_ptr<node> target;
    boost::shared_ptr<bridge_channel_alloc> vc_alloc;
    boost::shared_ptr<ingress> incoming;
    boost::shared_ptr<egress> outgoing;
    ingress_dmas_t ingress_dmas;
    egress_dmas_t egress_dmas;
    vector<virtual_queue_id> vqids; // [0..31] queue index -> real queue id
    boost::shared_ptr<id_factory<packet_id> > packet_id_factory;
    boost::shared_ptr<tile_statistics> stats;
    boost::shared_ptr<vcd_writer> vcd;
    logger &log;
private:
    bridge(); // not defined
    bridge(const bridge &); // not defined
};

inline const node_id &bridge::get_id() const throw() { return id; }

#endif // __BRIDGE_HPP__

