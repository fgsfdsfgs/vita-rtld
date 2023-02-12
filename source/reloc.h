#pragma once

#include "common.h"

int vrtld_relocate(dso_t *mod, const int ignore_undef, const int imports_only);
