/*
 * Module: rl_str_proc.hh
 *
 * **** License ****
 * Version: VPL 1.0
 *
 * The contents of this file are subject to the Vyatta Public License
 * Version 1.0 ("License"); you may not use this file except in
 * compliance with the License. You may obtain a copy of the License at
 * http://www.vyatta.com/vpl
 *
 * Software distributed under the License is distributed on an "AS IS"
 * basis, WITHOUT WARRANTY OF ANY KIND, either express or implied. See
 * the License for the specific language governing rights and limitations
 * under the License.
 *
 * This code was originally developed by Vyatta, Inc.
 * Portions created by Vyatta are Copyright (C) 2008 Vyatta, Inc.
 * All Rights Reserved.
 *
 * Author: Michael Larson
 * Date: 2008
 * Description:
 *
 * **** End License ****
 *
 */
#ifndef __RL_STR_PROC_HH__
#define __RL_STR_PROC_HH__

#include <vector>
#include <string>

class StrProc
{
public:
  StrProc(const std::string &in, const std::string &token);

  std::string get(int i);

  std::string get(int start, int end);

  std::vector<std::string> get();

  int size() {return _str_coll.size();}

private:
  std::vector<std::string> _str_coll;
};

#endif //__RL_STR_PROC_HH__
