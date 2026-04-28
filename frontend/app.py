"""
planc Streamlit frontend.

Start with:
    streamlit run frontend/app.py   (from the project root)
or:
    cd frontend && streamlit run app.py

Requires the FastAPI backend to be running at PLANC_API_URL (default: localhost:8000).
"""

import streamlit as st
import api_client as api
from api_client import PlanCError

st.set_page_config(page_title="planc", layout="wide")

PRIORITIES = ["today", "urgent", "high", "normal", "low"]
STATUSES   = ["open", "waiting", "done", "archived"]


# ─── helpers ─────────────────────────────────────────────────────────────────

def _token() -> str | None:
    return st.session_state.get("token")


def _build_subcat_options(cats_data: dict) -> tuple[list[str], dict[str, tuple[int, int]]]:
    """
    Returns:
        labels    — list of display strings, first entry is "(none)"
        id_map    — label -> (subcat_id, cat_id)
    """
    labels = ["(none)"]
    id_map: dict[str, tuple[int, int]] = {}

    cat_names = {c["id"]: c["name"] for c in cats_data.get("categories", [])}
    for s in cats_data.get("subcategories", []):
        parent = cat_names.get(s["category_id"], "?")
        label  = f"[{s['id']}] {parent} / {s['name']}"
        labels.append(label)
        id_map[label] = (s["id"], s["category_id"])

    return labels, id_map


def _item_header(item: dict, cats_data: dict) -> str:
    cat_names = {c["id"]: c["name"] for c in cats_data.get("categories", [])}
    sub_names = {s["id"]: s["name"] for s in cats_data.get("subcategories", [])}

    cat_part = ""
    if item["category_id"] != -1:
        cat_part = cat_names.get(item["category_id"], "?")
        if item["subcat_id"] != -1:
            cat_part += f" / {sub_names.get(item['subcat_id'], '?')}"
        cat_part = f"  _{cat_part}_"

    return (f"[{item['id']}] **{item['text']}**"
            f"  `{item['status']}` `{item['priority']}`{cat_part}")


# ─── login page ──────────────────────────────────────────────────────────────

def show_login() -> None:
    st.title("planc")
    with st.form("login"):
        username  = st.text_input("Username")
        password  = st.text_input("Password", type="password")
        submitted = st.form_submit_button("Login")
    if submitted:
        try:
            token = api.login(username, password)
            st.session_state["token"] = token
            st.rerun()
        except PlanCError as e:
            st.error(str(e))


# ─── main page ───────────────────────────────────────────────────────────────

