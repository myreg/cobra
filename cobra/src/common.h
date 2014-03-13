/*
 * Copyright 2014, Mirek Klimos <myreggg@gmail.com>
 */
#include <cassert>
#include <string>
#include <vector>
#include <algorithm>
#include <functional>
#include <iostream>

#ifndef COBRA_COMMON_H_
#define COBRA_COMMON_H_

typedef unsigned int uint;
typedef unsigned char CharId;
typedef short int VarId; // must se signed, -1 denotes negation of var 1
typedef unsigned char MapId;

template<typename T>
struct identity { typedef T type; };

template<class T>
void for_all_combinations(int k, std::vector<T>& list, std::function<void(std::vector<T>)> action, int offset = 0);

template<class T>
void for_all_combinations(int k, std::vector<T>& list, std::function<void(std::vector<T>)> action, int offset) {
  assert(k >= 0);
  static std::vector<T> combination;
  if (k == 0) {
    action(combination);
    return;
  }
  for (uint i = offset; i <= list.size() - k; ++i) {
    combination.push_back(list[i]);
    for_all_combinations(k-1, list, action, i+1);
    combination.pop_back();
  }
}

template<class T, class R>
void transform(std::vector<T>& from, std::vector<R>& to, std::function<R(T)>& fun) {
  to.resize(from.size());
  std::transform(from.begin(), from.end(), to.begin(), fun);
}

std::vector<std::string> split(std::string s);
bool readIntOrString(uint& i, std::string& str);

#endif  // COBRA_COMMON_H_
