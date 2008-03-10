/*
 * Module: rl_str_proc.cc
 *
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
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
