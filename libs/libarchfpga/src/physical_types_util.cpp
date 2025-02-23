#include "vtr_assert.h"
#include "vtr_memory.h"
#include "vtr_util.h"
#include "vtr_log.h"

#include "arch_types.h"
#include "arch_util.h"
#include "arch_error.h"

#include "physical_types_util.h"

/**
 * @brief Data structure that holds information about a physical pin
 *
 * This structure holds the following information on a pin:
 *   - sub_tile_index: index of the sub tile within the physical tile type containing this pin
 *   - capacity_instance: sub tile instance containing this physical pin.
 *                        Each sub tile has a capacity field, which determines how many of its
 *                        instances are present in the belonging physical tile.
 *                        E.g.:
 *                          - The sub tile BUFG has a capacity of 4 within its belonging physical tile CLOCK_TILE.
 *                          - The capacity instance of a pin in the CLOCK_TILE identifies which of the 4 instances
 *                            the pin belongs to.
 *   - port_index: Each sub tile has a set of ports with a variable number of pins. The port_index field identifies
 *                 which port the physical pin belongs to.
 *   - pin_index_in_port: Given that ports can have multiple pins, we need also a field to identify which one of the
 *                        multiple pins of the port corresponds to the physical pin.
 *
 */
struct t_pin_inst_port {
    int sub_tile_index;    // Sub Tile index
    int capacity_instance; // within capacity
    int port_index;        // Port index
    int pin_index_in_port; // Pin's index within the port
};

/******************** Subroutine declarations and definition ****************************/

static std::tuple<int, int, int> get_pin_index_for_inst(t_physical_tile_type_ptr type, int pin_index);
static t_pin_inst_port block_type_pin_index_to_pin_inst(t_physical_tile_type_ptr type, int pin_index);
static int get_sub_block_pin_num_offset(t_physical_tile_type_ptr physical_tile,
                                        const t_sub_tile* curr_sub_tile,
                                        t_logical_block_type_ptr curr_logical_block,
                                        const int curr_relative_cap);

static int get_root_pb_pin_physical_num(t_physical_tile_type_ptr physical_tile,
                                 const t_sub_tile* sub_tile,
                                 t_logical_block_type_ptr logical_block,
                                 int relative_cap,
                                 int logical_pin_num);

static std::tuple<int, int, int> get_pin_index_for_inst(t_physical_tile_type_ptr type, int pin_index) {
    VTR_ASSERT(pin_index < type->num_pins);

    int total_pin_counts = 0;
    int pin_offset = 0;
    for (auto& sub_tile : type->sub_tiles) {
        total_pin_counts += sub_tile.num_phy_pins;

        if (pin_index < total_pin_counts) {
            int pins_per_inst = sub_tile.num_phy_pins / sub_tile.capacity.total();
            int inst_num = (pin_index - pin_offset) / pins_per_inst;
            int inst_index = (pin_index - pin_offset) % pins_per_inst;

            return std::make_tuple(inst_index, inst_num, sub_tile.index);
        }

        pin_offset += sub_tile.num_phy_pins;
    }

    archfpga_throw(__FILE__, __LINE__,
                   "Could not infer the correct pin instance index for %s (pin index: %d)", type->name, pin_index);
}

static t_pin_inst_port block_type_pin_index_to_pin_inst(t_physical_tile_type_ptr type, int pin_index) {
    int sub_tile_index, inst_num;
    std::tie<int, int, int>(pin_index, inst_num, sub_tile_index) = get_pin_index_for_inst(type, pin_index);

    t_pin_inst_port pin_inst_port;
    pin_inst_port.sub_tile_index = sub_tile_index;
    pin_inst_port.capacity_instance = inst_num;
    pin_inst_port.port_index = OPEN;
    pin_inst_port.pin_index_in_port = OPEN;

    for (auto const& port : type->sub_tiles[sub_tile_index].ports) {
        if (pin_index >= port.absolute_first_pin_index && pin_index < port.absolute_first_pin_index + port.num_pins) {
            pin_inst_port.port_index = port.index;
            pin_inst_port.pin_index_in_port = pin_index - port.absolute_first_pin_index;
            break;
        }
    }
    return pin_inst_port;
}

static int get_sub_block_pin_num_offset(t_physical_tile_type_ptr physical_tile,
                                        const t_sub_tile* curr_sub_tile,
                                        t_logical_block_type_ptr curr_logical_block,
                                        const int curr_relative_cap) {
    bool is_found = false;
    int offset = physical_tile->num_pins;
    for(const auto& tmp_sub_tile : physical_tile->sub_tiles) {
        for(int sub_tile_cap = 0; sub_tile_cap < tmp_sub_tile.capacity.total(); sub_tile_cap++) {
            for(auto eq_site : tmp_sub_tile.equivalent_sites) {
                if(&tmp_sub_tile == curr_sub_tile) {
                    if (sub_tile_cap == curr_relative_cap) {
                        if (eq_site == curr_logical_block) {
                            is_found = true;
                        }
                    }
                }
                if(is_found) {
                    break;
                } else {
                    offset += eq_site->pb_type->num_pins;
                }
            }
            if(is_found)
                break;
        }
        if(is_found)
            break;
    }
    return offset;
}

