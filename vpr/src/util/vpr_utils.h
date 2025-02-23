#ifndef VPR_UTILS_H
#define VPR_UTILS_H

#include <vector>
#include <string>

#include "vpr_types.h"
#include "atom_netlist.h"
#include "clustered_netlist.h"
#include "netlist.h"
#include "vtr_vector.h"

#include "arch_util.h"
#include "physical_types_util.h"
#include "rr_graph_utils.h"

class DeviceGrid;

const t_model* find_model(const t_model* models, const std::string& name, bool required = true);
const t_model_ports* find_model_port(const t_model* model, const std::string& name, bool required = true);

void print_tabs(FILE* fpout, int num_tab);

bool is_clb_external_pin(ClusterBlockId blk_id, int pb_pin_id);

bool is_empty_type(t_physical_tile_type_ptr type);
bool is_empty_type(t_logical_block_type_ptr type);

//Returns the corresponding physical type given the logical type as parameter
t_physical_tile_type_ptr physical_tile_type(ClusterBlockId blk);

//Returns the sub tile corresponding to the logical block location within a physical type
int get_sub_tile_index(ClusterBlockId blk);

int get_unique_pb_graph_node_id(const t_pb_graph_node* pb_graph_node);

//Returns the physical class range relative to a block id. This must be called after placement
//as the block id is used to retrieve the information of the used physical tile.
t_class_range get_class_range_for_block(const ClusterBlockId blk_id);

//Returns the physical pin range relative to a block id. This must be called after placement
//as the block id is used to retrieve the information of the used physical tile.
void get_pin_range_for_block(const ClusterBlockId blk_id,
                             int* pin_low,
                             int* pin_high);

void sync_grid_to_blocks();

//Returns a user-friendly architectural identifier for the specified RR node
std::string rr_node_arch_name(int inode);

/**************************************************************
 * Intra-Logic Block Utility Functions
 **************************************************************/

//Class for looking up pb graph pins from block pin indicies
class IntraLbPbPinLookup {
  public:
    IntraLbPbPinLookup(const std::vector<t_logical_block_type>& block_types);
    IntraLbPbPinLookup(const IntraLbPbPinLookup& rhs);
    IntraLbPbPinLookup& operator=(IntraLbPbPinLookup rhs);
    ~IntraLbPbPinLookup();

    //Returns the pb graph pin associated with the specified type (index into block types array) and
    // pb pin index (index into block_pb().pb_route)
    const t_pb_graph_pin* pb_gpin(unsigned int itype, int ipin) const;

    friend void swap(IntraLbPbPinLookup& lhs, IntraLbPbPinLookup& rhs);

  private:
    std::vector<t_logical_block_type> block_types_;

    std::vector<t_pb_graph_pin**> intra_lb_pb_pin_lookup_;
};

//Find the atom pins (driver or sinks) connected to the specified top-level CLB pin
std::vector<AtomPinId> find_clb_pin_connected_atom_pins(ClusterBlockId clb, int logical_pin, const IntraLbPbPinLookup& pb_gpin_lookup);

//Find the atom pin driving to the specified top-level CLB pin
AtomPinId find_clb_pin_driver_atom_pin(ClusterBlockId clb, int logical_pin, const IntraLbPbPinLookup& pb_gpin_lookup);

//Find the atom pins driven by the specified top-level CLB pin
std::vector<AtomPinId> find_clb_pin_sink_atom_pins(ClusterBlockId clb, int logical_pin, const IntraLbPbPinLookup& pb_gpin_lookup);

std::tuple<ClusterNetId, int, int> find_pb_route_clb_input_net_pin(ClusterBlockId clb, int sink_pb_route_id);

//Returns the port matching name within pb_gnode
const t_port* find_pb_graph_port(const t_pb_graph_node* pb_gnode, std::string port_name);

//Returns the graph pin matching name at pin index
const t_pb_graph_pin* find_pb_graph_pin(const t_pb_graph_node* pb_gnode, std::string port_name, int index);

AtomPinId find_atom_pin(ClusterBlockId blk_id, const t_pb_graph_pin* pb_gpin);

//Returns the logical block type which is most common in the device grid
t_logical_block_type_ptr find_most_common_block_type(const DeviceGrid& grid);

//Returns the physical tile type which is most common in the device grid
t_physical_tile_type_ptr find_most_common_tile_type(const DeviceGrid& grid);

