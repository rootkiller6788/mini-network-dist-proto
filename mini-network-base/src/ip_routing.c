#include "ip_routing.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>

/* L4: CIDR (Classless Inter-Domain Routing) - RFC 4632 */

uint32_t ip_prefix_to_mask(uint8_t prefix_len) {
    if (prefix_len == 0) return 0;
    if (prefix_len > 32) prefix_len = 32;
    if (prefix_len == 32) return 0xFFFFFFFF;
    return (uint32_t)(0xFFFFFFFFULL << (32 - prefix_len));
}

uint32_t ip_network_addr(uint32_t ip, uint8_t prefix_len) {
    uint32_t mask = ip_prefix_to_mask(prefix_len);
    return ip & mask;
}

uint32_t ip_broadcast_addr(uint32_t network, uint8_t prefix_len) {
    uint32_t mask = ip_prefix_to_mask(prefix_len);
    return network | (~mask);
}

bool ip_in_subnet(uint32_t ip, uint32_t network, uint8_t prefix_len) {
    uint32_t mask = ip_prefix_to_mask(prefix_len);
    return (ip & mask) == (network & mask);
}

uint32_t ip_subnet_host_count(uint8_t prefix_len) {
    if (prefix_len >= 31) return 0;
    uint32_t bits = 32 - prefix_len;
    return ((uint32_t)1 << bits) - 2;
}

int ip_cidr_to_str(uint32_t network, uint8_t prefix_len, char *buf, size_t buf_len) {
    if (!buf || buf_len < 20) return -1;
    snprintf(buf, buf_len, "%u.%u.%u.%u/%u",
             (unsigned)((network >> 24) & 0xFF), (unsigned)((network >> 16) & 0xFF),
             (unsigned)((network >> 8) & 0xFF), (unsigned)(network & 0xFF),
             (unsigned)prefix_len);
    return 0;
}

int ip_cidr_from_str(const char *cidr, uint32_t *network, uint8_t *prefix_len) {
    if (!cidr || !network || !prefix_len) return -1;
    unsigned int a, b, c, d, p;
    if (sscanf(cidr, "%u.%u.%u.%u/%u", &a, &b, &c, &d, &p) != 5) return -2;
    if (a > 255 || b > 255 || c > 255 || d > 255 || p > 32) return -3;
    *network = ((a & 0xFF) << 24) | ((b & 0xFF) << 16) | ((c & 0xFF) << 8) | (d & 0xFF);
    *prefix_len = (uint8_t)p;
    return 0;
}

/* L3: IP Routing Table Operations */

void ip_routing_table_init(IPRoutingTable *rt) {
    if (!rt) return;
    memset(rt, 0, sizeof(IPRoutingTable));
    rt->has_default = false;
}

int ip_routing_add(IPRoutingTable *rt, uint32_t network, uint8_t prefix_len,
                   uint32_t nexthop, uint32_t iface, uint16_t metric) {
    if (!rt) return -1;
    if (rt->count >= IP_ROUTE_MAX_ENTRIES) return -2;
    if (prefix_len > 32) return -3;
    for (size_t i = 0; i < rt->count; i++) {
        if (rt->entries[i].network == network &&
            rt->entries[i].prefix_len == prefix_len &&
            rt->entries[i].active) {
            rt->entries[i].nexthop = nexthop;
            rt->entries[i].interface_addr = iface;
            rt->entries[i].metric = metric;
            rt->entries[i].age = 0;
            return 0;
        }
    }
    IPRouteEntry *e = &rt->entries[rt->count];
    e->network = network; e->prefix_len = prefix_len;
    e->nexthop = nexthop; e->interface_addr = iface;
    e->metric = metric; e->admin_distance = 1;
    e->active = true; e->age = 0;
    if (prefix_len == 0) { rt->default_gateway = nexthop; rt->has_default = true; }
    rt->count++;
    return 0;
}

int ip_routing_remove(IPRoutingTable *rt, uint32_t network, uint8_t prefix_len) {
    if (!rt) return -1;
    for (size_t i = 0; i < rt->count; i++) {
        if (rt->entries[i].network == network &&
            rt->entries[i].prefix_len == prefix_len) {
            rt->entries[i].active = false;
            if (i < rt->count - 1)
                memmove(&rt->entries[i], &rt->entries[i + 1],
                        (rt->count - i - 1) * sizeof(IPRouteEntry));
            rt->count--;
            return 0;
        }
    }
    return -2;
}