static int get_root_pb_pin_physical_num(t_physical_tile_type_ptr physical_tile,
                                 const t_sub_tile* sub_tile,
                                 t_logical_block_type_ptr logical_block,
                                 int relative_cap,
                                 int logical_pin_num) {
    /* The operation of this function is similar to get_physical_pin_at_sub_tile_location. However, in the mentioned function,
     * sub_tile is not given as an input. Thus, the first sub_tile which contains logical_block as one of its equivalent sites,
     * would be considered at the desirable sub_tile. I consider this approach as an unsafe one. As a result, this function ,which
     * get the sub_tile as an input, is written*/

    int sub_tile_index = sub_tile->index;

    int block_num_pins = physical_tile->sub_tiles[sub_tile_index].num_phy_pins / physical_tile->sub_tiles[sub_tile_index].capacity.total();

    int sub_tile_physical_pin = get_sub_tile_physical_pin(sub_tile_index, physical_tile, logical_block, logical_pin_num);
    sub_tile_physical_pin += relative_cap * block_num_pins;
    return physical_tile->sub_tiles[sub_tile_index].sub_tile_to_tile_pin_indices[sub_tile_physical_pin];
}

/******************** End Subroutine declarations and definition ************************/

int get_sub_tile_physical_pin(int sub_tile_index,
                              t_physical_tile_type_ptr physical_tile,
                              t_logical_block_type_ptr logical_block,
                              int pin) {
    t_logical_pin logical_pin(pin);

    const auto& direct_map = physical_tile->tile_block_pin_directs_map.at(logical_block->index).at(sub_tile_index);
    auto result = direct_map.find(logical_pin);

    if (result == direct_map.end()) {
        archfpga_throw(__FILE__, __LINE__,
                       "Couldn't find the corresponding physical tile pin of the logical block pin %d."
                       "Physical Tile Type: %s, Logical Block Type: %s.\n",
                       pin, physical_tile->name, logical_block->name);
    }

    return result->second.pin;
}

int get_logical_block_physical_sub_tile_index(t_physical_tile_type_ptr physical_tile,
                                              t_logical_block_type_ptr logical_block) {
    int sub_tile_index = OPEN;
    for (const auto& sub_tile : physical_tile->sub_tiles) {
        auto eq_sites = sub_tile.equivalent_sites;
        auto it = std::find(eq_sites.begin(), eq_sites.end(), logical_block);
        if (it != eq_sites.end()) {
            sub_tile_index = sub_tile.index;
        }
    }

    if (sub_tile_index == OPEN) {
        archfpga_throw(__FILE__, __LINE__,
                       "Found no instances of logical block type '%s' within physical tile type '%s'. ",
                       logical_block->name, physical_tile->name);
    }

    return sub_tile_index;
}

int get_physical_pin(t_physical_tile_type_ptr physical_tile,
                     t_logical_block_type_ptr logical_block,
                     int pin) {
    int sub_tile_index = get_logical_block_physical_sub_tile_index(physical_tile, logical_block);

    if (sub_tile_index == OPEN) {
        archfpga_throw(__FILE__, __LINE__,
                       "Couldn't find the corresponding physical tile type pin of the logical block type pin %d.",
                       pin);
    }

    int sub_tile_physical_pin = get_sub_tile_physical_pin(sub_tile_index, physical_tile, logical_block, pin);
    return physical_tile->sub_tiles[sub_tile_index].sub_tile_to_tile_pin_indices[sub_tile_physical_pin];
}

int get_logical_block_physical_sub_tile_index(t_physical_tile_type_ptr physical_tile,
                                              t_logical_block_type_ptr logical_block,
                                              int sub_tile_capacity) {
    int sub_tile_index = OPEN;
    for (const auto& sub_tile : physical_tile->sub_tiles) {
        auto eq_sites = sub_tile.equivalent_sites;
        auto it = std::find(eq_sites.begin(), eq_sites.end(), logical_block);
        if (it != eq_sites.end()
            && (sub_tile.capacity.is_in_range(sub_tile_capacity))) {
            sub_tile_index = sub_tile.index;
            break;
        }
    }

    if (sub_tile_index == OPEN) {
        archfpga_throw(__FILE__, __LINE__,
                       "Found no instances of logical block type '%s' within physical tile type '%s'. ",
                       logical_block->name, physical_tile->name);
    }

    return sub_tile_index;
}

/**
 * This function returns the most common physical tile type given a logical block
 */
t_physical_tile_type_ptr pick_physical_type(t_logical_block_type_ptr logical_block) {
    return logical_block->equivalent_tiles[0];
}

t_logical_block_type_ptr pick_logical_type(t_physical_tile_type_ptr physical_tile) {
    return physical_tile->sub_tiles[0].equivalent_sites[0];
}

bool is_tile_compatible(t_physical_tile_type_ptr physical_tile, t_logical_block_type_ptr logical_block) {
    const auto& equivalent_tiles = logical_block->equivalent_tiles;
    return std::find(equivalent_tiles.begin(), equivalent_tiles.end(), physical_tile) != equivalent_tiles.end();
}

bool is_sub_tile_compatible(t_physical_tile_type_ptr physical_tile, t_logical_block_type_ptr logical_block, int sub_tile_loc) {
    bool capacity_compatible = false;
    for (auto& sub_tile : physical_tile->sub_tiles) {
        auto result = std::find(sub_tile.equivalent_sites.begin(), sub_tile.equivalent_sites.end(), logical_block);

        if (sub_tile.capacity.is_in_range(sub_tile_loc) && result != sub_tile.equivalent_sites.end()) {
            capacity_compatible = true;
            break;
        }
    }

    return capacity_compatible && is_tile_compatible(physical_tile, logical_block);
}

