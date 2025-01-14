// -*- mode:c++; c-style:k&r; c-basic-offset:4; indent-tabs-mode: nil; -*-
// vi:set et cin sw=4 cino=>se0n0f0{0}0^0\:0=sl1g0hspst0+sc3C0/0(0u0U0w0m0:

#include <string>
#include <utility>
#include <set>
#include <cassert>
#include <boost/static_assert.hpp>
#include <boost/tuple/tuple.hpp>
#include <boost/tuple/tuple_comparison.hpp>
#include <boost/bind.hpp>
#include <boost/function.hpp>
#include "cstdint.hpp"
#include "endian.hpp"
#include "node.hpp"
#include "bridge.hpp"
#include "set_router.hpp"
#include "set_channel_alloc.hpp"
#include "set_bridge_channel_alloc.hpp"
#include "cpu.hpp"
#include "injector.hpp"
#include "event_parser.hpp"
#include "id_factory.hpp"
#include "sys.hpp"
#include "ginj.hpp"
/* cores */
#include "memtraceCore.hpp"
#include "mcpu.hpp"
/* memories */
#include "privateSharedMSI.hpp"
#include "privateSharedLCC.hpp"

typedef enum {
    CORE_MIPS_MPI = 0,
    CORE_INJECTOR= 1,
    CORE_MEMTRACE= 2,
    CORE_MCPU = 3
} sys_core_type_t;

static uint32_t read_word(boost::shared_ptr<ifstream> in)   {
    uint32_t word = 0xdeadbeef;
    in->read((char *) &word, 4);
    if (in->bad()) throw err_bad_mem_img();
    word = endian(word);
    return word;
}

static double read_double(boost::shared_ptr<ifstream> in)   {
    uint64_t word = 0xdeadbeefdeadbeefULL;
    in->read((char *) &word, 8);
    if (in->bad()) throw err_bad_mem_img();
    word = endian(word);
    BOOST_STATIC_ASSERT(sizeof(double) == 8);
    double d;
    memcpy(&d, &word, sizeof(double));
    return d;
}

static string read_string(boost::shared_ptr<ifstream> in)   {
    char buf[256];
    in->read((char *) &buf, 256);
    if (in->bad()) throw err_bad_mem_img();
    string s(&buf[1], (int) buf[0]);
    return s;
}

static void read_mem(uint8_t *ptr, uint32_t num_bytes,
                     boost::shared_ptr<ifstream> in)   {
    assert(ptr);
    in->read((char *) ptr, num_bytes);
    if (in->bad()) throw err_bad_mem_img();
}

static void create_memtrace_threads(boost::shared_ptr<vector<string> > files, boost::shared_ptr<memtraceThreadPool> pool, int num_cores, 
                                    const uint64_t &system_time, logger &log, boost::shared_ptr<system_statistics> sys_stats) 
{
    if (!files) {
        return;
    }
    boost::shared_ptr<memtraceThreadStats> memth_stats = 
        boost::shared_ptr<memtraceThreadStats>(new memtraceThreadStats(system_time));
    for (vector<string>::const_iterator fi = files->begin(); fi != files->end(); ++fi) {
        ifstream input(fi->c_str());
        if (input.fail()) throw err_parse(*fi, "cannot open file");
        while (input.good()) {
            memtraceThread *thread;
            string line, word; 
            getline(input, line);
            istringstream l(line);
            l >> skipws;
            l >> word;
            if (strcmp(word.c_str(), "Thread") == 0) {
                uint32_t th_id, interval;
                int home;
                uint64_t addr, pc;
                char rw;
                try {
                    l >> hex >> th_id >> addr >> rw >> pc >> home >> interval;
                } catch (const err &e) {
                    assert(false);
                }

                /* Massaging traces */
                /* for now, force 32bit-aligned, one-word, shared accesses only */
                maddr_t maddr;
                addr = addr - addr % 4;
                maddr.space = 0;
                maddr.address = addr;
                home = home % num_cores;
                // TODO : support static CAT
                uint32_t word_count = 1; /* 4byte word is assumed */

                thread = pool->find(th_id);
                if (thread == NULL) {
                    thread = new memtraceThread(th_id, system_time, log);
                    boost::shared_ptr<memtraceThreadStatsPerThread> per_thread_stats=
                        boost::shared_ptr<memtraceThreadStatsPerThread>(new memtraceThreadStatsPerThread(th_id, system_time));
                    thread->set_per_thread_stats(per_thread_stats);
                    memth_stats->add_per_thread_stats(per_thread_stats);
                    pool->add_thread(thread);
                }
                if (interval > 0) {
                    thread->add_non_mem_inst(interval);
                }
                thread->add_mem_inst(1, (rw == 'W')? true : false, maddr, word_count); 
            }
        }
    }
    if (pool->size() > 0) {
        sys_stats->add_aux_statistics(memth_stats);
    }
}

