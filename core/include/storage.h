#ifndef STORAGE_H
#define STORAGE_H

#include <sqlite3.h>
#include "plan.h"
#include "category.h"

/* Open the database and ensure the full schema exists.
 * Called by storage.c internally and by timesheet.c. */
int storage_db_open(const char *path, sqlite3 **db);

int storage_default_path(char *buf, size_t buf_size);
int storage_load(const char *path, PlanList *list);
int storage_save(const char *path, const PlanList *list);

int storage_cat_path(char *buf, size_t buf_size);
int storage_cat_load(const char *path, CategoryList *cats, SubcategoryList *subcats);
int storage_cat_save(const char *path, const CategoryList *cats, const SubcategoryList *subcats);

#endif