int get_physical_pin_at_sub_tile_location(t_physical_tile_type_ptr physical_tile,
                                          t_logical_block_type_ptr logical_block,
                                          int sub_tile_capacity,
                                          int pin) {
    int sub_tile_index = get_logical_block_physical_sub_tile_index(physical_tile, logical_block, sub_tile_capacity);

    if (sub_tile_index == OPEN) {
        archfpga_throw(__FILE__, __LINE__,
                       "Couldn't find the corresponding physical tile type pin of the logical block type pin %d.",
                       pin);
    }

    int sub_tile_physical_pin = get_sub_tile_physical_pin(sub_tile_index, physical_tile, logical_block, pin);

    /* Find the relative capacity of the logical_block in this sub tile */
    int relative_capacity = sub_tile_capacity - physical_tile->sub_tiles[sub_tile_index].capacity.low;

    /* Find the number of pins per block in the equivalent site list
     * of the sub tile. Otherwise, the current logical block may have smaller/larger number of pins
     * than other logical blocks that can be placed in the sub-tile. This will lead to an error
     * when computing the pin index!
     */
    int block_num_pins = physical_tile->sub_tiles[sub_tile_index].num_phy_pins / physical_tile->sub_tiles[sub_tile_index].capacity.total();

    return relative_capacity * block_num_pins
           + physical_tile->sub_tiles[sub_tile_index].sub_tile_to_tile_pin_indices[sub_tile_physical_pin];
}

int get_max_num_pins(t_logical_block_type_ptr logical_block) {
    int max_num_pins = 0;

    for (auto physical_tile : logical_block->equivalent_tiles) {
        max_num_pins = std::max(max_num_pins, physical_tile->num_pins);
    }

    return max_num_pins;
}

//Returns the pin class associated with the specified pin_index_in_port within the port port_name on type
int find_pin_class(t_physical_tile_type_ptr type, std::string port_name, int pin_index_in_port, e_pin_type pin_type) {
    int iclass = OPEN;

    int ipin = find_pin(type, port_name, pin_index_in_port);

    if (ipin != OPEN) {
        iclass = type->pin_class[ipin];

        if (iclass != OPEN) {
            VTR_ASSERT(type->class_inf[iclass].type == pin_type);
        }
    }
    return iclass;
}

int find_pin(t_physical_tile_type_ptr type, std::string port_name, int pin_index_in_port) {
    int ipin = OPEN;
    int port_base_ipin = 0;
    int num_pins = OPEN;
    int pin_offset = 0;

    bool port_found = false;
    for (const auto& sub_tile : type->sub_tiles) {
        for (const auto& port : sub_tile.ports) {
            if (0 == strcmp(port.name, port_name.c_str())) {
                port_found = true;
                num_pins = port.num_pins;
                break;
            }

            port_base_ipin += port.num_pins;
        }

        if (port_found) {
            break;
        }

        port_base_ipin = 0;
        pin_offset += sub_tile.num_phy_pins;
    }

    if (num_pins != OPEN) {
        VTR_ASSERT(pin_index_in_port < num_pins);

        ipin = port_base_ipin + pin_index_in_port + pin_offset;
    }

    return ipin;
}

std::pair<int, int> get_capacity_location_from_physical_pin(t_physical_tile_type_ptr physical_tile, int pin) {
    int pins_to_remove = 0;
    for (const auto& sub_tile : physical_tile->sub_tiles) {
        auto capacity = sub_tile.capacity;
        int sub_tile_num_pins = sub_tile.num_phy_pins;
        int sub_tile_pin = pin - pins_to_remove;

        if (sub_tile_pin < sub_tile_num_pins) {
            int rel_capacity = sub_tile_pin / (sub_tile_num_pins / capacity.total());
            int rel_pin = sub_tile_pin % (sub_tile_num_pins / capacity.total());

            return std::pair<int, int>(rel_capacity + capacity.low, rel_pin);
        }

        pins_to_remove += sub_tile_num_pins;
    }

    archfpga_throw(__FILE__, __LINE__,
                   "Couldn't find sub tile that contains the pin %d in physical tile %s.\n",
                   pin, physical_tile->name);
}

int get_physical_pin_from_capacity_location(t_physical_tile_type_ptr physical_tile, int relative_pin, int capacity_location) {
    int pins_to_add = 0;
    for (auto sub_tile : physical_tile->sub_tiles) {
        auto capacity = sub_tile.capacity;
        int rel_capacity = capacity_location - capacity.low;
        int num_inst_pins = sub_tile.num_phy_pins / capacity.total();

        if (capacity.is_in_range(capacity_location)) {
            return pins_to_add + num_inst_pins * rel_capacity + relative_pin;
        }

        pins_to_add += sub_tile.num_phy_pins;
    }

    archfpga_throw(__FILE__, __LINE__,
                   "Couldn't find sub tile that contains the relative pin %d at the capacity location %d in physical tile %s.\n",
                   relative_pin, capacity_location, physical_tile->name);
}
bool is_opin(int ipin, t_physical_tile_type_ptr type) {
    /* Returns true if this clb pin is an output, false otherwise. */

    if (ipin > type->num_pins) {
        //Not a top level pin
        return false;
    }

    int iclass = type->pin_class[ipin];

    if (type->class_inf[iclass].type == DRIVER)
        return true;
    else
        return false;
}

