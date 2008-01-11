/*
 * Module: rl_str_proc.cc
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
#include "rl_str_proc.hh"

using namespace std;

/**
 *
 **/
StrProc::StrProc(const string &in_str, const string &token)
{
  string tmp = in_str;
  
  //convert tabs to spaces
  uint32_t pos = 0;
  string tabtospace = "    ";
  string::iterator iter = tmp.begin();
  while ((pos = tmp.find("\t", pos)) != string::npos) {
    tmp.replace(pos, 1, tabtospace);
    pos += tabtospace.length();
  }
  
  //remove the cr
  pos = tmp.find("\n");
  if (pos != string::npos) {
    tmp.replace(pos, 1, "");
  }

  //now handle the case of the multiple length token
  //note that we are using the '~' as a token internally
  uint32_t start = 0, end;
  while ((start = tmp.find(token, start)) != string::npos) {
    tmp.replace(start, token.length(), "~");
  }


  while ((start = tmp.find_first_not_of("~")) != string::npos) {
    tmp = tmp.substr(start, tmp.length() - start);
    end = tmp.find_first_of("~");
    _str_coll.push_back(tmp.substr(0, end));
    tmp = tmp.substr(end+1, tmp.length() - end-1);
    if (end == string::npos) {
      break;
    }
  }
}

/**
 *
 **/
string
StrProc::get(int i)
{
  if (uint32_t(i) >= _str_coll.size()) {
    return string("");
  }
  return _str_coll[i];
}

/**
 *
 **/
string
StrProc::get(int start, int end)
{
  if (uint32_t(start) >= _str_coll.size()) {
    return string("");
  }

  string tmp;
  for (int i = start; (i < end) && (uint32_t(i) < _str_coll.size()); ++i) {
    tmp += _str_coll[i] + " ";
  }
  return tmp.substr(0,tmp.length()-1);
}

/**
 *
 **/
vector<string>
StrProc::get()
{
  return _str_coll;
}
