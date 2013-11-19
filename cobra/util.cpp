/*
 * Copyright 2013, Mirek Klimos <myreggg@gmail.com>
 */
#include <sstream>
#include <string>
#include <vector>
#include <algorithm>
#include <functional>

#include "util.h"

std::string to_string(int n) {
  std::ostringstream convert;
  convert << n;
  return convert.str();
}
