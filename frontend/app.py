"""
planc Streamlit frontend.

Start with:
    streamlit run frontend/app.py   (from the project root)
or:
    cd frontend && streamlit run app.py

Requires the FastAPI backend to be running at PLANC_API_URL (default: localhost:8000).
"""

import io
import csv as _csv
from datetime import datetime, time as _time

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


def _fmt_duration(seconds: int) -> str:
    if seconds < 60:
        return "< 1m"
    h, m = divmod(seconds // 60, 60)
    return f"{h}h {m:02d}m" if h else f"{m}m"


def _item_header(item: dict, cats_data: dict, totals_map: dict) -> str:
    cat_names = {c["id"]: c["name"] for c in cats_data.get("categories", [])}
    sub_names = {s["id"]: s["name"] for s in cats_data.get("subcategories", [])}

    cat_part = ""
    if item["category_id"] != -1:
        cat_part = cat_names.get(item["category_id"], "?")
        if item["subcat_id"] != -1:
            cat_part += f" / {sub_names.get(item['subcat_id'], '?')}"
        cat_part = f"  _{cat_part}_"

    time_part = ""
    timing = totals_map.get(item["id"])
    if timing and timing["total_seconds"] > 0:
        label = _fmt_duration(timing["total_seconds"])
        time_part = f"  [RUNNING {label}]" if timing["is_running"] else f"  [{label}]"

    return (f"[{item['id']}] **{item['text']}**"
            f"  `{item['status']}` `{item['priority']}`{cat_part}{time_part}")


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

    # cache the user role for the duration of the session
    if "user_role" not in st.session_state:
        try:
            me = api.get_me(token)
            st.session_state["user_role"] = me.get("role", "user")
        except PlanCError:
            st.session_state["user_role"] = "user"
    is_admin = st.session_state.get("user_role") == "admin"

    # load categories first so the sidebar can build dynamic filter options
    try:
        cats_data = api.get_categories(token)
    except PlanCError as e:
        st.error(f"Could not load categories: {e}")
        return

    # sidebar state that may not be set when not in admin/report mode
    show_reports     = False
    report_date_from = None
    report_date_to   = None

    # version counters — incrementing a counter changes the form key, which
    # forces Streamlit to render a brand-new widget with empty fields
    for _k in ("_ver_add_item", "_ver_add_cat", "_ver_add_subcat"):
        if _k not in st.session_state:
            st.session_state[_k] = 0

    with st.sidebar:
        st.header("Filters")
        show_all     = st.checkbox("Show all (including done / archived)")
        status_sel   = st.selectbox("Status",   ["(all)"] + STATUSES)
        priority_sel = st.selectbox("Priority", ["(all)"] + PRIORITIES)

        # category filter
        cats = cats_data.get("categories", [])
        cat_opt_labels = ["(all)"] + [f"[{c['id']}] {c['name']}" for c in cats]
        cat_opt_ids    = {f"[{c['id']}] {c['name']}": c["id"] for c in cats}
        cat_sel        = st.selectbox("Category", cat_opt_labels)
        selected_cat_id = cat_opt_ids.get(cat_sel)  # None when "(all)"

        # subcategory filter — limited to selected category when one is chosen
        all_subcats = cats_data.get("subcategories", [])
        visible_subcats = (
            [s for s in all_subcats if s["category_id"] == selected_cat_id]
            if selected_cat_id is not None else all_subcats
        )
        subcat_opt_labels = ["(all)"] + [f"[{s['id']}] {s['name']}" for s in visible_subcats]
        subcat_opt_ids    = {f"[{s['id']}] {s['name']}": s["id"] for s in visible_subcats}
        subcat_sel        = st.selectbox("Subcategory", subcat_opt_labels)
        selected_subcat_id = subcat_opt_ids.get(subcat_sel)  # None when "(all)"

        st.divider()
        if st.button("Logout"):
            for k in ("token", "user_role"):
                st.session_state.pop(k, None)
            st.rerun()

        if is_admin:
            st.divider()
            st.subheader("Admin")
            with st.expander("Change password"):
                with st.form("chpw"):
                    new_pw = st.text_input("New password", type="password")
                    if st.form_submit_button("Change"):
                        try:
                            api.change_password(token, new_pw)
                            st.success("Password updated.")
                        except PlanCError as e:
                            st.error(str(e))

            show_reports = st.checkbox("Reports")
            if show_reports:
                report_date_from = st.date_input("From", value=None, key="rpt_from")
                report_date_to   = st.date_input("To",   value=None, key="rpt_to")

    status_filter   = None if status_sel   == "(all)" else status_sel
    priority_filter = None if priority_sel == "(all)" else priority_sel

    st.title("planc")

    # ── reports view ──────────────────────────────────────────────────────────
    if is_admin and show_reports:
        st.subheader("Time report")
        date_from_s = str(report_date_from) if report_date_from else None
        date_to_s   = str(report_date_to)   if report_date_to   else None
        try:
            report_rows = api.time_report(token, date_from=date_from_s, date_to=date_to_s)
        except PlanCError as e:
            st.error(f"Could not load report: {e}")
            report_rows = []

        if not report_rows:
            st.info("No timesheet entries found for the selected period.")
        else:
            for session in report_rows:
                dur = _fmt_duration(session["total_seconds"])
                notes_prev = session["notes"]
                if len(notes_prev) > 50:
                    notes_prev = notes_prev[:50] + "…"
                running_tag = "  ▶" if session["stopped_at"] is None else ""
                cat_part = session.get("category", "")
                if session.get("subcategory"):
                    cat_part += f" / {session['subcategory']}"
                header = (f"[{session['task_id']}] {session['task_text']}"
                          + (f"  _({cat_part})_" if cat_part else "")
                          + f"  —  {session['started_at'][:10]}"
                          + f"  —  {dur}{running_tag}"
                          + (f"  —  {notes_prev}" if notes_prev else ""))

                with st.expander(header):
                    # parse stored timestamps ("YYYY-MM-DDTHH:MM:SSZ")
                    try:
                        start_dt = datetime.strptime(
                            session["started_at"][:19], "%Y-%m-%dT%H:%M:%S")
                    except ValueError:
                        start_dt = datetime.now().replace(second=0, microsecond=0)

                    stop_dt = None
                    if session["stopped_at"]:
                        try:
                            stop_dt = datetime.strptime(
                                session["stopped_at"][:19], "%Y-%m-%dT%H:%M:%S")
                        except ValueError:
                            stop_dt = None

                    with st.form(f"edit_session_{session['id']}"):
                        col_start, col_stop = st.columns(2)
                        with col_start:
                            st.caption("Started at")
                            new_start_date = st.date_input(
                                "Start date", value=start_dt.date(),
                                key=f"sd_{session['id']}")
                            new_start_time = st.time_input(
                                "Start time", value=start_dt.time(),
                                step=60, key=f"st_{session['id']}")
                        with col_stop:
                            st.caption("Stopped at")
                            new_stop_date = st.date_input(
                                "Stop date",
                                value=stop_dt.date() if stop_dt else None,
                                key=f"ed_{session['id']}")
                            new_stop_time = st.time_input(
                                "Stop time",
                                value=stop_dt.time() if stop_dt else _time(0, 0),
                                step=60, key=f"et_{session['id']}")

                        new_notes = st.text_area(
                            "Notes", value=session["notes"],
                            key=f"rn_{session['id']}")

                        if st.form_submit_button("Save"):
                            new_started_at = datetime.combine(
                                new_start_date, new_start_time
                            ).strftime("%Y-%m-%dT%H:%M:%SZ")

                            new_stopped_at = None
                            if new_stop_date:
                                new_stopped_at = datetime.combine(
                                    new_stop_date, new_stop_time
                                ).strftime("%Y-%m-%dT%H:%M:%SZ")

                            try:
                                api.update_session(
                                    token, session["id"],
                                    new_started_at, new_stopped_at, new_notes)
                                st.rerun()
                            except PlanCError as e:
                                st.error(str(e))

            # CSV export uses the raw API fields
            buf = io.StringIO()
            writer = _csv.DictWriter(
                buf, fieldnames=["id", "task_id", "task_text",
                                 "category", "subcategory",
                                 "started_at", "stopped_at",
                                 "total_seconds", "notes"])
            writer.writeheader()
            writer.writerows(report_rows)
            st.download_button(
                "Download CSV",
                buf.getvalue(),
                file_name="planc_report.csv",
                mime="text/csv",
            )
        return  # skip task view while in report mode

    # ── add item ─────────────────────────────────────────────────────────────
    with st.expander("Add item"):
        with st.form(f"add_item_{st.session_state['_ver_add_item']}"):
            text       = st.text_input("Text")
            subcat_labels, subcat_id_map = _build_subcat_options(cats_data)
            subcat_sel = st.selectbox("Subcategory", subcat_labels)
            if st.form_submit_button("Add"):
                if not text.strip():
                    st.warning("Text cannot be empty.")
                else:
                    subcat_id, cat_id = ((-1, -1) if subcat_sel == "(none)"
                                         else subcat_id_map[subcat_sel])
                    try:
                        api.add_item(token, text.strip(), cat_id, subcat_id)
                        st.session_state["_ver_add_item"] += 1
                        st.rerun()
                    except PlanCError as e:
                        st.error(str(e))

    st.divider()

    # ── item list ─────────────────────────────────────────────────────────────
    try:
        items       = api.list_items(token, show_all=show_all,
                                     status=status_filter, priority=priority_filter,
                                     cat_id=selected_cat_id, subcat_id=selected_subcat_id)
        active_info = api.time_active(token)   # {} or {"task_id":N,"started_at":"...","notes":"..."}
        totals_map  = {t["task_id"]: t for t in api.time_totals(token)}
    except PlanCError as e:
        st.error(f"Could not load data: {e}")
        return

    active_id = active_info.get("task_id")
    subcat_labels, subcat_id_map = _build_subcat_options(cats_data)

    # pending start/stop state keys (one pending operation at a time)
    pending_start = st.session_state.get("_pending_start")  # item_id or None
    pending_stop  = st.session_state.get("_pending_stop")   # item_id or None

    if not items:
        st.info("No items match the current filters.")
    else:
        for item in items:
            timing        = totals_map.get(item["id"], {"total_seconds": 0, "is_running": 0})
            is_running    = bool(timing["is_running"])
            other_running = active_id is not None and active_id != item["id"]

            with st.expander(_item_header(item, cats_data, totals_map), expanded=False):
                st.caption(f"created: {item['created_at']}")

                col_done, col_del, col_time, _ = st.columns([1, 1, 1, 5])
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
                with col_time:
                    if is_running:
                        # show Stop only while not already in stop-confirm mode
                        if pending_stop != item["id"]:
                            if st.button("Stop", key=f"stop_{item['id']}"):
                                st.session_state["_pending_stop"] = item["id"]
                                st.rerun()
                    else:
                        # show Start only while not already in start-confirm mode
                        if pending_start != item["id"]:
                            if st.button("Start", key=f"start_{item['id']}",
                                         disabled=other_running,
                                         help=(f"Task {active_id} is running"
                                               if other_running else None)):
                                st.session_state["_pending_start"] = item["id"]
                                st.rerun()

                # ── start notes confirmation form ─────────────────────────
                if not is_running and pending_start == item["id"]:
                    with st.form(f"start_notes_{item['id']}"):
                        st.caption("What will you work on?")
                        start_notes = st.text_area(
                            "Notes", key=f"sn_{item['id']}",
                            label_visibility="collapsed")
                        c1, c2, _ = st.columns([1, 1, 6])
                        with c1:
                            if st.form_submit_button("Start"):
                                try:
                                    api.time_start(token, item["id"], start_notes)
                                    st.session_state.pop("_pending_start", None)
                                    st.rerun()
                                except PlanCError as e:
                                    st.error(str(e))
                        with c2:
                            if st.form_submit_button("Cancel"):
                                st.session_state.pop("_pending_start", None)
                                st.rerun()

                # ── stop notes confirmation form ──────────────────────────
                if is_running and pending_stop == item["id"]:
                    with st.form(f"stop_notes_{item['id']}"):
                        st.caption("Ratify what was done:")
                        stop_notes = st.text_area(
                            "Notes",
                            value=active_info.get("notes", ""),
                            key=f"en_{item['id']}",
                            label_visibility="collapsed")
                        c1, c2, _ = st.columns([1, 1, 6])
                        with c1:
                            if st.form_submit_button("Stop"):
                                try:
                                    api.time_stop(token, item["id"], stop_notes)
                                    st.session_state.pop("_pending_stop", None)
                                    st.rerun()
                                except PlanCError as e:
                                    st.error(str(e))
                        with c2:
                            if st.form_submit_button("Cancel"):
                                st.session_state.pop("_pending_stop", None)
                                st.rerun()

                # ── inline edit form ──────────────────────────────────────
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
                        api.add_category(token, cat_name.strip())
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

            if cats:
                with st.form(f"add_subcat_{st.session_state['_ver_add_subcat']}"):
                    cat_choices = {f"[{c['id']}] {c['name']}": c["id"] for c in cats}
                    parent_sel  = st.selectbox("Parent category", list(cat_choices))
                    sub_name    = st.text_input("Name")
                    if st.form_submit_button("Add subcategory"):
                        try:
                            api.add_subcategory(
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
