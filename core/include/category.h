#ifndef CATEGORY_H
#define CATEGORY_H

#include <stddef.h>

typedef struct {
    int id;
    char *name;
} Category;

typedef struct {
    int id;
    int category_id;
    char *name;
} Subcategory;

typedef struct {
    Category *items;
    size_t len;
    size_t cap;
} CategoryList;

typedef struct {
    Subcategory *items;
    size_t len;
    size_t cap;
} SubcategoryList;

/* lifecycle */
void category_list_init(CategoryList *list);
void category_list_free(CategoryList *list);
void subcat_list_init(SubcategoryList *list);
void subcat_list_free(SubcategoryList *list);

/* lookup */
int category_next_id(const CategoryList *list);
int subcat_next_id(const SubcategoryList *list);
const Category *category_find_by_id(const CategoryList *list, int id);

/* operations */
int category_add(CategoryList *list, const char *name);
int subcat_add(SubcategoryList *list, int category_id, const char *name);

#endif