// TODO: Remove is_input_type / is_output_type / is_io_type as part of
// https://github.com/verilog-to-routing/vtr-verilog-to-routing/issues/1193
bool is_input_type(t_physical_tile_type_ptr type) {
    return type->is_input_type;
}

bool is_output_type(t_physical_tile_type_ptr type) {
    return type->is_output_type;
}

bool is_io_type(t_physical_tile_type_ptr type) {
    return is_input_type(type)
           || is_output_type(type);
}

std::string block_type_pin_index_to_name(t_physical_tile_type_ptr type, int pin_index) {
    VTR_ASSERT(pin_index < type->num_pins);

    std::string pin_name = type->name;

    int sub_tile_index, inst_num;
    std::tie<int, int, int>(pin_index, inst_num, sub_tile_index) = get_pin_index_for_inst(type, pin_index);

    if (type->sub_tiles[sub_tile_index].capacity.total() > 1) {
        pin_name += "[" + std::to_string(inst_num) + "]";
    }

    pin_name += ".";

    for (auto const& port : type->sub_tiles[sub_tile_index].ports) {
        if (pin_index >= port.absolute_first_pin_index && pin_index < port.absolute_first_pin_index + port.num_pins) {
            //This port contains the desired pin index
            int index_in_port = pin_index - port.absolute_first_pin_index;
            pin_name += port.name;
            pin_name += "[" + std::to_string(index_in_port) + "]";
            return pin_name;
        }
    }

    return "<UNKOWN>";
}

std::vector<std::string> block_type_class_index_to_pin_names(t_physical_tile_type_ptr type, int class_index) {
    VTR_ASSERT(class_index < (int)type->class_inf.size());

    auto class_inf = type->class_inf[class_index];

    std::vector<t_pin_inst_port> pin_info;
    for (int ipin = 0; ipin < class_inf.num_pins; ++ipin) {
        int pin_index = class_inf.pinlist[ipin];
        pin_info.push_back(block_type_pin_index_to_pin_inst(type, pin_index));
    }

    auto cmp = [](const t_pin_inst_port& lhs, const t_pin_inst_port& rhs) {
        return std::tie(lhs.capacity_instance, lhs.port_index, lhs.pin_index_in_port)
               < std::tie(rhs.capacity_instance, rhs.port_index, rhs.pin_index_in_port);
    };

    //Ensure all the pins are in order
    std::sort(pin_info.begin(), pin_info.end(), cmp);

    //Determine ranges for each capacity instance and port pair
    std::map<std::tuple<int, int, int>, std::pair<int, int>> pin_ranges;
    for (const auto& pin_inf : pin_info) {
        auto key = std::make_tuple(pin_inf.sub_tile_index, pin_inf.capacity_instance, pin_inf.port_index);
        if (!pin_ranges.count(key)) {
            pin_ranges[key].first = pin_inf.pin_index_in_port;
            pin_ranges[key].second = pin_inf.pin_index_in_port;
        } else {
            VTR_ASSERT(pin_ranges[key].second == pin_inf.pin_index_in_port - 1);
            pin_ranges[key].second = pin_inf.pin_index_in_port;
        }
    }

    //Format pin ranges
    std::vector<std::string> pin_names;
    for (auto kv : pin_ranges) {
        auto type_port = kv.first;
        auto pins = kv.second;

        int isub_tile, icapacity, iport;
        std::tie<int, int, int>(isub_tile, icapacity, iport) = type_port;

        int ipin_start = pins.first;
        int ipin_end = pins.second;

        auto& sub_tile = type->sub_tiles[isub_tile];

        std::string pin_name;
        if (ipin_start == ipin_end) {
            pin_name = vtr::string_fmt("%s[%d].%s[%d]",
                                       type->name,
                                       icapacity,
                                       sub_tile.ports[iport].name,
                                       ipin_start);
        } else {
            pin_name = vtr::string_fmt("%s[%d].%s[%d:%d]",
                                       type->name,
                                       icapacity,
                                       sub_tile.ports[iport].name,
                                       ipin_start,
                                       ipin_end);
        }

        pin_names.push_back(pin_name);
    }

    return pin_names;
}

const t_physical_tile_port* get_port_by_name(t_sub_tile* sub_tile, const char* port_name) {
    for (auto port : sub_tile->ports) {
        if (0 == strcmp(port.name, port_name)) {
            return &sub_tile->ports[port.index];
        }
    }

    return nullptr;
}

const t_port* get_port_by_name(t_logical_block_type_ptr type, const char* port_name) {
    auto pb_type = type->pb_type;

    for (int i = 0; i < pb_type->num_ports; i++) {
        auto port = pb_type->ports[i];
        if (0 == strcmp(port.name, port_name)) {
            return &pb_type->ports[port.index];
        }
    }

    return nullptr;
}

const t_physical_tile_port* get_port_by_pin(const t_sub_tile* sub_tile, int pin) {
    for (auto port : sub_tile->ports) {
        if (pin >= port.absolute_first_pin_index && pin < port.absolute_first_pin_index + port.num_pins) {
            return &sub_tile->ports[port.index];
        }
    }

    return nullptr;
}

const t_port* get_port_by_pin(t_logical_block_type_ptr type, int pin) {
    auto pb_type = type->pb_type;

    for (int i = 0; i < pb_type->num_ports; i++) {
        auto port = pb_type->ports[i];
        if (pin >= port.absolute_first_pin_index && pin < port.absolute_first_pin_index + port.num_pins) {
            return &pb_type->ports[port.index];
        }
    }

    return nullptr;
}