//Parses a block_name.port[x:y] (e.g. LAB.data_in[3:10]) pin range specification, if no pin range is specified
//looks-up the block port and fills in the full range
InstPort parse_inst_port(std::string str);

//Returns the block type which is most likely the logic block
t_logical_block_type_ptr infer_logic_block_type(const DeviceGrid& grid);

int get_max_primitives_in_pb_type(t_pb_type* pb_type);
int get_max_depth_of_pb_type(t_pb_type* pb_type);
int get_max_nets_in_pb_type(const t_pb_type* pb_type);
bool primitive_type_feasible(AtomBlockId blk_id, const t_pb_type* cur_pb_type);
t_pb_graph_pin* get_pb_graph_node_pin_from_model_port_pin(const t_model_ports* model_port, const int model_pin, const t_pb_graph_node* pb_graph_node);
const t_pb_graph_pin* find_pb_graph_pin(const AtomNetlist& netlist, const AtomLookup& netlist_lookup, const AtomPinId pin_id);
t_pb_graph_pin* get_pb_graph_node_pin_from_block_pin(ClusterBlockId iblock, int ipin);
vtr::vector<ClusterBlockId, t_pb**> alloc_and_load_pin_id_to_pb_mapping();
void free_pin_id_to_pb_mapping(vtr::vector<ClusterBlockId, t_pb**>& pin_id_to_pb_mapping);

float compute_primitive_base_cost(const t_pb_graph_node* primitive);
int num_ext_inputs_atom_block(AtomBlockId blk_id);

void alloc_and_load_idirect_from_blk_pin(t_direct_inf* directs, int num_directs, int*** idirect_from_blk_pin, int*** direct_type_from_blk_pin);

void parse_direct_pin_name(char* src_string, int line, int* start_pin_index, int* end_pin_index, char* pb_type_name, char* port_name);

void free_pb_stats(t_pb* pb);
void free_pb(t_pb* pb);
void revalid_molecules(const t_pb* pb, const std::multimap<AtomBlockId, t_pack_molecule*>& atom_molecules);

void print_switch_usage();
void print_usage_by_wire_length();

AtomBlockId find_memory_sibling(const t_pb* pb);

/**
 * @brief Syncs the logical block pins corresponding to the input iblk with the corresponding chosen physical tile
 * @param iblk cluster block ID to sync within the assigned physical tile
 *
 * This routine updates the physical pins vector of the place context after the placement step
 * to syncronize the pins related to the logical block with the actual connection interface of
 * the belonging physical tile with the RR graph.
 *
 * This step is required as the logical block can be placed at any compatible sub tile locations
 * within a physical tile.
 * Given that it is possible to have equivalent logical blocks within a specific sub tile, with
 * a different subset of IO pins, the various pins offsets must be correctly computed and assigned
 * to the physical pins vector, so that, when the net RR terminals are computed, the correct physical
 * tile IO pins are selected.
 *
 * This routine uses the x,y and sub_tile coordinates of the clb netlist, and expects those to place each netlist block
 * at a legal location that can accomodate it.
 * It does not check for overuse of locations, therefore it can be used with placements that have resource overuse.
 */
void place_sync_external_block_connections(ClusterBlockId iblk);

//Returns the current tile implemnting blk (if placement is valid), or
//the best expected physical tile the block should use (if no valid placement).
t_physical_tile_type_ptr get_physical_tile_type(const ClusterBlockId blk);

//Returns the physical pin of the tile, related to the given ClusterNedId, and the net pin index
int net_pin_to_tile_pin_index(const ClusterNetId net_id, int net_pin_index);

//Returns the physical pin of the tile, related to the given ClusterPinId
int tile_pin_index(const ClusterPinId pin);

int max_pins_per_grid_tile();

void pretty_print_uint(const char* prefix, size_t value, int num_digits, int scientific_precision);
void pretty_print_float(const char* prefix, double value, int num_digits, int scientific_precision);

void print_timing_stats(std::string name,
                        const t_timing_analysis_profile_info& current,
                        const t_timing_analysis_profile_info& past = t_timing_analysis_profile_info());

std::vector<const t_pb_graph_node*> get_all_pb_graph_node_primitives(const t_pb_graph_node* pb_graph_node);

#endif
