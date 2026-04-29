#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sqlite3.h>

#include "planc_api.h"
#include "storage.h"
#include "strbuf.h"
#include "util.h"

int planc_time_start(const char *db_path, int task_id,
                     const char *notes,
                     char *err_buf, size_t err_size) {
    sqlite3 *db;
    if (storage_db_open(db_path, &db) != 0) {
        snprintf(err_buf, err_size, "could not open database");
        return -1;
    }

    /* reject if any task is already running */
    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(db,
            "SELECT task_id FROM timesheet WHERE stopped_at IS NULL LIMIT 1",
            -1, &stmt, NULL) != SQLITE_OK) {
        snprintf(err_buf, err_size, "db error");
        sqlite3_close(db); return -1;
    }
    int already = (sqlite3_step(stmt) == SQLITE_ROW);
    int other   = already ? sqlite3_column_int(stmt, 0) : -1;
    sqlite3_finalize(stmt);

    if (already) {
        snprintf(err_buf, err_size, "task %d is already running", other);
        sqlite3_close(db); return -1;
    }

    char ts[21];
    if (current_timestamp_utc(ts) != 0) {
        snprintf(err_buf, err_size, "could not get timestamp");
        sqlite3_close(db); return -1;
    }

    if (sqlite3_prepare_v2(db,
            "INSERT INTO timesheet (task_id, started_at, notes) VALUES (?, ?, ?)",
            -1, &stmt, NULL) != SQLITE_OK) {
        snprintf(err_buf, err_size, "db error");
        sqlite3_close(db); return -1;
    }
    sqlite3_bind_int (stmt, 1, task_id);
    sqlite3_bind_text(stmt, 2, ts, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, notes ? notes : "", -1, SQLITE_STATIC);

    int rc = (sqlite3_step(stmt) == SQLITE_DONE) ? 0 : -1;
    if (rc != 0) snprintf(err_buf, err_size, "could not start timer");
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return rc;
}

int planc_time_stop(const char *db_path, int task_id,
                    const char *notes,
                    char *err_buf, size_t err_size) {
    sqlite3 *db;
    if (storage_db_open(db_path, &db) != 0) {
        snprintf(err_buf, err_size, "could not open database");
        return -1;
    }

    char ts[21];
    if (current_timestamp_utc(ts) != 0) {
        snprintf(err_buf, err_size, "could not get timestamp");
        sqlite3_close(db); return -1;
    }

    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(db,
            "UPDATE timesheet SET stopped_at = ?, notes = ? "
            "WHERE task_id = ? AND stopped_at IS NULL",
            -1, &stmt, NULL) != SQLITE_OK) {
        snprintf(err_buf, err_size, "db error");
        sqlite3_close(db); return -1;
    }
    sqlite3_bind_text(stmt, 1, ts, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, notes ? notes : "", -1, SQLITE_STATIC);
    sqlite3_bind_int (stmt, 3, task_id);

    int rc = -1;
    if (sqlite3_step(stmt) == SQLITE_DONE) {
        if (sqlite3_changes(db) > 0) rc = 0;
        else snprintf(err_buf, err_size, "task %d is not running", task_id);
    } else {
        snprintf(err_buf, err_size, "could not stop timer");
    }
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return rc;
}

char *planc_time_active(const char *db_path) {
    sqlite3 *db;
    if (storage_db_open(db_path, &db) != 0) return NULL;

    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(db,
            "SELECT task_id, started_at, notes FROM timesheet "
            "WHERE stopped_at IS NULL LIMIT 1",
            -1, &stmt, NULL) != SQLITE_OK) {
        sqlite3_close(db); return NULL;
    }

    char *result = NULL;

    if (sqlite3_step(stmt) == SQLITE_ROW) {
        int         task_id    = sqlite3_column_int (stmt, 0);
        const char *started_at = (const char *)sqlite3_column_text(stmt, 1);
        const char *notes      = (const char *)sqlite3_column_text(stmt, 2);
        if (started_at) {
            Strbuf sb;
            if (sb_init(&sb) == 0) {
                if (sb_append    (&sb, "{\"task_id\":") == 0 &&
                    sb_append_int(&sb, task_id)         == 0 &&
                    sb_append    (&sb, ",\"started_at\":") == 0 &&
                    sb_append_json_str(&sb, started_at) == 0 &&
                    sb_append    (&sb, ",\"notes\":") == 0 &&
                    sb_append_json_str(&sb, notes ? notes : "") == 0 &&
                    sb_append    (&sb, "}") == 0) {
                    result = sb.buf;
                } else {
                    free(sb.buf);
                }
            }
        }
    } else {
        result = xstrdup("{}");
    }

    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return result;
}