const t_port* get_port_by_logical_pin_num(t_logical_block_type_ptr type, int pin) {
    auto pb_pin = type->pb_pin_idx_bimap[pin];
    return pb_pin->port;
}

int get_pb_pin_physical_num(t_physical_tile_type_ptr physical_tile,
                   const t_sub_tile* sub_tile,
                   t_logical_block_type_ptr logical_block,
                   int relative_cap,
                   const t_pb_graph_pin* pin) {
    int pin_ptc;
    /* There is special mapping between logical_block on the root-level block and the physical pins on the tiles.
     * Thus, special care needs to be taken when the physical index of a root-level pb_graph_pin is being calculated */
    if(pin->is_root_block_pin()){
        pin_ptc = get_root_pb_pin_physical_num(physical_tile,
                                               sub_tile,
                                               logical_block,
                                               relative_cap,
                                               pin->port->absolute_first_pin_index + pin->pin_number);
    } else{
        /* There isn't any mapping between sub-pb-blocks pins and the physical pins.
         * However, since we want to retain the uniqueness of physical indices at the tile level, we shift the logical_indices,
         * which are unique at logical level, by the number of physical pins. By doing so, we avoid collision in the case that the
         * number of physical pins is higher than those of root-level logical pins. The more conservative approach would be to offset
         * the sub-blocks' pin id by the difference between the number of pins on root-level logical pin and the physical tile */
        int logical_pin_num = logical_block->pb_pin_idx_bimap[pin];
        int offset = get_sub_block_pin_num_offset(physical_tile,
                                                  sub_tile,
                                                  logical_block,
                                                  relative_cap);
        pin_ptc = logical_pin_num + offset;
    }

    return pin_ptc;
}

/* Access information related to pin classes */

/** get information given class physical num **/

const t_sub_tile* get_sub_tile_from_class_physical_num(t_physical_tile_type_ptr physical_tile, int physical_class_num) {

    for(auto& sub_tile : physical_tile->sub_tiles) {
        int start_physical_class_num = get_primitives_class_physical_num(physical_tile,
                                                                         &sub_tile,
                                                                         sub_tile.equivalent_sites[0],
                                                                         0,
                                                                         0);
        int end_physical_class_num = start_physical_class_num;
        for (int sub_tile_cap = 0; sub_tile_cap < sub_tile.capacity.total(); sub_tile_cap++) {
            for (auto eq_site : sub_tile.equivalent_sites) {
                end_physical_class_num += (int)eq_site->primitive_class_inf.size();
            }
        }

        if(physical_class_num >= start_physical_class_num && physical_class_num < end_physical_class_num) {
            return &sub_tile;
        }
    }

    return nullptr;
}

int get_sub_tile_cap_from_class_physical_num(t_physical_tile_type_ptr physical_tile, int physical_class_num) {

    for(auto& sub_tile : physical_tile->sub_tiles) {
        for (int sub_tile_cap = 0; sub_tile_cap < sub_tile.capacity.total(); sub_tile_cap++) {
            int start_physical_class_num = get_primitives_class_physical_num(physical_tile,
                                                                             &sub_tile,
                                                                             sub_tile.equivalent_sites[0],
                                                                             sub_tile_cap,
                                                                             0);
            int end_physical_class_num = start_physical_class_num;
            for (auto eq_site : sub_tile.equivalent_sites) {
                end_physical_class_num += (int)eq_site->primitive_class_inf.size();
            }

            if (physical_class_num >= start_physical_class_num && physical_class_num < end_physical_class_num) {
                return sub_tile_cap;
            }
        }
    }

    return -1;
}

t_logical_block_type_ptr get_logical_block_from_class_physical_num(t_physical_tile_type_ptr physical_tile, int physical_class_num) {

    for(auto& sub_tile : physical_tile->sub_tiles) {
        for (int sub_tile_cap = 0; sub_tile_cap < sub_tile.capacity.total(); sub_tile_cap++) {
            for (auto eq_site : sub_tile.equivalent_sites) {
                int start_physical_class_num = get_primitives_class_physical_num(physical_tile,
                                                                                 &sub_tile,
                                                                                 eq_site,
                                                                                 sub_tile_cap,
                                                                                 0);
                int end_physical_class_num = start_physical_class_num + (int)eq_site->primitive_class_inf.size();
                if(physical_class_num >= start_physical_class_num && physical_class_num < end_physical_class_num) {
                    return eq_site;
                }
            }
        }
    }

    return nullptr;
}

int get_class_logical_num_from_class_physical_num(t_physical_tile_type_ptr physical_tile, int physical_class_num) {

    auto sub_tile = get_sub_tile_from_class_physical_num(physical_tile, physical_class_num);
    auto sub_tile_cap = get_sub_tile_cap_from_class_physical_num(physical_tile, physical_class_num);
    VTR_ASSERT(sub_tile_cap != -1);
    auto logical_block = get_logical_block_from_class_physical_num(physical_tile, physical_class_num);
    int start_physical_class_num = get_primitives_class_physical_num(physical_tile,
                                                                     sub_tile,
                                                                     logical_block,
                                                                     sub_tile_cap,
                                                                     0);
    VTR_ASSERT(start_physical_class_num != -1);
    return physical_class_num - start_physical_class_num;

}