sys::sys(const uint64_t &new_sys_time, boost::shared_ptr<ifstream> img,
         const uint64_t &stats_t0, boost::shared_ptr<vector<string> > events_files, boost::shared_ptr<vector<string> > memtrace_files,
         boost::shared_ptr<vcd_writer> vcd,
         logger &new_log, uint32_t seed, bool use_graphite_inj,
         uint64_t new_test_flags)  
    : sys_time(new_sys_time), stats(new system_statistics()), log(new_log),
      sys_rand(new random_gen(-1, seed++)), test_flags(new_test_flags) {
    uint32_t num_nodes = read_word(img);
    uint32_t network_width = read_word(img);
    LOG(log,2) << "creating system with " << num_nodes << " node"
               << (num_nodes == 1 ? "" : "s") << "..." << endl;
    boost::shared_ptr<flow_rename_table> flow_renames =
        boost::shared_ptr<flow_rename_table>(new flow_rename_table());
    for (uint32_t i = 0; i < num_nodes; ++i) {
        tile_indices.push_back(tile_id(i));
        tiles.push_back(boost::shared_ptr<tile>(new tile(tile_id(i), num_nodes,
                                                  sys_time, stats_t0,
                                                  flow_renames, log)));
        stats->add(i, tiles.back()->get_statistics());
    }
    typedef vector<boost::shared_ptr<pe> > pes_t;
    typedef vector<boost::shared_ptr<bridge> > bridges_t;
    typedef vector<boost::shared_ptr<node> > nodes_t;
    typedef map<boost::tuple<unsigned, unsigned>, boost::shared_ptr<arbiter> > arbiters_t;
    typedef vector<boost::shared_ptr<router> > routers_t;
    typedef vector<boost::shared_ptr<channel_alloc> > vcas_t;
    typedef vector<boost::shared_ptr<bridge_channel_alloc> > br_vcas_t;
    pes_t pes(num_nodes);
    nodes_t nodes(num_nodes);
    bridges_t bridges(num_nodes);
    arbiters_t arbiters;
    routers_t node_rts(num_nodes);
    vcas_t n_vcas(num_nodes);
    br_vcas_t br_vcas(num_nodes);
    typedef event_parser::injectors_t injectors_t;
    typedef event_parser::flow_starts_t flow_starts_t;
    boost::shared_ptr<injectors_t> injectors(new injectors_t(num_nodes));
    boost::shared_ptr<flow_starts_t> flow_starts(new flow_starts_t());

    vector<boost::shared_ptr<memtraceCore> > memtrace_cores;
    boost::shared_ptr<memtraceThreadPool> memtrace_thread_pool(new memtraceThreadPool());

    boost::shared_ptr<privateSharedMSIStats> private_shared_msi_stats = boost::shared_ptr<privateSharedMSIStats>();
    boost::shared_ptr<privateSharedLCCStats> private_shared_lcc_stats = boost::shared_ptr<privateSharedLCCStats>();

    boost::shared_ptr<dram> new_dram(new dram ());

    for (unsigned i = 0; i < num_nodes; ++i) {
        uint32_t id = read_word(img);
        uint32_t bytes_per_flit = read_word(img);
        if (id < 0 || id >= num_nodes) throw err_bad_mem_img();
        if (nodes[id]) throw err_bad_mem_img();
        boost::shared_ptr<tile> t = tiles[id];
        boost::shared_ptr<random_gen> ran(new random_gen(id, seed++));
        boost::shared_ptr<router> n_rt(new set_router(id, log, ran));
        uint32_t one_q_per_f = read_word(img);
        uint32_t one_f_per_q = read_word(img);
        uint32_t multi_path_routing = read_word(img);
        n_rt->set_multi_path_routing((router::multi_path_routing_t)multi_path_routing);
        boost::shared_ptr<channel_alloc>
            //n_vca(new set_channel_alloc(id, one_q_per_f, one_f_per_q, log, ran));
            n_vca(new set_channel_alloc(id, one_q_per_f, one_f_per_q, t->get_statistics(),
                  log, ran));//pengju
        n_rt->set_virtual_channel_alloc(n_vca);
        boost::shared_ptr<bridge_channel_alloc>
            b_vca(new set_bridge_channel_alloc(id, one_q_per_f, one_f_per_q,
                                               log, ran));
        uint32_t flits_per_q = read_word(img);
        boost::shared_ptr<node> n(new node(node_id(id), flits_per_q, n_rt, n_vca,
                                    t->get_statistics(), vcd, log, ran));
        nodes[id] = n;
        t->add(n);
        uint32_t n2b_bw = read_word(img);
        uint32_t b2n_bw = read_word(img);
        uint32_t b2n_xbar_bw = read_word(img);
        uint32_t b2n_num_queues = read_word(img);
        set<virtual_queue_id> b2n_queues;
        for (uint32_t q = 0; q < b2n_num_queues; ++q) {
            b2n_queues.insert(virtual_queue_id(read_word(img)));
        }
        uint32_t n2b_num_queues = read_word(img);
        set<virtual_queue_id> n2b_queues;
        for (uint32_t q = 0; q < n2b_num_queues; ++q) {
            n2b_queues.insert(virtual_queue_id(read_word(img)));
        }
        boost::shared_ptr<bridge> b(new bridge(n, b_vca,
                                        n2b_queues, n2b_bw, b2n_queues, b2n_bw,
                                        flits_per_q, b2n_xbar_bw,
                                        one_q_per_f, one_f_per_q,
                                        t->get_packet_id_factory(),
                                        t->get_statistics(), vcd, log));
        bridges[id] = b;
        t->add(b);
        boost::shared_ptr<set_bridge_channel_alloc> vca =
            static_pointer_cast<set_bridge_channel_alloc>(b_vca);
        boost::shared_ptr<ingress> b_n_i = n->get_ingress_from(b->get_id());
        const ingress::queues_t &b_n_i_qs = b_n_i->get_queues();
        for (ingress::queues_t::const_iterator qi = b_n_i_qs.begin();
                 qi != b_n_i_qs.end(); ++qi) {
                vca->add_queue(qi->second);
        }
        uint32_t core_type_word = read_word(img);
        sys_core_type_t core_type = static_cast<sys_core_type_t>(core_type_word);
        boost::shared_ptr<pe> p;

        switch (core_type) {
        case CORE_MIPS_MPI: {
            uint32_t mem_start = read_word(img);
            uint32_t mem_size = read_word(img);
            boost::shared_ptr<mem> m(new mem(id, mem_start, mem_size, log));
            read_mem(m->ptr(mem_start), mem_size, img);

            uint32_t cpu_entry_point = read_word(img);
            uint32_t cpu_stack_pointer = read_word(img);
            p = boost::shared_ptr<pe>(new cpu(pe_id(id), t->get_time(), m,
                                       cpu_entry_point,
                                       cpu_stack_pointer,
                                       t->get_statistics(),
                                       log));
            break;
        }
        case CORE_INJECTOR: {
            boost::shared_ptr<injector> inj(new injector(id, t->get_time(),
                                                  t->get_packet_id_factory(),
                                                  t->get_statistics(),
                                                  log, ran));
            boost::shared_ptr<ginj> g_inj(new ginj(id, t->get_time(),
                                            t->get_packet_id_factory(), 
                                            t->get_statistics(), log, ran));
            if (use_graphite_inj) {
               p = g_inj;
            }
            else {
               p = inj;
            }
            (*injectors)[id] = inj;
            break;
        }
        case CORE_MEMTRACE: {
            /* memtraceCore-specific cfgs*/
            uint32_t em_type_word = read_word(img);
            emType_t em_type = static_cast<emType_t>(em_type_word);
            uint32_t msg_queue_size = read_word(img);
            uint32_t mig_size_in_bytes = read_word(img);
            uint32_t max_threads = read_word(img);

            /* memory type */
            uint32_t memory_type_word = read_word(img);
            memoryType_t mem_type = static_cast<memoryType_t>(memory_type_word);

            /* dram controller location */
            uint32_t dram_location_word = read_word(img);
            dramLocationType_t dram_location = static_cast<dramLocationType_t>(dram_location_word);

            /* cat type */
            uint32_t cat_type_word = read_word(img);
            catType_t cat_type = static_cast<catType_t>(cat_type_word);

            /* cat latency */
            uint32_t cat_latency = read_word(img);

            /* cat allocation unit */
            uint32_t cat_allocation_unit_in_bytes = read_word(img);

            /* cat synch delay */
            uint32_t cat_synch_delay = read_word(img);

            /* cat number of ports 0:infinite */
            uint32_t cat_num_ports = read_word(img);

            boost::shared_ptr<cat> new_cat = boost::shared_ptr<cat>();
            switch(cat_type) {
            case CAT_STRIPE:
                new_cat = boost::shared_ptr<catStripe>(new catStripe(num_nodes, t->get_time(), cat_num_ports, 
                                                              cat_latency, cat_allocation_unit_in_bytes));
                break;
            case CAT_STATIC:
                new_cat = boost::shared_ptr<catStatic>(new catStatic(num_nodes, t->get_time(), cat_num_ports, 
                                                              cat_latency, cat_allocation_unit_in_bytes, cat_synch_delay));
                break;
            case CAT_FIRST_TOUCH:
                new_cat = boost::shared_ptr<catFirstTouch>(new catFirstTouch(num_nodes, t->get_time(), cat_num_ports, 
                                                                      cat_latency, cat_allocation_unit_in_bytes, cat_synch_delay));
                break;
            }

            /* dram controller */
            uint32_t dram_controller_latency = read_word(img);
            uint32_t offchip_oneway_latency = read_word(img);
            uint32_t dram_latency = read_word(img);
            uint32_t msg_header_size_in_words = read_word(img);
            uint32_t dc_max_requests_in_flight = read_word(img);
            uint32_t bandwidth_in_words_per_cycle = read_word(img);
            uint32_t address_size_in_bytes = read_word(img);

            /* cache configurations */
            boost::shared_ptr<memory> mem = boost::shared_ptr<memory>();

            switch (mem_type) {
            case MEM_PRIVATE_SHARED_MSI_MESI:
                {
                    privateSharedMSI::privateSharedMSICfg_t cfg;
                    cfg.use_mesi = read_word(img);
                    cfg.num_nodes = num_nodes;
                    cfg.bytes_per_flit = bytes_per_flit;
                    cfg.address_size_in_bytes = address_size_in_bytes;
                    cfg.l1_work_table_size= read_word(img);
                    cfg.l2_work_table_size_shared = read_word(img);
                    cfg.l2_work_table_size_replies = read_word(img);
                    cfg.l2_work_table_size_evict = read_word(img);
                    cfg.l1_replacement_policy = (replacementPolicy_t)read_word(img);
                    cfg.l2_replacement_policy = (replacementPolicy_t)read_word(img);
                    cfg.words_per_cache_line = read_word(img);
                    cfg.num_local_core_ports = read_word(img);
                    cfg.lines_in_l1 = read_word(img);
                    cfg.l1_associativity = read_word(img);
                    cfg.l1_hit_test_latency = read_word(img);
                    cfg.l1_num_read_ports = read_word(img);
                    cfg.l1_num_write_ports = read_word(img);
                    cfg.lines_in_l2 = read_word(img);
                    cfg.l2_associativity = read_word(img);
                    cfg.l2_hit_test_latency = read_word(img);
                    cfg.l2_num_read_ports = read_word(img);
                    cfg.l2_num_write_ports = read_word(img);

                    assert(em_type == EM_NEVER);
                    if (private_shared_msi_stats == boost::shared_ptr<privateSharedMSIStats>()) {
                        private_shared_msi_stats = 
                            boost::shared_ptr<privateSharedMSIStats>(new privateSharedMSIStats(t->get_time()));
                        stats->add_aux_statistics(private_shared_msi_stats);
                    }
                    boost::shared_ptr<privateSharedMSI> new_mem = 
                        boost::shared_ptr<privateSharedMSI>(new privateSharedMSI(id, t->get_time(), 
                                                                          t->get_statistics(), log, ran, new_cat, cfg));
                    boost::shared_ptr<privateSharedMSIStatsPerTile> per_tile_stats = 
                        boost::shared_ptr<privateSharedMSIStatsPerTile>(new privateSharedMSIStatsPerTile(id, t->get_time()));
                    new_mem->set_per_tile_stats(per_tile_stats);

                    private_shared_msi_stats->add_per_tile_stats(per_tile_stats);

                    mem = new_mem;

                    break;
                }
            case MEM_PRIVATE_SHARED_LCC:
                {
                    privateSharedLCC::privateSharedLCCCfg_t cfg;
                    cfg.logic = (privateSharedLCC::timestampLogic_t)read_word(img);
                    cfg.save_timestamp_in_dram = read_word(img);
                    cfg.use_separate_vc_for_writes = read_word(img);
                    cfg.default_timestamp_delta = read_word(img); /**/
                    cfg.num_nodes = num_nodes;
                    cfg.bytes_per_flit = bytes_per_flit;
                    cfg.address_size_in_bytes = address_size_in_bytes;
                    cfg.l2_work_table_size_shared = read_word(img);
                    cfg.l2_work_table_size_readonly = read_word(img);
                    cfg.l1_replacement_policy = (privateSharedLCC::_replacementPolicy_t)read_word(img);
                    cfg.l2_replacement_policy = (privateSharedLCC::_replacementPolicy_t)read_word(img);                 
                    cfg.words_per_cache_line = read_word(img);
                    cfg.num_local_core_ports = read_word(img);
                    cfg.lines_in_l1 = read_word(img);
                    cfg.l1_associativity = read_word(img);
                    cfg.l1_hit_test_latency = read_word(img);
                    cfg.l1_num_read_ports = read_word(img);
                    cfg.l1_num_write_ports = read_word(img);
                    cfg.lines_in_l2 = read_word(img);
                    cfg.l2_associativity = read_word(img);
                    cfg.l2_hit_test_latency = read_word(img);
                    cfg.l2_num_read_ports = read_word(img);
                    cfg.l2_num_write_ports = read_word(img);

                    if (private_shared_lcc_stats == boost::shared_ptr<privateSharedLCCStats>()) {
                        private_shared_lcc_stats = 
                            boost::shared_ptr<privateSharedLCCStats>(new privateSharedLCCStats(t->get_time()));
                        stats->add_aux_statistics(private_shared_lcc_stats);
                    }

                    boost::shared_ptr<privateSharedLCC> new_mem = 
                        boost::shared_ptr<privateSharedLCC>(new privateSharedLCC(id, t->get_time(), 
                                                                          t->get_statistics(), log, ran, new_cat, cfg));
                    boost::shared_ptr<privateSharedLCCStatsPerTile> per_tile_stats = 
                        boost::shared_ptr<privateSharedLCCStatsPerTile>(new privateSharedLCCStatsPerTile(id, t->get_time()));
                    if (cfg.logic == privateSharedLCC::TIMESTAMP_IDEAL) {
                        per_tile_stats->record_ideal_timestamp();
                    }
                    new_mem->set_per_tile_stats(per_tile_stats);

                    private_shared_lcc_stats->add_per_tile_stats(per_tile_stats);

                    mem = new_mem;

                    break;
                }
            default:
                {
                    throw err_bad_shmem_cfg("not supported memory type");
                    break;
                }
            }

            /* dram controller set up */
            uint32_t location = 0;
            if (dram_location == TOP_AND_BOTTOM_TO_DRAM) {
                if (id/network_width == 0 || id/network_width == (num_nodes-1)/network_width) {
                    location = id;
                } else {
                    if (id < num_nodes/2) {
                        location = id%network_width;
                    } else {
                        location = num_nodes - network_width + id%network_width;
                    }
                }
            } else if (dram_location == BOUNDARY_TO_DRAM) {
                if (id/network_width == 0 || id/network_width == (num_nodes-1)/network_width
                    || id%network_width == 0 || id%network_width == network_width -1 ) {
                    location = id;
                } else if (id < num_nodes/2) {
                    uint32_t min_dist = min( id/network_width, min(id%network_width, network_width - id%network_width) );
                    if (id/network_width == min_dist) {
                        location = id%network_width;
                    } else if (id%network_width == min_dist) {
                        location = id/network_width;
                    } else {
                        location = id/network_width + network_width - 1;
                    }
                } else {
                    uint32_t min_dist = min( num_nodes/network_width - id/network_width, 
                                             min(id%network_width, network_width - id%network_width) );
                    if (num_nodes/network_width - id/network_width == min_dist) {
                        location = num_nodes - network_width + id%network_width;
                    } else if (id%network_width == min_dist) {
                        location = id/network_width;
                    } else {
                        location = id/network_width + network_width - 1;
                    }
                }
            }

            if (location != (uint32_t)id) {
                mem->set_remote_dram_controller(location);
            } else {
                mem->add_local_dram_controller(new_dram, dram_controller_latency, offchip_oneway_latency, dram_latency,
                                               msg_header_size_in_words, dc_max_requests_in_flight, bandwidth_in_words_per_cycle,
                                               true /* use lock */);
            }

            boost::shared_ptr<memtraceCore> new_core = 
                boost::shared_ptr<memtraceCore>(new memtraceCore(pe_id(id), t->get_time(), t->get_packet_id_factory(),
                                                          t->get_statistics(), log, ran, memtrace_thread_pool, mem,
                                                          em_type, msg_queue_size, bytes_per_flit, 
                                                          (mig_size_in_bytes + bytes_per_flit - 1)/bytes_per_flit, /* flit/context */
                                                          max_threads));

            p = new_core;
            memtrace_cores.push_back(new_core);

            break;
        }
        case CORE_MCPU: {
            // -----------------------------------------------------------------
            // MIPS image setup ------------------------------------------------ 
            // -----------------------------------------------------------------

            uint32_t mem_start = read_word(img);
            uint32_t mem_size = read_word(img);

            boost::shared_ptr<mem> m(new mem(id, mem_start, mem_size, log));
            read_mem(m->ptr(mem_start), mem_size, img);

            uint32_t __attribute__((unused)) cpu_entry_point = read_word(img);
            uint32_t __attribute__((unused)) cpu_stack_pointer = read_word(img);

            // setup -----------------------------------------------------------

            /* memory type */
            uint32_t memory_type_word = read_word(img);
            memoryType_t mem_type = static_cast<memoryType_t>(memory_type_word);

            /* dram controller location */
            uint32_t dram_location_word = read_word(img);
            dramLocationType_t dram_location = static_cast<dramLocationType_t>(dram_location_word);

            /* cat type */
            uint32_t cat_type_word = read_word(img);
            catType_t cat_type = static_cast<catType_t>(cat_type_word);

            /* cat latency */
            uint32_t cat_latency = read_word(img);

            /* cat allocation unit */
            uint32_t cat_allocation_unit_in_bytes = read_word(img);

            /* cat synch delay */
            uint32_t cat_synch_delay = read_word(img);

            /* cat number of ports 0:infinite */
            uint32_t cat_num_ports = read_word(img);

            boost::shared_ptr<cat> new_cat = boost::shared_ptr<cat>();
            switch(cat_type) {
            case CAT_STRIPE:
                new_cat = boost::shared_ptr<catStripe>(new catStripe(num_nodes, t->get_time(), cat_num_ports, 
                                                              cat_latency, cat_allocation_unit_in_bytes));
                break;
            case CAT_STATIC:
                new_cat = boost::shared_ptr<catStatic>(new catStatic(num_nodes, t->get_time(), cat_num_ports, 
                                                              cat_latency, cat_allocation_unit_in_bytes, cat_synch_delay));
                break;
            case CAT_FIRST_TOUCH:
                new_cat = boost::shared_ptr<catFirstTouch>(new catFirstTouch(num_nodes, t->get_time(), cat_num_ports, 
                                                                      cat_latency, cat_allocation_unit_in_bytes, cat_synch_delay));
                break;
            }

            // dram controller -------------------------------------------------

            uint32_t dram_controller_latency = read_word(img);
            uint32_t offchip_oneway_latency = read_word(img);
            uint32_t dram_latency = read_word(img);
            uint32_t msg_header_size_in_words = read_word(img);
            uint32_t dc_max_requests_in_flight = read_word(img);
            uint32_t bandwidth_in_words_per_cycle = read_word(img);
            uint32_t address_size_in_bytes = read_word(img);

            assert(mem_type == MEM_PRIVATE_SHARED_MSI_MESI);

            // data and instruction cache --------------------------------------

            boost::shared_ptr<memory> data_memory = boost::shared_ptr<memory>();
            //shared_ptr<memory> instruction_memory = boost::shared_ptr<memory>();

            privateSharedMSI::privateSharedMSICfg_t cfg;
            cfg.use_mesi = read_word(img);
            cfg.num_nodes = num_nodes;
            cfg.bytes_per_flit = bytes_per_flit;
            cfg.address_size_in_bytes = address_size_in_bytes;
            cfg.l1_work_table_size= read_word(img);
            cfg.l2_work_table_size_shared = read_word(img);
            cfg.l2_work_table_size_replies = read_word(img);
            cfg.l2_work_table_size_evict = read_word(img);
            cfg.l1_replacement_policy = (replacementPolicy_t)read_word(img);
            cfg.l2_replacement_policy = (replacementPolicy_t)read_word(img);
            cfg.words_per_cache_line = read_word(img);
            cfg.num_local_core_ports = read_word(img);
            cfg.lines_in_l1 = read_word(img);
            cfg.l1_associativity = read_word(img);
            cfg.l1_hit_test_latency = read_word(img);
            cfg.l1_num_read_ports = read_word(img);
            cfg.l1_num_write_ports = read_word(img);
            cfg.lines_in_l2 = read_word(img);
            cfg.l2_associativity = read_word(img);
            cfg.l2_hit_test_latency = read_word(img);
            cfg.l2_num_read_ports = read_word(img);
            cfg.l2_num_write_ports = read_word(img);

            if (private_shared_msi_stats == boost::shared_ptr<privateSharedMSIStats>()) {
                private_shared_msi_stats = 
                    boost::shared_ptr<privateSharedMSIStats>(new privateSharedMSIStats(t->get_time()));
                stats->add_aux_statistics(private_shared_msi_stats);
            }

            // form new caches
            boost::shared_ptr<privateSharedMSI> new_data_memory = 
                boost::shared_ptr<privateSharedMSI>(new privateSharedMSI(id, t->get_time(), 
                                                                  t->get_statistics(), log, ran, new_cat, cfg));
            //shared_ptr<privateSharedMSI> new_instruction_memory = 
            //    boost::shared_ptr<privateSharedMSI>(new privateSharedMSI(id, t->get_time(), 
            //                                                     t->get_statistics(), log, ran, new_cat, cfg));

            boost::shared_ptr<privateSharedMSIStatsPerTile> per_tile_stats = 
                boost::shared_ptr<privateSharedMSIStatsPerTile>(new privateSharedMSIStatsPerTile(id, t->get_time()));
            new_data_memory->set_per_tile_stats(per_tile_stats);
            //new_instruction_memory->set_per_tile_stats(per_tile_stats);

            private_shared_msi_stats->add_per_tile_stats(per_tile_stats);

            data_memory = new_data_memory;
            //instruction_memory = new_instruction_memory;

            // DRAM controller setup -------------------------------------------

            uint32_t location = 0;
            if (dram_location == TOP_AND_BOTTOM_TO_DRAM) {
                if (id/network_width == 0 || id/network_width == (num_nodes-1)/network_width) {
                    location = id;
                } else {
                    if (id < num_nodes/2) {
                        location = id%network_width;
                    } else {
                        location = num_nodes - network_width + id%network_width;
                    }
                }
            } else if (dram_location == BOUNDARY_TO_DRAM) {
                if (id/network_width == 0 || id/network_width == (num_nodes-1)/network_width
                    || id%network_width == 0 || id%network_width == network_width -1 ) {
                    location = id;
                } else if (id < num_nodes/2) {
                    uint32_t min_dist = min( id/network_width, min(id%network_width, network_width - id%network_width) );
                    if (id/network_width == min_dist) {
                        location = id%network_width;
                    } else if (id%network_width == min_dist) {
                        location = id/network_width;
                    } else {
                        location = id/network_width + network_width - 1;
                    }
                } else {
                    uint32_t min_dist = min( num_nodes/network_width - id/network_width, 
                                             min(id%network_width, network_width - id%network_width) );
                    if (num_nodes/network_width - id/network_width == min_dist) {
                        location = num_nodes - network_width + id%network_width;
                    } else if (id%network_width == min_dist) {
                        location = id/network_width;
                    } else {
                        location = id/network_width + network_width - 1;
                    }
                }
            }

            if (location != (uint32_t)id) {
                data_memory->set_remote_dram_controller(location);
                //instruction_memory->set_remote_dram_controller(location);
            } else {
                data_memory->add_local_dram_controller(new_dram, dram_controller_latency, offchip_oneway_latency, dram_latency,
                                               msg_header_size_in_words, dc_max_requests_in_flight, bandwidth_in_words_per_cycle,
                                               true /* use lock */);
                //instruction_memory->add_local_dram_controller(new_dram, dram_controller_latency, offchip_oneway_latency, dram_latency,
                //               msg_header_size_in_words, dc_max_requests_in_flight, bandwidth_in_words_per_cycle,
                //               true /* use lock */);
            }

            // Core config setup -----------------------------------------------

            new_dram->mem_write_instant(m, i+1, mem_start, mem_size);

            boost::shared_ptr<mcpu> new_core(new mcpu( pe_id(id), 
                                                t->get_time(), 
                                                cpu_entry_point,
                                                cpu_stack_pointer,
                                                t->get_packet_id_factory(),
                                                t->get_statistics(),
                                                log,
                                                ran,
            /* To re-enable instruction memory, uncomment lines ABOVE this one, 
               and also add the second loop to the pos_tick code in mcpu */
                                                data_memory,//instruction_memory,
                                                data_memory,
                                                new_dram,
                                                flits_per_q,
                                                bytes_per_flit));
            
            p = new_core;
            break;
        }
        default:
            throw err_bad_mem_img();
        }

        pes[id] = p;
        t->add(p);
        p->connect(b);
        br_vcas[id] = b_vca;
        node_rts[id] = n_rt;
        n_vcas[id] = n_vca;
    }
    for (unsigned i = 0; i < num_nodes; ++i) {
        assert(nodes[i]);
        assert(pes[i]);
        assert(bridges[i]);
        assert(br_vcas[i]);
        assert(node_rts[i]);
        assert(n_vcas[i]);
    }
    arbitration_t arb_scheme = static_cast<arbitration_t>(read_word(img));
    unsigned arb_min_bw = 0;
    unsigned arb_period = 0;
    unsigned arb_delay = 0;
    if (arb_scheme != AS_NONE) {
        arb_min_bw = read_word(img);
        arb_period = read_word(img);
        arb_delay = read_word(img);
    }
    uint32_t num_cxns = read_word(img);
    typedef set<boost::tuple<uint32_t, uint32_t> > cxns_t;
    cxns_t cxns;
    LOG(log,2) << "network fabric has " << dec << num_cxns
               << " one-way link" << (num_cxns == 1 ? "" : "s") << endl;
    if (arb_scheme == AS_NONE) {
        LOG(log,2) << "    (no bidirectional arbitration)" << endl;
    } else {
        LOG(log,2) << "    (bidirectional arbitration every "
                   << dec << arb_period
                   << " cycle" << (arb_period == 1 ? "" : "s");
        if (arb_min_bw == 0) {
            LOG(log,2) << " with no minimum bandwidth" << endl;
        } else {
            LOG(log,2) << " with minimum bandwidth "
                       << dec << arb_min_bw << endl;
        }
    }
    for (unsigned i  = 0; i < num_cxns; ++i) {
        uint32_t link_src_n = read_word(img);
        string link_src_port_name = read_string(img);
        uint32_t link_dst_n = read_word(img);
        string link_dst_port_name = read_string(img);
        uint32_t link_bw = read_word(img);
        uint32_t to_xbar_bw = read_word(img);
        uint32_t link_num_queues = read_word(img);
        set<virtual_queue_id> queues;
        for (unsigned q = 0; q < link_num_queues; ++q) {
            queues.insert(virtual_queue_id(read_word(img)));
        }
        if (link_src_n >= num_nodes) throw err_bad_mem_img();
        if (link_dst_n >= num_nodes) throw err_bad_mem_img();
        nodes[link_dst_n]->connect_from(link_dst_port_name,
                                        nodes[link_src_n],
                                        link_src_port_name,
                                        queues, link_bw, to_xbar_bw);
        cxns.insert(boost::make_tuple(link_src_n, link_dst_n));
    }
    if (arb_scheme != AS_NONE) {
        for (cxns_t::const_iterator i = cxns.begin(); i != cxns.end(); ++i) {
            uint32_t from, to;
            boost::tie(from, to) = *i;
            if (from <= to
                && cxns.count(boost::make_tuple(to, from)) > 0) {
                boost::shared_ptr<tile> t = tiles[from];
                boost::shared_ptr<arbiter> arb =
                    boost::shared_ptr<arbiter>(new arbiter(t->get_time(), nodes[from],
                                                    nodes[to], arb_scheme,
                                                    arb_min_bw, arb_period,
                                                    arb_delay,
                                                    t->get_statistics(), log));
                arbiters[*i] = arb;
                t->add(arb);
            }
        }
    }

    uint32_t num_routes = read_word(img);
    LOG(log,2) << "network contains " << dec << num_routes
               << " routing " << (num_routes==1 ? "entry" : "entries") << endl;
    for (unsigned i = 0; i < num_routes; ++i) {
        uint32_t flow = read_word(img);
        // XXX stats->register_flow(flow_id(flow));
        uint32_t cur_n = read_word(img);
        uint32_t prev_n = read_word(img);
        if (prev_n == 0xffffffffUL) { // program the bridge
            uint32_t num_queues = read_word(img); // 0 is valid (= all queues)
            vector<boost::tuple<virtual_queue_id, double> > qs;
            for (uint32_t q = 0; q < num_queues; ++q) {
                uint32_t vqid = read_word(img);
                double prop = read_double(img);
                if (prop <= 0) throw err_bad_mem_img();
                qs.push_back(boost::make_tuple(virtual_queue_id(vqid), prop));
            }
            (*flow_starts)[flow] = cur_n;
            boost::shared_ptr<set_bridge_channel_alloc> vca =
                static_pointer_cast<set_bridge_channel_alloc>(br_vcas[cur_n]);
            vca->add_route(flow_id(flow), qs);
        } else { // program a routing node
            uint32_t num_nodes = read_word(img);
            vector<boost::tuple<node_id,flow_id,double> > next_nodes;
            if (num_nodes == 0) throw err_bad_mem_img();
            for (uint32_t n = 0; n < num_nodes; ++n) {
                uint32_t next_n = read_word(img);
                uint32_t next_f = read_word(img);
                double nprop = read_double(img);
                if (nprop <= 0) throw err_bad_mem_img();
                next_nodes.push_back(boost::make_tuple(next_n, next_f, nprop));
                uint32_t num_queues = read_word(img); // 0 is valid
                vector<boost::tuple<virtual_queue_id,double> > next_qs;
                for (uint32_t q = 0; q < num_queues; ++q) {
                    uint32_t qid = read_word(img);
                    double qprop = read_double(img);
                    if (qprop <= 0) throw err_bad_mem_img();
                    next_qs.push_back(boost::make_tuple(qid, qprop));
                }
                boost::shared_ptr<set_channel_alloc> vca =
                    static_pointer_cast<set_channel_alloc>(n_vcas[cur_n]);
                vca->add_route(prev_n, flow_id(flow),
                               next_n, flow_id(next_f), next_qs);
                if (flow != next_f) {
                    flow_renames->add_flow_rename(flow, next_f);
                }
            }
            boost::shared_ptr<set_router> r =
                static_pointer_cast<set_router>(node_rts[cur_n]);
            r->add_route(prev_n, flow_id(flow), next_nodes);
        }
    }
    event_parser ep(events_files, injectors, flow_starts);

    /* populate memtrace thread pool from traces */
    create_memtrace_threads(memtrace_files, memtrace_thread_pool, memtrace_cores.size(), sys_time, log, stats);

    /* spawn threads */
    for (unsigned int i = 0; i < memtrace_thread_pool->size() && memtrace_cores.size() > 0; ++i) {
        uint32_t mth_id = memtrace_thread_pool->thread_at(i)->get_id();
        unsigned int idx = mth_id % memtrace_cores.size();
        memtrace_cores[idx]->spawn(memtrace_thread_pool->thread_at(i));
    }

    LOG(log,1) << "system created" << endl;
}

