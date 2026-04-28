#ifndef PLANC_API_H
#define PLANC_API_H

#include <stddef.h>

/*
 * Public C API for libplanc.so — all string-returning functions allocate
 * on the heap; the caller must release with planc_free() when done.
 *
 * Sentinel values for integer fields:
 *   -1  = "not assigned" (no category / no subcategory)
 *   -2  = "keep current value" (planc_update only)
 */

/* Returns a JSON array of items, or NULL on error.
 * Pass show_all=1 to include done/archived.
 * Pass has_status=1 with a status_s string to filter by status.
 * Pass has_priority=1 with a priority_s string to filter by priority. */
char *planc_list(const char *db_path,
                 int show_all,
                 int has_status,   const char *status_s,
                 int has_priority, const char *priority_s);

/* Add a new item. Returns the new item id (>= 1) or -1 on error. */
int planc_add(const char *db_path, const char *text,
              int cat_id, int subcat_id,
              char *err_buf, size_t err_size);

/* Update an existing item.
 * Pass NULL for text/priority_s/status_s to keep the current value.
 * Pass -2 for cat_id/subcat_id to keep the current value.
 * Returns 0 on success, -1 on error. */
int planc_update(const char *db_path, int id,
                 const char *text,
                 int cat_id, int subcat_id,
                 const char *priority_s, const char *status_s,
                 char *err_buf, size_t err_size);

/* Delete an item. Returns 0 or -1. */
int planc_delete(const char *db_path, int id,
                 char *err_buf, size_t err_size);

/* Returns {"categories":[...], "subcategories":[...]} or NULL on error. */
char *planc_categories(const char *db_path);

/* Add a category. Returns new id or -1. */
int planc_cat_add(const char *db_path, const char *name,
                  char *err_buf, size_t err_size);

/* Add a subcategory under cat_id. Returns new id or -1. */
int planc_subcat_add(const char *db_path, int cat_id, const char *name,
                     char *err_buf, size_t err_size);

/* Release any string returned by the functions above. */
void planc_free(char *ptr);

/* ── time tracking ────────────────────────────────────────────────────────
 * Each session is one row in the timesheet table: task_id, started_at,
 * stopped_at (NULL while the timer is running).
 * Only one task may be running at a time. */

/* Start the timer for task_id. Returns 0 or -1.
 * Fails if any task is already running. */
int planc_time_start(const char *db_path, int task_id,
                     char *err_buf, size_t err_size);

/* Stop the running timer for task_id. Returns 0 or -1. */
int planc_time_stop(const char *db_path, int task_id,
                    char *err_buf, size_t err_size);

/* Returns the active session as JSON: {"task_id":N,"started_at":"..."}
 * or {} if nothing is running. Returns NULL on error. */
char *planc_time_active(const char *db_path);

/* Returns per-task time totals as a JSON array:
 * [{"task_id":N,"total_seconds":N,"is_running":0|1}, ...]
 * Returns NULL on error. */
char *planc_time_totals(const char *db_path);

#endif /* PLANC_API_H */