e_pin_type get_class_type_from_class_physical_num(t_physical_tile_type_ptr physical_tile, int physical_class_num, bool is_flat) {
    e_pin_type class_type;

    if(is_flat) {
        auto logical_block = get_logical_block_from_class_physical_num(physical_tile, physical_class_num);
        int class_logical_num = get_class_logical_num_from_class_physical_num(physical_tile, physical_class_num);
        class_type = logical_block->primitive_class_inf[class_logical_num].type;

    } else {
        VTR_ASSERT(is_flat == false);
        class_type = physical_tile->class_inf[physical_class_num].type;
    }

    return class_type;
}

int get_class_num_pins_from_class_physical_num(t_physical_tile_type_ptr physical_tile, int physical_class_num, bool is_flat) {
    int num_pins = -1;

    if(is_flat) {
        auto logical_block = get_logical_block_from_class_physical_num(physical_tile, physical_class_num);
        int class_logical_num = get_class_logical_num_from_class_physical_num(physical_tile, physical_class_num);
        num_pins = logical_block->primitive_class_inf[class_logical_num].num_pins;


    } else {
        VTR_ASSERT(is_flat == false);
        num_pins = physical_tile->class_inf[physical_class_num].num_pins;
    }

    return num_pins;
}

/** **/

/** get classes under different blocks **/

std::unordered_map<int, const t_class*> get_tile_primitive_classes_map(t_physical_tile_type_ptr physical_tile) {

    std::unordered_map<int, const t_class*> primitive_classes_map;

    for(const auto& sub_tile : physical_tile->sub_tiles) {
        for(int sub_tile_cap = 0; sub_tile_cap < sub_tile.capacity.total(); sub_tile_cap++) {
            auto sub_tile_primitive_classes_map = get_sub_tile_primitive_classes_map(physical_tile,
                                                                                     &sub_tile,
                                                                                     sub_tile_cap);
            primitive_classes_map.insert(sub_tile_primitive_classes_map.begin(), sub_tile_primitive_classes_map.end());
        }
    }

    return primitive_classes_map;
}

std::unordered_map<int, const t_class*> get_sub_tile_primitive_classes_map(t_physical_tile_type_ptr physical_tile,
                                                                           const t_sub_tile* sub_tile,
                                                                           int relative_cap) {

    std::unordered_map<int, const t_class*> primitive_classes_map;

    for(auto eq_site : sub_tile->equivalent_sites) {
        auto logical_block_primitive_classes_map = get_logical_block_primitive_classes_map(physical_tile,
                                                                                           sub_tile,
                                                                                           eq_site,
                                                                                           relative_cap);

        primitive_classes_map.insert(logical_block_primitive_classes_map.begin(), logical_block_primitive_classes_map.end());
    }

    return primitive_classes_map;
}

std::unordered_map<int, const t_class*> get_logical_block_primitive_classes_map(t_physical_tile_type_ptr physical_tile,
                                                                                const t_sub_tile* sub_tile,
                                                                                t_logical_block_type_ptr logical_block,
                                                                                int relative_cap) {
    std::unordered_map<int, const t_class*> primitive_classes;
    auto& logical_block_primitives_classes = logical_block->primitive_class_inf;
    for(int class_num = 0; class_num < (int)logical_block_primitives_classes.size(); class_num++) {
        int tile_class_num = get_primitives_class_physical_num(physical_tile,
                                                               sub_tile,
                                                               logical_block,
                                                               relative_cap,
                                                               class_num);
        VTR_ASSERT(tile_class_num != -1);
        primitive_classes.insert(std::make_pair(tile_class_num, &logical_block_primitives_classes[class_num]));
    }
    return primitive_classes;
}

std::unordered_map<int, const t_class*> get_primitive_block_classes_map(t_physical_tile_type_ptr physical_tile,
                                                                        const t_sub_tile* sub_tile,
                                                                        t_logical_block_type_ptr logical_block,
                                                                        int sub_tile_relative_cap,
                                                                        const t_pb_graph_node* primitive_pb_graph_node) {
    VTR_ASSERT(primitive_pb_graph_node->is_primitive());
    std::unordered_map<int, const t_class*> block_classes_map;
    const std::unordered_map<const t_pb_graph_pin*, int>& pb_pin_class_map = logical_block->pb_pin_class_map;
    const std::vector<t_class>& primitive_class_inf = logical_block->primitive_class_inf;

    for(int port_type = 0; port_type < 3; port_type++) {
        int num_ports;
        int* ports_num_pins;
        t_pb_graph_pin** pins;
        if (port_type == 0) {
            // Add internal edges for connected to the current block input pins
            num_ports = primitive_pb_graph_node->num_input_ports;
            ports_num_pins = primitive_pb_graph_node->num_input_pins;
            pins = primitive_pb_graph_node->input_pins;
        } else if(port_type == 1){
            // Add internal edges for connected to the current block output pins

            num_ports = primitive_pb_graph_node->num_output_ports;
            ports_num_pins = primitive_pb_graph_node->num_output_pins;
            pins = primitive_pb_graph_node->output_pins;
        } else {
            VTR_ASSERT(port_type == 2);

            num_ports = primitive_pb_graph_node->num_clock_ports;
            ports_num_pins = primitive_pb_graph_node->num_clock_pins;
            pins = primitive_pb_graph_node->clock_pins;
        }
        std::unordered_set<int> seen_classes;
        for(int port_idx = 0; port_idx < num_ports; port_idx++) {
            int num_pins = ports_num_pins[port_idx];
            for (int pin_idx = 0; pin_idx < num_pins; pin_idx++) {
                const t_pb_graph_pin* tmp_pin = &(pins[port_idx][pin_idx]);
                auto tmp_class_idx = pb_pin_class_map.at(tmp_pin);
                auto tmp_class = &primitive_class_inf[tmp_class_idx];
                if(seen_classes.count(tmp_class_idx) == 0) {
                    int class_physical_num = get_primitives_class_physical_num(physical_tile,
                                                                               sub_tile,
                                                                               logical_block,
                                                                               sub_tile_relative_cap,
                                                                               tmp_class_idx);
                    seen_classes.insert(tmp_class_idx);
                    block_classes_map.insert(std::make_pair(class_physical_num, tmp_class));
                }
            }
        }
    }

    return block_classes_map;

}