boost::shared_ptr<system_statistics> sys::get_statistics() const throw() {
    return stats;
}

boost::shared_ptr<tile_statistics> sys::get_statistics_tile(tile_id t) const throw() {
    assert(t.get_numeric_id() < tiles.size());
    return tiles[t.get_numeric_id()]->get_statistics();
}

uint32_t sys::get_num_tiles() const throw() {
    return tiles.size();
}

void sys::tick_positive_edge()   {
    LOG(log,1) << "[system] posedge " << dec << get_time() << endl;
    if (test_flags & TF_RANDOMIZE_NODE_ORDER) {
        boost::function<int(int)> rr_fn =
            bind(&random_gen::random_range, sys_rand, _1);
        random_shuffle(tile_indices.begin(), tile_indices.end(), rr_fn);
    }
    for (vector<tile_id>::const_iterator i = tile_indices.begin();
         i != tile_indices.end(); ++i) {
        tick_positive_edge_tile(*i);
    }
}

void sys::tick_negative_edge()   {
    LOG(log,1) << "[system] negedge " << dec << get_time() << endl;
    if (test_flags & TF_RANDOMIZE_NODE_ORDER) {
        boost::function<int(int)> rr_fn =
            bind(&random_gen::random_range, sys_rand, _1);
        random_shuffle(tile_indices.begin(), tile_indices.end(), rr_fn);
    }
    for (vector<tile_id>::const_iterator i = tile_indices.begin();
         i != tile_indices.end(); ++i) {
        tick_negative_edge_tile(*i);
    }
}

