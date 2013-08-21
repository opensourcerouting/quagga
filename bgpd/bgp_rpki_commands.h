/*
 * rpki_commands.h
 *
 *  Created on: 19.03.2013
 *      Author: Michael Mester
 */

#ifndef RPKI_COMMANDS_H_
#define RPKI_COMMANDS_H_
#include "rtrlib/rtrlib.h"
void install_cli_commands();
rtr_mgr_group* get_rtr_mgr_groups();
void free_rtr_mgr_groups(rtr_mgr_group* group, int length);
int get_number_of_cache_groups();
void delete_cache_group_list();
extern int rpki_config_write (struct vty *);

#endif /* RPKI_COMMANDS_H_ */