char *planc_time_totals(const char *db_path) {
    sqlite3 *db;
    if (storage_db_open(db_path, &db) != 0) return NULL;

    /* sum durations per task; for running sessions use current time */
    const char *sql =
        "SELECT task_id,"
        "  SUM(CASE WHEN stopped_at IS NOT NULL"
        "    THEN CAST(strftime('%s', stopped_at) AS INTEGER)"
        "       - CAST(strftime('%s', started_at) AS INTEGER)"
        "    ELSE CAST(strftime('%s', 'now')      AS INTEGER)"
        "       - CAST(strftime('%s', started_at) AS INTEGER)"
        "  END) AS total_seconds,"
        "  MAX(CASE WHEN stopped_at IS NULL THEN 1 ELSE 0 END) AS is_running"
        " FROM timesheet GROUP BY task_id";

    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        sqlite3_close(db); return NULL;
    }

    Strbuf sb;
    if (sb_init(&sb) != 0) { sqlite3_finalize(stmt); sqlite3_close(db); return NULL; }
    if (sb_append(&sb, "[") != 0) goto fail;

    int first = 1;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        int task_id       = sqlite3_column_int(stmt, 0);
        int total_seconds = sqlite3_column_int(stmt, 1);
        int is_running    = sqlite3_column_int(stmt, 2);

        if (!first && sb_append(&sb, ",") != 0) goto fail;
        if (sb_append    (&sb, "{\"task_id\":")       != 0) goto fail;
        if (sb_append_int(&sb, task_id)               != 0) goto fail;
        if (sb_append    (&sb, ",\"total_seconds\":") != 0) goto fail;
        if (sb_append_int(&sb, total_seconds)         != 0) goto fail;
        if (sb_append    (&sb, ",\"is_running\":")    != 0) goto fail;
        if (sb_append    (&sb, is_running ? "1" : "0") != 0) goto fail;
        if (sb_append    (&sb, "}")                   != 0) goto fail;
        first = 0;
    }

    if (sb_append(&sb, "]") != 0) goto fail;
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return sb.buf;

fail:
    free(sb.buf);
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return NULL;
}