void sys::fast_forward_time(uint64_t new_time) throw() {
    assert(new_time >= get_time());
    LOG(log,1) << "[system] fast forward to  " << dec << new_time << endl;
    if (test_flags & TF_RANDOMIZE_NODE_ORDER) {
        boost::function<int(int)> rr_fn =
            bind(&random_gen::random_range, sys_rand, _1);
        random_shuffle(tile_indices.begin(), tile_indices.end(), rr_fn);
    }
    for (vector<tile_id>::const_iterator i = tile_indices.begin();
         i != tile_indices.end(); ++i) {
        fast_forward_time_tile(*i, new_time);
    }
    sys_time = new_time;
}

void sys::tick_positive_edge_tile(tile_id tile_no)   {
    assert(tile_no.get_numeric_id() < tiles.size());
    tiles[tile_no.get_numeric_id()]->tick_positive_edge();
}

void sys::tick_negative_edge_tile(tile_id tile_no)   {
    assert(tile_no.get_numeric_id() < tiles.size());
    tiles[tile_no.get_numeric_id()]->tick_negative_edge();
    if (tiles[tile_no.get_numeric_id()]->get_time() > get_time()) {
        sys_time = tiles[tile_no.get_numeric_id()]->get_time();
    }
}

void sys::fast_forward_time_tile(tile_id tile_no, uint64_t new_time) throw() {
    assert(new_time >= get_time());
    assert(tile_no.get_numeric_id() < tiles.size());
    tiles[tile_no.get_numeric_id()]->fast_forward_time(new_time);
    sys_time = new_time;
}