void ip_routing_set_default(IPRoutingTable *rt, uint32_t gateway) {
    if (!rt) return;
    ip_routing_add(rt, 0, 0, gateway, 0, 1);
}

/* L5: Longest Prefix Match - RFC 1812 5.2.4.3. O(n) scan. */

int ip_routing_lookup(const IPRoutingTable *rt, uint32_t dst, IPRouteEntry *result) {
    if (!rt || !result) return -1;
    int best_idx = -1;
    uint8_t best_prefix = 0;
    uint16_t best_metric = IP_ROUTE_METRIC_MAX;
    for (size_t i = 0; i < rt->count; i++) {
        const IPRouteEntry *e = &rt->entries[i];
        if (!e->active) continue;
        uint32_t mask = ip_prefix_to_mask(e->prefix_len);
        if ((dst & mask) == (e->network & mask)) {
            if (e->prefix_len > best_prefix ||
                (e->prefix_len == best_prefix && e->metric < best_metric)) {
                best_idx = (int)i;
                best_prefix = e->prefix_len;
                best_metric = e->metric;
            }
        }
    }
    if (best_idx < 0) {
        if (rt->has_default) {
            for (size_t i = 0; i < rt->count; i++) {
                if (rt->entries[i].active && rt->entries[i].prefix_len == 0) {
                    memcpy(result, &rt->entries[i], sizeof(IPRouteEntry));
                    return 0;
                }
            }
        }
        return -2;
    }
    memcpy(result, &rt->entries[best_idx], sizeof(IPRouteEntry));
    return 0;
}

static int compare_prefix(const void *a, const void *b) {
    const IPRouteEntry *ea = (const IPRouteEntry *)a;
    const IPRouteEntry *eb = (const IPRouteEntry *)b;
    if (ea->prefix_len > eb->prefix_len) return -1;
    if (ea->prefix_len < eb->prefix_len) return 1;
    if (ea->metric < eb->metric) return -1;
    if (ea->metric > eb->metric) return 1;
    return 0;
}

void ip_routing_sort_by_prefix(IPRoutingTable *rt) {
    if (!rt || rt->count < 2) return;
    qsort(rt->entries, rt->count, sizeof(IPRouteEntry), compare_prefix);
}

/* L5: Route Aggregation (CIDR Supernetting) */

int ip_routing_aggregate(IPRoutingTable *rt) {
    if (!rt) return -1;
    if (rt->count < 2) return 0;
    int merged = 0;
    for (size_t i = 0; i < rt->count; i++) {
        if (!rt->entries[i].active) continue;
        for (size_t j = i + 1; j < rt->count; j++) {
            if (!rt->entries[j].active) continue;
            IPRouteEntry *a = &rt->entries[i];
            IPRouteEntry *b = &rt->entries[j];
            if (a->prefix_len != b->prefix_len) continue;
            if (a->nexthop != b->nexthop) continue;
            if (a->prefix_len == 0) continue;
            uint8_t pl = a->prefix_len - 1;
            uint32_t mask = ip_prefix_to_mask(pl);
            if ((a->network & mask) == (b->network & mask)) {
                a->prefix_len = pl; a->network &= mask;
                a->metric = (a->metric < b->metric) ? a->metric : b->metric;
                b->active = false; merged++;
            }
        }
    }
    size_t w = 0;
    for (size_t r = 0; r < rt->count; r++) {
        if (rt->entries[r].active) { if (w != r) rt->entries[w] = rt->entries[r]; w++; }
    }
    rt->count = w;
    return merged;
}

/* L4: Split Horizon - prevents count-to-infinity in DV routing */
bool ip_routing_split_horizon(const IPRoutingTable *rt, uint32_t dest, uint32_t nexthop) {
    if (!rt) return true;
    IPRouteEntry r;
    if (ip_routing_lookup(rt, dest, &r) == 0) return (r.nexthop != nexthop);
    return true;
}