def show_main() -> None:
    token = _token()

    # sidebar
    with st.sidebar:
        st.header("Filters")
        show_all     = st.checkbox("Show all (including done / archived)")
        status_sel   = st.selectbox("Status",   ["(all)"] + STATUSES)
        priority_sel = st.selectbox("Priority", ["(all)"] + PRIORITIES)
        st.divider()
        if st.button("Logout"):
            del st.session_state["token"]
            st.rerun()
        st.divider()
        with st.expander("Change password"):
            with st.form("chpw"):
                new_pw = st.text_input("New password", type="password")
                if st.form_submit_button("Change"):
                    try:
                        api.change_password(token, new_pw)
                        st.success("Password updated.")
                    except PlanCError as e:
                        st.error(str(e))

    status_filter   = None if status_sel   == "(all)" else status_sel
    priority_filter = None if priority_sel == "(all)" else priority_sel

    # version counters — incrementing a counter changes the form key, which
    # forces Streamlit to render a brand-new widget with empty fields
    for _k in ("_ver_add_item", "_ver_add_cat", "_ver_add_subcat"):
        if _k not in st.session_state:
            st.session_state[_k] = 0

    st.title("planc")

    # load all data upfront — one API call each, then work from the local dicts
    try:
        items     = api.list_items(token, show_all=show_all,
                                   status=status_filter, priority=priority_filter)
        cats_data = api.get_categories(token)
    except PlanCError as e:
        st.error(f"Could not load data: {e}")
        return

    subcat_labels, subcat_id_map = _build_subcat_options(cats_data)

    # ── add item ─────────────────────────────────────────────────────────────
    with st.expander("Add item"):
        with st.form(f"add_item_{st.session_state['_ver_add_item']}"):
            text       = st.text_input("Text")
            subcat_sel = st.selectbox("Subcategory", subcat_labels)
            if st.form_submit_button("Add"):
                if not text.strip():
                    st.warning("Text cannot be empty.")
                else:
                    subcat_id, cat_id = ((-1, -1) if subcat_sel == "(none)"
                                         else subcat_id_map[subcat_sel])
                    try:
                        new_id = api.add_item(token, text.strip(), cat_id, subcat_id)
                        st.session_state["_ver_add_item"] += 1
                        st.rerun()
                    except PlanCError as e:
                        st.error(str(e))

    st.divider()

    # ── item list ─────────────────────────────────────────────────────────────
    if not items:
        st.info("No items match the current filters.")
    else:
        for item in items:
            with st.expander(_item_header(item, cats_data), expanded=False):
                st.caption(f"created: {item['created_at']}")

                col_done, col_del, _ = st.columns([1, 1, 6])
                with col_done:
                    if item["status"] != "done":
                        if st.button("Mark done", key=f"done_{item['id']}"):
                            try:
                                api.update_item(token, item["id"], status="done")
                                st.rerun()
                            except PlanCError as e:
                                st.error(str(e))
                with col_del:
                    if st.button("Delete", key=f"del_{item['id']}"):
                        try:
                            api.delete_item(token, item["id"])
                            st.rerun()
                        except PlanCError as e:
                            st.error(str(e))

                # inline edit form — unique key per item
                with st.form(f"edit_{item['id']}"):
                    new_text = st.text_input("Text", value=item["text"])
                    new_pri  = st.selectbox("Priority", PRIORITIES,
                                            index=PRIORITIES.index(item["priority"]),
                                            key=f"pri_{item['id']}")
                    new_stat = st.selectbox("Status", STATUSES,
                                            index=STATUSES.index(item["status"]),
                                            key=f"stat_{item['id']}")

                    # find the current subcat label to pre-select it
                    current_label = next(
                        (lbl for lbl, (sid, _) in subcat_id_map.items()
                         if sid == item["subcat_id"]),
                        "(none)",
                    )
                    new_subcat_sel = st.selectbox(
                        "Subcategory", subcat_labels,
                        index=subcat_labels.index(current_label),
                        key=f"subcat_{item['id']}",
                    )

                    if st.form_submit_button("Update"):
                        new_subcat_id, new_cat_id = (
                            (-1, -1) if new_subcat_sel == "(none)"
                            else subcat_id_map[new_subcat_sel]
                        )
                        try:
                            api.update_item(
                                token, item["id"],
                                text=new_text.strip() or None,
                                cat_id=new_cat_id,
                                subcat_id=new_subcat_id,
                                priority=new_pri,
                                status=new_stat,
                            )
                            st.rerun()
                        except PlanCError as e:
                            st.error(str(e))

    # ── category management ───────────────────────────────────────────────────
    st.divider()
    with st.expander("Manage categories"):
        col_cat, col_sub = st.columns(2)

        with col_cat:
            st.subheader("Categories")
            for c in cats_data.get("categories", []):
                st.write(f"[{c['id']}] {c['name']}")
            with st.form(f"add_cat_{st.session_state['_ver_add_cat']}"):
                cat_name = st.text_input("Name")
                if st.form_submit_button("Add category"):
                    try:
                        new_id = api.add_category(token, cat_name.strip())
                        st.session_state["_ver_add_cat"] += 1
                        st.rerun()
                    except PlanCError as e:
                        st.error(str(e))

        with col_sub:
            st.subheader("Subcategories")
            cat_names = {c["id"]: c["name"] for c in cats_data.get("categories", [])}
            for s in cats_data.get("subcategories", []):
                parent = cat_names.get(s["category_id"], "?")
                st.write(f"[{s['id']}] {parent} / {s['name']}")

            cats = cats_data.get("categories", [])
            if cats:
                with st.form(f"add_subcat_{st.session_state['_ver_add_subcat']}"):
                    cat_choices = {f"[{c['id']}] {c['name']}": c["id"] for c in cats}
                    parent_sel  = st.selectbox("Parent category", list(cat_choices))
                    sub_name    = st.text_input("Name")
                    if st.form_submit_button("Add subcategory"):
                        try:
                            new_id = api.add_subcategory(
                                token, cat_choices[parent_sel], sub_name.strip())
                            st.session_state["_ver_add_subcat"] += 1
                            st.rerun()
                        except PlanCError as e:
                            st.error(str(e))
            else:
                st.info("Add a category first.")


# ─── entry point ─────────────────────────────────────────────────────────────

if _token():
    show_main()
else:
    show_login()
