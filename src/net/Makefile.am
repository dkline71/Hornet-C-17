AM_CPPFLAGS = -I$(top_srcdir)/src/power $(BOOST_CPPFLAGS)
AM_LDFLAGS = $(BOOST_LDFLAGS)
AM_CXXFLAGS = -Wall -Wno-uninitialized -Werror

noinst_LTLIBRARIES = libdarnet.la

libdarnet_la_SOURCES = \
    flow_id.hpp flow_id.cpp \
    flit.hpp flit.cpp \
    node_id.hpp node_id.cpp \
    link_id.hpp link_id.cpp \
    virtual_queue_id.hpp virtual_queue_id.cpp \
    router.hpp router.cpp \
    channel_alloc.hpp channel_alloc.cpp \
    bridge_channel_alloc.hpp bridge_channel_alloc.cpp \
    pressure_tracker.hpp pressure_tracker.cpp \
    virtual_queue.hpp virtual_queue.cpp \
    ingress_id.hpp ingress_id.cpp \
    ingress.hpp ingress.cpp \
    egress_id.hpp egress_id.cpp \
    egress.hpp egress.cpp \
    crossbar.hpp crossbar.cpp \
    power_controller.hpp power_controller.cpp \
    node.hpp node.cpp \
    dma.hpp dma.cpp \
    bridge.hpp bridge.cpp \
    arbiter.hpp arbiter.cpp \
    set_router.hpp set_router.cpp \
    set_channel_alloc.hpp set_channel_alloc.cpp \
    set_bridge_channel_alloc.hpp set_bridge_channel_alloc.cpp \
    statistics.hpp statistics.cpp 
    