void ip_routing_print_entry(const IPRouteEntry *e) {
    if (!e || !e->active) return;
    char net[24];
    ip_cidr_to_str(e->network, e->prefix_len, net, sizeof(net));
    fprintf(stderr, "  %-20s -> ", net);
    if (e->prefix_len == 0) fprintf(stderr, "default (");
    fprintf(stderr, "%u.%u.%u.%u",
            (unsigned)((e->nexthop>>24)&0xFF), (unsigned)((e->nexthop>>16)&0xFF),
            (unsigned)((e->nexthop>>8)&0xFF), (unsigned)(e->nexthop&0xFF));
    if (e->prefix_len == 0) fprintf(stderr, ")");
    fprintf(stderr, " metric=%u\n", e->metric);
}

void ip_routing_print(const IPRoutingTable *rt) {
    if (!rt) return;
    fprintf(stderr, "  [Routing Table] %zu entries:\n", rt->count);
    for (size_t i = 0; i < rt->count; i++)
        if (rt->entries[i].active) ip_routing_print_entry(&rt->entries[i]);
}

/* ============================================================
 * L5: Dijkstra's Shortest Path First Algorithm (OSPF)
 *
 * Theorem (Dijkstra, 1959):
 *   Given weighted directed graph G=(V,E) with non-negative
 *   edge weights, finds shortest paths from source s to all
 *   vertices in O(V^2) time.
 *
 * Algorithm:
 *   Initialize dist[s]=0, dist[v]=INF for v!=s
 *   While unvisited vertices exist:
 *     Pick u = argmin_v{ dist[v] | v unvisited }
 *     Mark u visited
 *     For each neighbor v of u:
 *       if dist[u] + w(u,v) < dist[v]:
 *         dist[v] = dist[u] + w(u,v)
 *         prev[v] = u
 *
 * Reference: E.W. Dijkstra, Numerische Mathematik 1 (1959),
 *   "A Note on Two Problems in Connexion with Graphs"
 * ============================================================ */

int ip_topology_dijkstra(IPTopologyGraph *graph, size_t src_idx,
                         uint32_t *distances, uint32_t *previous) {
    if (!graph || !distances || !previous) return -1;
    if (src_idx >= graph->node_count) return -2;
    const uint32_t INF = 0xFFFFFFFF;
    for (size_t i = 0; i < graph->node_count; i++) {
        distances[i] = INF;
        previous[i] = (uint32_t)graph->node_count;
        graph->nodes[i].visited = false;
    }
    distances[src_idx] = 0;
    for (size_t count = 0; count < graph->node_count; count++) {
        size_t u = graph->node_count;
        uint32_t min_d = INF;
        for (size_t i = 0; i < graph->node_count; i++) {
            if (!graph->nodes[i].visited && distances[i] < min_d) {
                u = i; min_d = distances[i];
            }
        }
        if (u == graph->node_count) break;
        graph->nodes[u].visited = true;
        for (size_t n = 0; n < graph->nodes[u].neighbor_count; n++) {
            size_t v = graph->node_count;
            uint32_t na = graph->nodes[u].neighbors[n];
            for (size_t j = 0; j < graph->node_count; j++) {
                if (graph->nodes[j].addr == na) { v = j; break; }
            }
            if (v >= graph->node_count) continue;
            uint32_t w = graph->nodes[u].cost[n];
            if (distances[u] == INF || w > INF - distances[u]) continue;
            uint32_t alt = distances[u] + w;
            if (alt < distances[v]) { distances[v] = alt; previous[v] = (uint32_t)u; }
        }
    }
    return 0;
}

/* ============================================================
 * L5: Bellman-Ford Distance Vector Algorithm (RIP)
 *
 * Theorem (Bellman 1958, Ford 1956):
 *   Given weighted directed graph G=(V,E), finds shortest
 *   paths from source s to all vertices in O(V*E) time.
 *   Unlike Dijkstra, handles negative edge weights and
 *   detects negative cycles.
 *
 * L4: Count-to-Infinity Problem
 *   In distance vector routing (RIP), when a link fails,
 *   routing loops form where metrics increment until
 *   reaching RIP_INFINITY=16. Mitigation: split horizon,
 *   poison reverse, triggered updates (RFC 1058).
 *
 * Algorithm:
 *   Initialize dist[s]=0, dist[v]=INF for v!=s
 *   Repeat |V|-1 times:
 *     For each edge (u,v) with weight w:
 *       if dist[u]+w < dist[v]: dist[v] = dist[u]+w
 *   (Optional) Check for negative cycles
 * ============================================================ */