/* */


/* Access information related to pins */

/** get information given pin physical number **/
const t_sub_tile* get_sub_tile_from_pin_physical_num(t_physical_tile_type_ptr physical_tile, int physical_num) {

    const t_sub_tile* target_sub_tile = nullptr;

    if(physical_num < physical_tile->num_pins) { /* pin located on the root-level block */
        int num_seen_root_pins = 0;
        for(const auto& sub_tile : physical_tile->sub_tiles) {
            if(physical_num < (num_seen_root_pins + sub_tile.num_phy_pins))
                target_sub_tile = &sub_tile;
            else
                num_seen_root_pins += sub_tile.num_phy_pins;
        }
    } else { /* This physical_num corresponds to a pin located inside a block */
        int num_seen_root_pins = physical_tile->num_pins;
        for(const auto& sub_tile : physical_tile->sub_tiles) {
            int num_internal_sub_tile_pin = get_total_num_sub_tile_pins(&sub_tile) - sub_tile.num_phy_pins;
            if(physical_num < (num_seen_root_pins + num_internal_sub_tile_pin))
                target_sub_tile = &sub_tile;
            else
                num_seen_root_pins += num_internal_sub_tile_pin;
        }
    }

    VTR_ASSERT(target_sub_tile != nullptr);
    return target_sub_tile;
}

int get_sub_tile_cap_from_pin_physical_num(t_physical_tile_type_ptr physical_tile, int physical_num) {

    int target_cap = -1;
    auto sub_tile = get_sub_tile_from_pin_physical_num(physical_tile, physical_num);

    if(physical_num < physical_tile->num_pins) {
        int block_num_pins = (sub_tile->num_phy_pins / sub_tile->capacity.total());
        target_cap = physical_num / block_num_pins;
    } else {
        int num_seen_pins = physical_tile->num_pins;
        int block_num_pins = get_total_num_sub_tile_pins(sub_tile) / sub_tile->capacity.total();
        for (const auto& tmp_sub_tile : physical_tile->sub_tiles) {
            int num_internal_sub_tile_pin = get_total_num_sub_tile_pins(&tmp_sub_tile) - tmp_sub_tile.num_phy_pins;
            if (sub_tile == &tmp_sub_tile) {
                break;
            } else {
                num_seen_pins += num_internal_sub_tile_pin;
            }
        }
        for (int sub_tile_cap = 0; sub_tile_cap < sub_tile->capacity.total(); sub_tile_cap++) {
            if (physical_num < (num_seen_pins + block_num_pins)) {
                target_cap = sub_tile_cap;
                break;
            } else {
                num_seen_pins += block_num_pins;
            }
        }
    }

    VTR_ASSERT(target_cap != -1);
    return target_cap;

}

t_logical_block_type_ptr get_logical_block_from_pin_physical_num(t_physical_tile_type_ptr physical_tile, int physical_num) {

    t_logical_block_type_ptr target_logical_block = nullptr;
    auto sub_tile = get_sub_tile_from_pin_physical_num(physical_tile, physical_num);
    auto sub_tile_cap = get_sub_tile_cap_from_pin_physical_num(physical_tile, physical_num);
    int num_seen_pins = physical_tile->num_pins;

    for(const auto& tmp_sub_tile : physical_tile->sub_tiles) {
        int num_internal_sub_tile_pin = get_total_num_sub_tile_pins(&tmp_sub_tile) - tmp_sub_tile.num_phy_pins;
        if(sub_tile == &tmp_sub_tile)
            num_seen_pins += num_internal_sub_tile_pin;
        else
            break;
    }

    int block_num_pins = get_total_num_sub_tile_pins(sub_tile) / sub_tile->capacity.total();
    for(int cap = 0; cap < sub_tile_cap; cap++) {
        num_seen_pins += block_num_pins;
    }

    for(auto tmp_logical_block : sub_tile->equivalent_sites) {
        if(physical_num < num_seen_pins) {
            target_logical_block = tmp_logical_block;
            break;
        } else {
            num_seen_pins += tmp_logical_block->pb_type->num_pins;
        }
    }

    VTR_ASSERT(target_logical_block != nullptr);
    return target_logical_block;
}

const t_pb_graph_pin* get_pb_pin_from_pin_physical_num (t_physical_tile_type_ptr physical_tile, int physical_num) {
    auto logical_block = get_logical_block_from_pin_physical_num(physical_tile, physical_num);
    int logical_num = get_pin_logical_num_from_pin_physical_num(physical_tile, physical_num);
    return logical_block->pb_pin_idx_bimap[logical_num];

}

