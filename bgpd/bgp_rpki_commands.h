/* BGP RPKI Commands
 * Copyright (C) 2013 Michael Mester (m.mester@fu-berlin.de)
 *
 * This file is part of Quagga
 *
 * Quagga is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2, or (at your option) any
 * later version.
 *
 * Quagga is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Quagga; see the file COPYING.  If not, write to the Free
 * Software Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
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
