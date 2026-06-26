#ifndef IP_ROUTING_H
#define IP_ROUTING_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

/* ── L1 Definitions: IP Routing Table & Algorithms ── */

#define IP_ROUTE_MAX_ENTRIES    256
#define IP_ROUTE_MAX_HOPS       64
#define IP_ROUTE_NETWORK_COUNT  32
#define IP_ROUTE_INFINITY       16
#define IP_ROUTE_METRIC_MAX     65535

/* CIDR notation: prefix length [0, 32] */
#define IP_CIDR_MIN_PREFIX      0
#define IP_CIDR_MAX_PREFIX      32

/* ── L1: Routing Table Entry (RFC 1812) ── */
typedef struct {
    uint32_t  network;       
    uint8_t   prefix_len;    
    uint32_t  nexthop;       
    uint32_t  interface_addr;
    uint16_t  metric;         
    uint16_t  admin_distance; 
    bool      active;         
    uint32_t  age;            
} IPRouteEntry;

/* ── L1: Routing Table ── */
typedef struct {
    IPRouteEntry entries[IP_ROUTE_MAX_ENTRIES];
    size_t       count;
    uint32_t     default_gateway;
    bool         has_default;
} IPRoutingTable;

/* ── L1: Network Topology Node (for Dijkstra/Bellman-Ford) ── */
typedef struct {
    uint32_t addr;           
    uint32_t cost[IP_ROUTE_NETWORK_COUNT];
    size_t   neighbor_count;
    uint32_t neighbors[IP_ROUTE_NETWORK_COUNT];
    bool     visited;
    uint32_t distance;
    uint32_t previous;
} IPNetworkNode;

/* ── L1: Topology Graph ── */
typedef struct {
    IPNetworkNode nodes[IP_ROUTE_NETWORK_COUNT];
    size_t        node_count;
} IPTopologyGraph;

/* ── L5: Longest Prefix Match / CIDR Operations ── */

/* Subnet mask from prefix length */
uint32_t ip_prefix_to_mask(uint8_t prefix_len);

/* Network address from IP + prefix */
uint32_t ip_network_addr(uint32_t ip, uint8_t prefix_len);

/* Broadcast address from network + prefix */
uint32_t ip_broadcast_addr(uint32_t network, uint8_t prefix_len);

/* Check if IP is in subnet */
bool ip_in_subnet(uint32_t ip, uint32_t network, uint8_t prefix_len);

/* Host count in subnet */
uint32_t ip_subnet_host_count(uint8_t prefix_len);

/* CIDR notation to string (e.g., "192.168.1.0/24") */
int ip_cidr_to_str(uint32_t network, uint8_t prefix_len,
                   char *buf, size_t buf_len);

/* Parse CIDR string */
int ip_cidr_from_str(const char *cidr, uint32_t *network, uint8_t *prefix_len);

/* ── L1: IP Routing Table API ── */
void     ip_routing_table_init(IPRoutingTable *rt);
int      ip_routing_add(IPRoutingTable *rt, uint32_t network,
                        uint8_t prefix_len, uint32_t nexthop,
                        uint32_t iface, uint16_t metric);
int      ip_routing_remove(IPRoutingTable *rt, uint32_t network,
                           uint8_t prefix_len);
void     ip_routing_set_default(IPRoutingTable *rt, uint32_t gateway);

/* ── L5: Longest Prefix Match (RFC 1812 5.2.4.3) ──
 * Find the most specific route matching the destination.
 * Complexity: O(n) linear scan; can be O(log n) with trie. */
int      ip_routing_lookup(const IPRoutingTable *rt, uint32_t dst,
                           IPRouteEntry *result);

/* ── L3 Engineering: Routing Table Operations ── */
void     ip_routing_sort_by_prefix(IPRoutingTable *rt);
int      ip_routing_aggregate(IPRoutingTable *rt);
void     ip_routing_print(const IPRoutingTable *rt);
void     ip_routing_print_entry(const IPRouteEntry *entry);

/* ── L5: Dijkstra's Shortest Path First (OSPF Algorithm) ──
 * Time complexity: O(V^2) where V = vertex count.
 * Space complexity: O(V).
 * Reference: Dijkstra (1959), "A Note on Two Problems in
 * Connexion with Graphs" */
int      ip_topology_dijkstra(IPTopologyGraph *graph, size_t src_idx,
                              uint32_t *distances, uint32_t *previous);

/* ── L5: Bellman-Ford Distance Vector (RIP Algorithm) ──
 * Time complexity: O(V*E) where V = vertex count, E = edge count.
 * Handles negative edge weights (unlike Dijkstra).
 * Detects negative cycles.
 * Reference: Bellman (1958), Ford (1956) */
int      ip_topology_bellman_ford(IPTopologyGraph *graph, size_t src_idx,
                                  uint32_t *distances, uint32_t *previous);

/* ── L3 Engineering: Topology Graph Operations ── */
void     ip_topology_init(IPTopologyGraph *graph);
int      ip_topology_add_node(IPTopologyGraph *graph, uint32_t addr);
int      ip_topology_add_edge(IPTopologyGraph *graph,
                              size_t from, size_t to, uint32_t cost);
void     ip_topology_print(const IPTopologyGraph *graph);

/* ── L4: Count-to-Infinity Problem ──
 * Distance vector routing suffers from count-to-infinity.
 * Solutions: split horizon, poison reverse, path hold-down.
 * Default infinity value (RIP): 16 hops. */
#define IP_ROUTE_INF 16

/* Split horizon check */
bool ip_routing_split_horizon(const IPRoutingTable *rt,
                               uint32_t dest, uint32_t nexthop);

#endif