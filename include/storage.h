#ifndef STORAGE_H
#define STORAGE_H

#include "plan.h"

int storage_default_path(char *buf, size_t buf_size);
int storage_load(const char *path, PlanList *list);
int storage_save(const char *path, const PlanList *list);

#endif