char *planc_time_report(const char *db_path,
                        int has_date_from, const char *date_from,
                        int has_date_to,   const char *date_to) {
    sqlite3 *db;
    if (storage_db_open(db_path, &db) != 0) return NULL;

    /* one row per session; includes full timestamps so the frontend can edit them */
    const char *sql =
        "SELECT t.id,"
        "       t.task_id,"
        "       COALESCE(i.text, '[deleted]') AS task_text,"
        "       COALESCE(c.name, '')  AS category_name,"
        "       COALESCE(s.name, '')  AS subcat_name,"
        "       t.started_at,"
        "       t.stopped_at,"
        "       t.notes,"
        "       CASE WHEN t.stopped_at IS NOT NULL"
        "           THEN CAST(strftime('%s', t.stopped_at) AS INTEGER)"
        "              - CAST(strftime('%s', t.started_at) AS INTEGER)"
        "           ELSE CAST(strftime('%s', 'now')        AS INTEGER)"
        "              - CAST(strftime('%s', t.started_at) AS INTEGER)"
        "           END AS total_seconds"
        " FROM timesheet t"
        " LEFT JOIN items        i ON t.task_id     = i.id"
        " LEFT JOIN categories   c ON i.category_id = c.id"
        " LEFT JOIN subcategories s ON i.subcat_id  = s.id"
        " WHERE (? = 0 OR DATE(t.started_at) >= ?)"
        "   AND (? = 0 OR DATE(t.started_at) <= ?)"
        " ORDER BY t.started_at DESC";

    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        sqlite3_close(db); return NULL;
    }

    sqlite3_bind_int (stmt, 1, has_date_from);
    sqlite3_bind_text(stmt, 2, has_date_from ? date_from : "", -1, SQLITE_STATIC);
    sqlite3_bind_int (stmt, 3, has_date_to);
    sqlite3_bind_text(stmt, 4, has_date_to   ? date_to   : "", -1, SQLITE_STATIC);

    Strbuf sb;
    if (sb_init(&sb) != 0) { sqlite3_finalize(stmt); sqlite3_close(db); return NULL; }
    if (sb_append(&sb, "[") != 0) goto fail_report;

    int first = 1;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        int         session_id    = sqlite3_column_int (stmt, 0);
        int         task_id       = sqlite3_column_int (stmt, 1);
        const char *task_text     = (const char *)sqlite3_column_text(stmt, 2);
        const char *category_name = (const char *)sqlite3_column_text(stmt, 3);
        const char *subcat_name   = (const char *)sqlite3_column_text(stmt, 4);
        const char *started_at    = (const char *)sqlite3_column_text(stmt, 5);
        const char *stopped_at    = (const char *)sqlite3_column_text(stmt, 6); /* may be NULL */
        const char *notes         = (const char *)sqlite3_column_text(stmt, 7);
        int         total_seconds = sqlite3_column_int (stmt, 8);

        if (!task_text || !started_at) goto fail_report;
        if (!first && sb_append(&sb, ",") != 0) goto fail_report;
        if (sb_append    (&sb, "{\"id\":")            != 0) goto fail_report;
        if (sb_append_int(&sb, session_id)            != 0) goto fail_report;
        if (sb_append    (&sb, ",\"task_id\":")       != 0) goto fail_report;
        if (sb_append_int(&sb, task_id)               != 0) goto fail_report;
        if (sb_append    (&sb, ",\"task_text\":")     != 0) goto fail_report;
        if (sb_append_json_str(&sb, task_text)        != 0) goto fail_report;
        if (sb_append    (&sb, ",\"category\":")      != 0) goto fail_report;
        if (sb_append_json_str(&sb, category_name ? category_name : "") != 0) goto fail_report;
        if (sb_append    (&sb, ",\"subcategory\":")   != 0) goto fail_report;
        if (sb_append_json_str(&sb, subcat_name   ? subcat_name   : "") != 0) goto fail_report;
        if (sb_append    (&sb, ",\"started_at\":")    != 0) goto fail_report;
        if (sb_append_json_str(&sb, started_at)       != 0) goto fail_report;
        if (sb_append    (&sb, ",\"stopped_at\":")    != 0) goto fail_report;
        if (stopped_at) {
            if (sb_append_json_str(&sb, stopped_at)   != 0) goto fail_report;
        } else {
            if (sb_append(&sb, "null")                != 0) goto fail_report;
        }
        if (sb_append    (&sb, ",\"notes\":")         != 0) goto fail_report;
        if (sb_append_json_str(&sb, notes ? notes : "") != 0) goto fail_report;
        if (sb_append    (&sb, ",\"total_seconds\":") != 0) goto fail_report;
        if (sb_append_int(&sb, total_seconds)         != 0) goto fail_report;
        if (sb_append    (&sb, "}")                   != 0) goto fail_report;
        first = 0;
    }

    if (sb_append(&sb, "]") != 0) goto fail_report;
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return sb.buf;

fail_report:
    free(sb.buf);
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return NULL;
}

int planc_time_update(const char *db_path, int session_id,
                      const char *started_at,
                      const char *stopped_at,
                      const char *notes,
                      char *err_buf, size_t err_size) {
    sqlite3 *db;
    if (storage_db_open(db_path, &db) != 0) {
        snprintf(err_buf, err_size, "could not open database");
        return -1;
    }

    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(db,
            "UPDATE timesheet SET started_at = ?, stopped_at = ?, notes = ? "
            "WHERE id = ?",
            -1, &stmt, NULL) != SQLITE_OK) {
        snprintf(err_buf, err_size, "db error");
        sqlite3_close(db); return -1;
    }

    sqlite3_bind_text(stmt, 1, started_at, -1, SQLITE_STATIC);
    if (stopped_at && *stopped_at != '\0')
        sqlite3_bind_text(stmt, 2, stopped_at, -1, SQLITE_STATIC);
    else
        sqlite3_bind_null(stmt, 2);
    sqlite3_bind_text(stmt, 3, notes ? notes : "", -1, SQLITE_STATIC);
    sqlite3_bind_int (stmt, 4, session_id);

    int rc = -1;
    if (sqlite3_step(stmt) == SQLITE_DONE) {
        if (sqlite3_changes(db) > 0) rc = 0;
        else snprintf(err_buf, err_size, "session %d not found", session_id);
    } else {
        snprintf(err_buf, err_size, "could not update session");
    }
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return rc;
}