int ip_topology_bellman_ford(IPTopologyGraph *graph, size_t src_idx,
                             uint32_t *distances, uint32_t *previous) {
    if (!graph || !distances || !previous) return -1;
    if (src_idx >= graph->node_count) return -2;
    const uint32_t INF = 0xFFFFFFFF;
    for (size_t i = 0; i < graph->node_count; i++) {
        distances[i] = INF;
        previous[i] = (uint32_t)graph->node_count;
    }
    distances[src_idx] = 0;
    for (size_t iter = 1; iter < graph->node_count; iter++) {
        bool updated = false;
        for (size_t u = 0; u < graph->node_count; u++) {
            if (distances[u] == INF) continue;
            for (size_t n = 0; n < graph->nodes[u].neighbor_count; n++) {
                uint32_t na = graph->nodes[u].neighbors[n];
                size_t v = graph->node_count;
                for (size_t j = 0; j < graph->node_count; j++) {
                    if (graph->nodes[j].addr == na) { v = j; break; }
                }
                if (v >= graph->node_count) continue;
                uint32_t w = graph->nodes[u].cost[n];
                if (w > INF - distances[u]) continue;
                uint32_t alt = distances[u] + w;
                if (alt < distances[v]) {
                    if (alt >= IP_ROUTE_INF) {
                        distances[v] = IP_ROUTE_INF;
                        previous[v] = (uint32_t)graph->node_count;
                    } else {
                        distances[v] = alt;
                        previous[v] = (uint32_t)u;
                    }
                    updated = true;
                }
            }
        }
        if (!updated) break;
    }
    return 0;
}

/* L3: Topology Graph Operations */

void ip_topology_init(IPTopologyGraph *graph) {
    if (!graph) return;
    memset(graph, 0, sizeof(IPTopologyGraph));
}

int ip_topology_add_node(IPTopologyGraph *graph, uint32_t addr) {
    if (!graph) return -1;
    if (graph->node_count >= IP_ROUTE_NETWORK_COUNT) return -2;
    for (size_t i = 0; i < graph->node_count; i++)
        if (graph->nodes[i].addr == addr) return 0;
    IPNetworkNode *n = &graph->nodes[graph->node_count];
    n->addr = addr; n->neighbor_count = 0;
    n->visited = false; n->distance = 0;
    n->previous = graph->node_count;
    graph->node_count++;
    return 0;
}

int ip_topology_add_edge(IPTopologyGraph *graph, size_t from, size_t to, uint32_t cost) {
    if (!graph) return -1;
    if (from >= graph->node_count || to >= graph->node_count) return -2;
    IPNetworkNode *n = &graph->nodes[from];
    if (n->neighbor_count >= IP_ROUTE_NETWORK_COUNT) return -3;
    for (size_t i = 0; i < n->neighbor_count; i++) {
        if (n->neighbors[i] == graph->nodes[to].addr) { n->cost[i] = cost; return 0; }
    }
    n->neighbors[n->neighbor_count] = graph->nodes[to].addr;
    n->cost[n->neighbor_count] = cost;
    n->neighbor_count++;
    return 0;
}

void ip_topology_print(const IPTopologyGraph *graph) {
    if (!graph) return;
    fprintf(stderr, "  [Topology] %zu nodes:\n", graph->node_count);
    for (size_t i = 0; i < graph->node_count; i++) {
        fprintf(stderr, "    Node %zu: %u.%u.%u.%u neighbors=%zu\n",
                i,
                (unsigned)((graph->nodes[i].addr>>24)&0xFF),
                (unsigned)((graph->nodes[i].addr>>16)&0xFF),
                (unsigned)((graph->nodes[i].addr>>8)&0xFF),
                (unsigned)(graph->nodes[i].addr&0xFF),
                graph->nodes[i].neighbor_count);
        for (size_t j = 0; j < graph->nodes[i].neighbor_count; j++) {
            fprintf(stderr, "      -> %u.%u.%u.%u cost=%u\n",
                    (unsigned)((graph->nodes[i].neighbors[j]>>24)&0xFF),
                    (unsigned)((graph->nodes[i].neighbors[j]>>16)&0xFF),
                    (unsigned)((graph->nodes[i].neighbors[j]>>8)&0xFF),
                    (unsigned)(graph->nodes[i].neighbors[j]&0xFF),
                    graph->nodes[i].cost[j]);
        }
    }
}