int get_pin_logical_num_from_pin_physical_num(t_physical_tile_type_ptr physical_tile, int physical_num) {

    auto sub_tile = get_sub_tile_from_pin_physical_num(physical_tile, physical_num);
    auto relative_cap = get_sub_tile_cap_from_pin_physical_num(physical_tile, physical_num);
    auto logical_block = get_logical_block_from_pin_physical_num(physical_tile, physical_num);

    int tile_num_pins = physical_tile->num_pins;
    int sub_tile_index = sub_tile->index;
    int block_num_pins = physical_tile->sub_tiles[sub_tile_index].num_phy_pins / physical_tile->sub_tiles[sub_tile_index].capacity.total();

    int pin_logical_num;
    if(physical_num >= tile_num_pins) { /* pin is not on the root-level block */
        int offset = get_sub_block_pin_num_offset(physical_tile,
                                                  sub_tile,
                                                  logical_block,
                                                  relative_cap);
        pin_logical_num = physical_num - offset;
    } else { /* pin is on the root-level block */
        auto direct_map = physical_tile->tile_block_pin_directs_map.at(logical_block->index).at(sub_tile_index);
        auto result = std::find(std::begin(sub_tile->sub_tile_to_tile_pin_indices),
                                end(sub_tile->sub_tile_to_tile_pin_indices), physical_num);
        VTR_ASSERT(result != end(sub_tile->sub_tile_to_tile_pin_indices));
        int sub_tile_pin_num = std::distance(std::begin(sub_tile->sub_tile_to_tile_pin_indices), result);
        sub_tile_pin_num -= relative_cap * block_num_pins;
        pin_logical_num = direct_map[t_physical_pin(sub_tile_pin_num)].pin;
    }

    return pin_logical_num;
}

/** **/

/* */

int get_total_num_sub_tile_pins(const t_sub_tile* sub_tile) {
    int num_pins = 0;
    for(auto eq_site : sub_tile->equivalent_sites) {
        num_pins += eq_site->pb_pin_idx_bimap.size();
    }
    num_pins *= sub_tile->capacity.total();
    return num_pins;
}

int get_total_num_tile_pins(t_physical_tile_type_ptr tile) {
    int num_pins = 0;
    for(const t_sub_tile& sub_tile : tile->sub_tiles) {
        for(auto eq_site : sub_tile.equivalent_sites) {
            num_pins += ((eq_site->pb_type->num_pins)*sub_tile.capacity.total());
        }
    }

    return num_pins;
}

int get_tile_max_ptc(t_physical_tile_type_ptr tile, bool is_flat) {

    if(is_flat) {
        return tile->num_pins + get_total_num_tile_pins(tile);
    } else {
        VTR_ASSERT(is_flat == false);
        return tile->num_pins;
    }

}



int get_primitives_class_physical_num(t_physical_tile_type_ptr physical_tile,
                                      const t_sub_tile* curr_sub_tile,
                                      t_logical_block_type_ptr curr_logical_block,
                                      int curr_relative_cap,
                                      int logical_primitive_class_num) {

    int num_seen_primitive_class = 0;
    for(auto& sub_tile : physical_tile->sub_tiles) {
        for (int sub_tile_cap = 0; sub_tile_cap < sub_tile.capacity.total(); sub_tile_cap++) {
            for (auto eq_site : sub_tile.equivalent_sites) {
                if(&sub_tile == curr_sub_tile) {
                    if (eq_site == curr_logical_block) {
                        if (curr_relative_cap == sub_tile_cap)
                            return num_seen_primitive_class + logical_primitive_class_num;
                    }
                }
                num_seen_primitive_class += (int)eq_site->primitive_class_inf.size();
            }
        }
    }

    return -1;
}

int get_tile_num_primitive_classes(t_physical_tile_type_ptr physical_tile) {

    int num_classes = 0;

    for(const auto& sub_tile : physical_tile->sub_tiles) {
        for(auto eq_site : sub_tile.equivalent_sites) {
            num_classes += (int)(eq_site->primitive_class_inf.size()*sub_tile.capacity.total());
        }
    }
    return num_classes;
}
/* */

std::vector<const t_pb_graph_pin*> get_pb_graph_node_pins(const t_pb_graph_node* pb_graph_node) {
    std::vector<const t_pb_graph_pin*> pins;

    for (int port_type_idx = 0; port_type_idx < 3; port_type_idx++) {
        t_pb_graph_pin** pb_pins;
        int num_ports;
        int* num_pins;

        if (port_type_idx == 0) {
            pb_pins = pb_graph_node->input_pins;
            num_ports = pb_graph_node->num_input_ports;
            num_pins = pb_graph_node->num_input_pins;

        } else if (port_type_idx == 1) {
            pb_pins = pb_graph_node->output_pins;
            num_ports = pb_graph_node->num_output_ports;
            num_pins = pb_graph_node->num_output_pins;
        } else {
            VTR_ASSERT(port_type_idx == 2);
            pb_pins = pb_graph_node->clock_pins;
            num_ports = pb_graph_node->num_clock_ports;
            num_pins = pb_graph_node->num_clock_pins;
        }

        for (int port_idx = 0; port_idx < num_ports; port_idx++) {
            for (int pin_idx = 0; pin_idx < num_pins[port_idx]; pin_idx++) {
                const t_pb_graph_pin* pin = &(pb_pins[port_idx][pin_idx]);
                pins.push_back(pin);
            }
        }
    }
    return pins;
}
