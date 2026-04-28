"""
Thin HTTP client over the planc FastAPI backend.
All functions raise PlanCError on non-2xx responses.
"""

import os
import requests

BASE_URL = os.environ.get("PLANC_API_URL", "http://localhost:8000")


class PlanCError(Exception):
    pass


def _raise(r: requests.Response) -> None:
    if not r.ok:
        try:
            detail = r.json().get("detail", r.text)
        except Exception:
            detail = r.text
        raise PlanCError(detail)


def _h(token: str) -> dict:
    return {"Authorization": f"Bearer {token}"}


# ─── auth ──────────────────────────────────────────────────────────────────

def login(username: str, password: str) -> str:
    """Authenticate and return the JWT access token."""
    # OAuth2PasswordRequestForm expects form data, not JSON
    r = requests.post(f"{BASE_URL}/auth/login",
                      data={"username": username, "password": password})
    _raise(r)
    return r.json()["access_token"]


def change_password(token: str, new_password: str) -> None:
    r = requests.put(f"{BASE_URL}/auth/users/me/password",
                     json={"new_password": new_password}, headers=_h(token))
    _raise(r)


# ─── items ─────────────────────────────────────────────────────────────────

def list_items(token: str,
               show_all: bool = False,
               status: str | None = None,
               priority: str | None = None) -> list[dict]:
    params: dict = {}
    if show_all:
        params["show_all"] = "true"
    if status:
        params["status"] = status
    if priority:
        params["priority"] = priority
    r = requests.get(f"{BASE_URL}/items", params=params, headers=_h(token))
    _raise(r)
    return r.json()


def add_item(token: str, text: str,
             cat_id: int = -1, subcat_id: int = -1) -> int:
    r = requests.post(f"{BASE_URL}/items",
                      json={"text": text, "cat_id": cat_id, "subcat_id": subcat_id},
                      headers=_h(token))
    _raise(r)
    return r.json()["id"]


def update_item(token: str, item_id: int, **fields) -> None:
    """Pass only the fields you want to change; omitted fields use API defaults."""
    r = requests.put(f"{BASE_URL}/items/{item_id}",
                     json=fields, headers=_h(token))
    _raise(r)


def delete_item(token: str, item_id: int) -> None:
    r = requests.delete(f"{BASE_URL}/items/{item_id}", headers=_h(token))
    _raise(r)


# ─── categories ────────────────────────────────────────────────────────────

def get_categories(token: str) -> dict:
    """Returns {"categories": [...], "subcategories": [...]}."""
    r = requests.get(f"{BASE_URL}/categories", headers=_h(token))
    _raise(r)
    return r.json()


def add_category(token: str, name: str) -> int:
    r = requests.post(f"{BASE_URL}/categories",
                      json={"name": name}, headers=_h(token))
    _raise(r)
    return r.json()["id"]


def add_subcategory(token: str, cat_id: int, name: str) -> int:
    r = requests.post(f"{BASE_URL}/subcategories",
                      json={"cat_id": cat_id, "name": name}, headers=_h(token))
    _raise(r)
    return r.json()["id"]


# ─── time tracking ─────────────────────────────────────────────────────────

def time_start(token: str, task_id: int) -> None:
    r = requests.post(f"{BASE_URL}/items/{task_id}/start", headers=_h(token))
    _raise(r)


def time_stop(token: str, task_id: int) -> None:
    r = requests.post(f"{BASE_URL}/items/{task_id}/stop", headers=_h(token))
    _raise(r)


def time_active(token: str) -> dict:
    r = requests.get(f"{BASE_URL}/timesheet/active", headers=_h(token))
    _raise(r)
    return r.json()


def time_totals(token: str) -> list[dict]:
    r = requests.get(f"{BASE_URL}/timesheet/totals", headers=_h(token))
    _raise(r)
    return r.json()