uint64_t sys::get_time_tile(tile_id tile_no) const throw() {
    return tiles[tile_no.get_numeric_id()]->get_time();
}

uint64_t sys::advance_time()   {
    uint64_t next_time = UINT64_MAX;
    for (vector<tile_id>::const_iterator i = tile_indices.begin();
         i != tile_indices.end(); ++i) {
       uint64_t t = advance_time_tile(*i);
       if (next_time > t) next_time = t;
    }
    return next_time;
}

uint64_t sys::advance_time_tile(tile_id tile_no)   {
    return tiles[tile_no.get_numeric_id()]->next_pkt_time();
}

bool sys::is_drained() const throw() {
    for (vector<tile_id>::const_iterator i = tile_indices.begin();
         i != tile_indices.end(); ++i) {
        if (!is_drained_tile(*i)) return false;
    }
    return true;
}

bool sys::is_drained_tile(tile_id tile_no) const throw() {
    return tiles[tile_no.get_numeric_id()]->is_drained();
}

bool sys::nothing_to_offer()   {
    for (tiles_t::const_iterator i = tiles.begin(); i != tiles.end(); ++i) {
        if ((*i)->is_ready_to_offer()) return false;
    }
    return true;
}

bool sys::work_tbd_darsim()   {
    for (tiles_t::const_iterator i = tiles.begin(); i != tiles.end(); ++i) {
       if ((*i)->work_queued()) return true;
    }
    return false;
}

uint64_t sys::get_time() const throw() {
    return sys_time;
}
