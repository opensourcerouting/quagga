/*
 * rpki_commands.h
 *
 * Author: Michael Mester (m.mester@fu-berlin.de)
 */

#ifndef RPKI_COMMANDS_H_
#define RPKI_COMMANDS_H_

#include "rtrlib/rtrlib.h"

void install_cli_commands();
rtr_mgr_group* get_rtr_mgr_groups();
unsigned int get_number_of_cache_groups();
void free_rtr_mgr_groups(rtr_mgr_group* group, int length);
void delete_cache_group_list();

#endif /* RPKI_COMMANDS_H_ */
