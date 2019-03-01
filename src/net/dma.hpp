// -*- mode:c++; c-style:k&r; c-basic-offset:4; indent-tabs-mode: nil; -*-
// vi:set et cin sw=4 cino=>se0n0f0{0}0^0\:0=sl1g0hspst0+sc3C0/0(0u0U0w0m0:

#ifndef __DMA_HPP__
#define __DMA_HPP__

#include <iostream>
#include <boost/shared_ptr.hpp>
#include "cstdint.hpp"
#include "virtual_queue.hpp"
#include "bridge_channel_alloc.hpp"
#include "logger.hpp"
#include "statistics.hpp"

using namespace std;


class dma_channel_id {
public:
    dma_channel_id(uint32_t new_id) throw();
    uint32_t get_numeric_id() const throw();
    friend ostream &operator<<(ostream &, const dma_channel_id &);
private:
    explicit dma_channel_id() throw();
    uint32_t id;
};

inline dma_channel_id::dma_channel_id(uint32_t new_id) throw() : id(new_id) {}

inline uint32_t dma_channel_id::get_numeric_id() const throw() { return id; }

ostream &operator<<(ostream &, const dma_channel_id &);

ostream &operator<<(ostream &, const boost::tuple<node_id, dma_channel_id> &);

class dma_channel {
public:
    virtual ~dma_channel();
    bool busy() const throw();
    const dma_channel_id &get_id() const throw();
protected:
    const boost::tuple<node_id, dma_channel_id> id;
    const uint32_t bandwidth;
    boost::shared_ptr<virtual_queue> vq;
    bool started;
    flow_id flow;
    uint32_t remaining_flits;
    void *mem;
    boost::shared_ptr<tile_statistics> stats;
    logger &log;
protected:
    dma_channel(node_id n_id, dma_channel_id d_id, unsigned bw,
                boost::shared_ptr<virtual_queue> vq, boost::shared_ptr<tile_statistics> stats,
                logger &log) throw();
private:
    dma_channel(); // not defined
    dma_channel(const dma_channel &); // not defined
};

class ingress_dma_channel : public dma_channel {
public:
    ingress_dma_channel(node_id n_id, dma_channel_id d_id, unsigned bw,
                        boost::shared_ptr<virtual_queue> vq,
                        boost::shared_ptr<tile_statistics> stats, logger &log) throw();
    virtual ~ingress_dma_channel();
    void receive(void *dst, packet_id *pid, uint32_t num_flits)  ;
    bool has_waiting_flow() const throw();
    uint32_t get_flow_length() const  ;
    flow_id get_flow_id() const  ;
    void tick_positive_edge()  ;
    void tick_negative_edge()  ;
private:
    ingress_dma_channel(); // not defined
    ingress_dma_channel(const ingress_dma_channel &); // not defined
private:
    packet_id *pid_p;
};

class egress_dma_channel : public dma_channel {
public:
    egress_dma_channel(node_id n_id, dma_channel_id d_id, unsigned bw,
                       boost::shared_ptr<virtual_queue> vq,
                       boost::shared_ptr<bridge_channel_alloc> vca,
                       boost::shared_ptr<tile_statistics> stats, logger &log) throw();
    virtual ~egress_dma_channel();
    void send(flow_id flow, void *src, uint32_t num_flits,
              const packet_id &pid, bool count_in_stats)  ;
    void tick_positive_edge()  ;
    void tick_negative_edge()  ;
private:
    packet_id pid;
    bool count_in_stats;
    boost::shared_ptr<bridge_channel_alloc> vc_alloc;
private:
    egress_dma_channel(); // not defined
    egress_dma_channel(const egress_dma_channel &); // not defined
};

inline bool dma_channel::busy() const throw() { return remaining_flits > 0; }

inline const dma_channel_id &dma_channel::get_id() const throw() {
    return id.get<1>();
}

#endif // __DMA_HPP__

