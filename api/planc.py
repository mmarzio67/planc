"""
ctypes wrapper around libplanc.so.

All string-returning C functions allocate on the heap and require planc_free().
We use c_void_p as restype (instead of c_char_p) so Python does NOT make a
silent copy — we retain the raw pointer and call planc_free() ourselves.
"""

import ctypes
import json
import os
from pathlib import Path

_lib_path = Path(__file__).parent.parent / "libplanc.so"
_lib = ctypes.CDLL(str(_lib_path))

# --- function signatures ---

# planc_list returns a heap JSON string; c_void_p keeps the raw pointer
_lib.planc_list.restype  = ctypes.c_void_p
_lib.planc_list.argtypes = [
    ctypes.c_char_p,  # db_path
    ctypes.c_int,     # show_all
    ctypes.c_int,     # has_status
    ctypes.c_char_p,  # status_s  (may be NULL)
    ctypes.c_int,     # has_priority
    ctypes.c_char_p,  # priority_s (may be NULL)
]

_lib.planc_add.restype  = ctypes.c_int
_lib.planc_add.argtypes = [
    ctypes.c_char_p,  # db_path
    ctypes.c_char_p,  # text
    ctypes.c_int,     # cat_id
    ctypes.c_int,     # subcat_id
    ctypes.c_char_p,  # err_buf
    ctypes.c_size_t,  # err_size
]

_lib.planc_update.restype  = ctypes.c_int
_lib.planc_update.argtypes = [
    ctypes.c_char_p,  # db_path
    ctypes.c_int,     # id
    ctypes.c_char_p,  # text        (may be NULL = keep current)
    ctypes.c_int,     # cat_id      (-2 = keep current, -1 = unassign)
    ctypes.c_int,     # subcat_id   (-2 = keep current, -1 = unassign)
    ctypes.c_char_p,  # priority_s  (may be NULL = keep current)
    ctypes.c_char_p,  # status_s    (may be NULL = keep current)
    ctypes.c_char_p,  # err_buf
    ctypes.c_size_t,  # err_size
]

_lib.planc_delete.restype  = ctypes.c_int
_lib.planc_delete.argtypes = [
    ctypes.c_char_p,  # db_path
    ctypes.c_int,     # id
    ctypes.c_char_p,  # err_buf
    ctypes.c_size_t,  # err_size
]

_lib.planc_categories.restype  = ctypes.c_void_p
_lib.planc_categories.argtypes = [ctypes.c_char_p]

_lib.planc_cat_add.restype  = ctypes.c_int
_lib.planc_cat_add.argtypes = [
    ctypes.c_char_p,
    ctypes.c_char_p,
    ctypes.c_char_p,
    ctypes.c_size_t,
]

_lib.planc_subcat_add.restype  = ctypes.c_int
_lib.planc_subcat_add.argtypes = [
    ctypes.c_char_p,
    ctypes.c_int,
    ctypes.c_char_p,
    ctypes.c_char_p,
    ctypes.c_size_t,
]

_lib.planc_free.restype  = None
_lib.planc_free.argtypes = [ctypes.c_void_p]

# --- helpers ---

def _db_path() -> bytes:
    """Resolve the planc.db path, honouring PLANC_DB_PATH and XDG_DATA_HOME."""
    explicit = os.environ.get("PLANC_DB_PATH")
    if explicit:
        return explicit.encode()
    xdg = os.environ.get("XDG_DATA_HOME")
    if xdg:
        return f"{xdg}/plan/planc.db".encode()
    home = os.environ["HOME"]
    return f"{home}/.local/share/plan/planc.db".encode()


def _read_and_free(ptr: int) -> str:
    """Copy a C heap string to Python bytes, then free the C allocation."""
    data = ctypes.string_at(ptr).decode()
    _lib.planc_free(ptr)
    return data

# --- public API ---

def list_items(show_all: bool = False,
               status: str | None = None,
               priority: str | None = None) -> list[dict]:
    ptr = _lib.planc_list(
        _db_path(),
        int(show_all),
        int(status is not None),   status.encode()   if status   else None,
        int(priority is not None), priority.encode() if priority else None,
    )
    if ptr is None:
        raise RuntimeError("planc_list returned NULL")
    return json.loads(_read_and_free(ptr))


def add_item(text: str, cat_id: int = -1, subcat_id: int = -1) -> int:
    err = ctypes.create_string_buffer(256)
    result = _lib.planc_add(_db_path(), text.encode(), cat_id, subcat_id, err, 256)
    if result < 0:
        raise RuntimeError(err.value.decode())
    return result


def update_item(item_id: int,
                text: str | None = None,
                cat_id: int = -2,
                subcat_id: int = -2,
                priority: str | None = None,
                status: str | None = None) -> None:
    err = ctypes.create_string_buffer(256)
    result = _lib.planc_update(
        _db_path(), item_id,
        text.encode()     if text     else None,
        cat_id,
        subcat_id,
        priority.encode() if priority else None,
        status.encode()   if status   else None,
        err, 256,
    )
    if result != 0:
        raise RuntimeError(err.value.decode())


def delete_item(item_id: int) -> None:
    err = ctypes.create_string_buffer(256)
    result = _lib.planc_delete(_db_path(), item_id, err, 256)
    if result != 0:
        raise RuntimeError(err.value.decode())


def get_categories() -> dict:
    ptr = _lib.planc_categories(_db_path())
    if ptr is None:
        raise RuntimeError("planc_categories returned NULL")
    return json.loads(_read_and_free(ptr))


def add_category(name: str) -> int:
    err = ctypes.create_string_buffer(256)
    result = _lib.planc_cat_add(_db_path(), name.encode(), err, 256)
    if result < 0:
        raise RuntimeError(err.value.decode())
    return result


def add_subcategory(cat_id: int, name: str) -> int:
    err = ctypes.create_string_buffer(256)
    result = _lib.planc_subcat_add(_db_path(), cat_id, name.encode(), err, 256)
    if result < 0:
        raise RuntimeError(err.value.decode())
    return result


# ── time tracking ─────────────────────────────────────────────────────────────

_lib.planc_time_start.restype  = ctypes.c_int
_lib.planc_time_start.argtypes = [ctypes.c_char_p, ctypes.c_int,
                                   ctypes.c_char_p, ctypes.c_size_t]

_lib.planc_time_stop.restype  = ctypes.c_int
_lib.planc_time_stop.argtypes = [ctypes.c_char_p, ctypes.c_int,
                                  ctypes.c_char_p, ctypes.c_size_t]

_lib.planc_time_active.restype  = ctypes.c_void_p
_lib.planc_time_active.argtypes = [ctypes.c_char_p]

_lib.planc_time_totals.restype  = ctypes.c_void_p
_lib.planc_time_totals.argtypes = [ctypes.c_char_p]


def time_start(task_id: int) -> None:
    err = ctypes.create_string_buffer(256)
    if _lib.planc_time_start(_db_path(), task_id, err, 256) != 0:
        raise RuntimeError(err.value.decode())


def time_stop(task_id: int) -> None:
    err = ctypes.create_string_buffer(256)
    if _lib.planc_time_stop(_db_path(), task_id, err, 256) != 0:
        raise RuntimeError(err.value.decode())


def time_active() -> dict:
    ptr = _lib.planc_time_active(_db_path())
    if ptr is None:
        raise RuntimeError("planc_time_active returned NULL")
    return json.loads(_read_and_free(ptr))


def time_totals() -> list[dict]:
    ptr = _lib.planc_time_totals(_db_path())
    if ptr is None:
        raise RuntimeError("planc_time_totals returned NULL")
    return json.loads(_read_and_free(ptr))
