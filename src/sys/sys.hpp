// -*- mode:c++; c-style:k&r; c-basic-offset:4; indent-tabs-mode: nil; -*-
// vi:set et cin sw=4 cino=>se0n0f0{0}0^0\:0=sl1g0hspst0+sc3C0/0(0u0U0w0m0:

#ifndef __SYS_HPP__
#define __SYS_HPP__

#include <vector>
#include <map>
#include <utility>
#include <fstream>
#include <boost/shared_ptr.hpp>
#include <boost/thread.hpp>
#include "cstdint.hpp"
#include "vcd.hpp"
#include "statistics.hpp"
#include "logger.hpp"
#include "tile.hpp"
#include "random.hpp"

using namespace std;


class sys {
public:
    sys(const uint64_t &time, boost::shared_ptr<ifstream> image, 
        const uint64_t &stats_t0,
        boost::shared_ptr<vector<string> > event_files,
        boost::shared_ptr<vector<string> > memtrace_files,
        boost::shared_ptr<vcd_writer> vcd,
        logger &log, uint32_t seed, bool use_graphite_inj,
        uint64_t test_flags)  ;
    boost::shared_ptr<system_statistics> get_statistics() const throw();
    boost::shared_ptr<tile_statistics> get_statistics_tile(tile_id t) const throw();
    bool work_tbd_darsim()  ;
    bool nothing_to_offer()  ;
    uint64_t advance_time()  ;
    uint64_t get_time() const throw();
    void tick_positive_edge()  ;
    void tick_negative_edge()  ;
    void fast_forward_time(uint64_t new_time) throw();
    bool is_drained() const throw();
    // parallel support
    uint32_t get_num_tiles() const throw();
    uint64_t get_time_tile(tile_id tile) const throw();
    void tick_positive_edge_tile(tile_id tile)  ;
    void tick_negative_edge_tile(tile_id tile)  ;
    void fast_forward_time_tile(tile_id tile, uint64_t new_time) throw();
    uint64_t advance_time_tile(tile_id tile)  ;
    bool is_drained_tile(tile_id tile) const throw();
private:
    typedef vector<boost::shared_ptr<tile> > tiles_t;

private:
    uint64_t sys_time;
    typedef enum {
        TF_RANDOMIZE_NODE_ORDER = 1
    } test_flags_t;

    tiles_t tiles;
    vector<tile_id> tile_indices;

    boost::shared_ptr<system_statistics> stats;
    logger &log;
    boost::shared_ptr<random_gen> sys_rand;
    uint64_t test_flags;
};

#endif // __SYS_HPP__
