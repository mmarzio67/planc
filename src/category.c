#include <stdlib.h>
#include <string.h>

#include "category.h"
#include "util.h"

void category_list_init(CategoryList *list) {
    list->items = NULL;
    list->len = 0;
    list->cap = 0;
}

void category_list_free(CategoryList *list) {
    if (!list) return;
    for (size_t i = 0; i < list->len; ++i) {
        free(list->items[i].name);
    }
    free(list->items);
    list->items = NULL;
    list->len = 0;
    list->cap = 0;
}

void subcat_list_init(SubcategoryList *list) {
    list->items = NULL;
    list->len = 0;
    list->cap = 0;
}

void subcat_list_free(SubcategoryList *list) {
    if (!list) return;
    for (size_t i = 0; i < list->len; ++i) {
        free(list->items[i].name);
    }
    free(list->items);
    list->items = NULL;
    list->len = 0;
    list->cap = 0;
}

int category_next_id(const CategoryList *list) {
    int max = 0;
    for (size_t i = 0; i < list->len; ++i) {
        if (list->items[i].id > max) max = list->items[i].id;
    }
    return max + 1;
}

int subcat_next_id(const SubcategoryList *list) {
    int max = 0;
    for (size_t i = 0; i < list->len; ++i) {
        if (list->items[i].id > max) max = list->items[i].id;
    }
    return max + 1;
}

const Category *category_find_by_id(const CategoryList *list, int id) {
    for (size_t i = 0; i < list->len; ++i) {
        if (list->items[i].id == id) return &list->items[i];
    }
    return NULL;
}

int category_add(CategoryList *list, const char *name) {
    if (list->len == list->cap) {
        size_t new_cap = (list->cap == 0) ? 8 : list->cap * 2;
        Category *tmp = realloc(list->items, new_cap * sizeof(*tmp));
        if (!tmp) return -1;
        list->items = tmp;
        list->cap = new_cap;
    }
    Category cat;
    cat.id = category_next_id(list);
    cat.name = xstrdup(name);
    if (!cat.name) return -1;
    list->items[list->len++] = cat;
    return cat.id;
}

int subcat_add(SubcategoryList *list, int category_id, const char *name) {
    if (list->len == list->cap) {
        size_t new_cap = (list->cap == 0) ? 8 : list->cap * 2;
        Subcategory *tmp = realloc(list->items, new_cap * sizeof(*tmp));
        if (!tmp) return -1;
        list->items = tmp;
        list->cap = new_cap;
    }
    Subcategory sub;
    sub.id = subcat_next_id(list);
    sub.category_id = category_id;
    sub.name = xstrdup(name);
    if (!sub.name) return -1;
    list->items[list->len++] = sub;
    return sub.id;
}
