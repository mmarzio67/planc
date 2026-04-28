"""
Authentication: SQLite-backed user store + JWT token issuance.

auth.db sits alongside planc.db (~/.local/share/plan/auth.db).
On first startup, a default admin user is created if the table is empty.
"""

import os
import sqlite3
from datetime import datetime, timedelta, timezone
from pathlib import Path

import bcrypt
from jose import JWTError, jwt

JWT_SECRET    = os.environ.get("PLANC_JWT_SECRET", "change-me-in-production")
JWT_ALGORITHM = "HS256"
TOKEN_TTL     = timedelta(hours=24)

DEFAULT_ADMIN_USER     = "admin"
DEFAULT_ADMIN_PASSWORD = "admin"


def _auth_db_path() -> str:
    explicit = os.environ.get("PLANC_AUTH_DB_PATH")
    if explicit:
        return explicit
    xdg = os.environ.get("XDG_DATA_HOME")
    if xdg:
        return f"{xdg}/plan/auth.db"
    home = os.environ["HOME"]
    return f"{home}/.local/share/plan/auth.db"


def _connect() -> sqlite3.Connection:
    path = _auth_db_path()
    Path(path).parent.mkdir(parents=True, exist_ok=True)
    return sqlite3.connect(path)


def _hash(password: str) -> str:
    return bcrypt.hashpw(password.encode(), bcrypt.gensalt()).decode()


def _verify(password: str, pw_hash: str) -> bool:
    return bcrypt.checkpw(password.encode(), pw_hash.encode())


def init_db() -> None:
    """Create the users table; seed a default admin if empty."""
    with _connect() as con:
        con.execute(
            "CREATE TABLE IF NOT EXISTS users ("
            "  id            INTEGER PRIMARY KEY,"
            "  username      TEXT    UNIQUE NOT NULL,"
            "  password_hash TEXT    NOT NULL,"
            "  role          TEXT    NOT NULL DEFAULT 'user'"
            ")"
        )
        count = con.execute("SELECT COUNT(*) FROM users").fetchone()[0]
        if count == 0:
            con.execute(
                "INSERT INTO users (username, password_hash, role) VALUES (?, ?, ?)",
                (DEFAULT_ADMIN_USER, _hash(DEFAULT_ADMIN_PASSWORD), "admin"),
            )
            print(f"[auth] created default admin user '{DEFAULT_ADMIN_USER}' "
                  f"— change the password immediately")


def authenticate(username: str, password: str) -> dict | None:
    """Return the user row dict if credentials are valid, else None."""
    with _connect() as con:
        row = con.execute(
            "SELECT id, username, password_hash, role FROM users WHERE username = ?",
            (username,),
        ).fetchone()
    if not row:
        return None
    user_id, uname, pw_hash, role = row
    if not _verify(password, pw_hash):
        return None
    return {"id": user_id, "username": uname, "role": role}


def create_token(user: dict) -> str:
    expire = datetime.now(tz=timezone.utc) + TOKEN_TTL
    payload = {
        "sub":  user["username"],
        "role": user["role"],
        "exp":  expire,
    }
    return jwt.encode(payload, JWT_SECRET, algorithm=JWT_ALGORITHM)


def decode_token(token: str) -> dict:
    """Return payload dict or raise JWTError."""
    return jwt.decode(token, JWT_SECRET, algorithms=[JWT_ALGORITHM])


def create_user(username: str, password: str, role: str = "user") -> int:
    """Insert a new user; raises ValueError on duplicate username."""
    with _connect() as con:
        try:
            cur = con.execute(
                "INSERT INTO users (username, password_hash, role) VALUES (?, ?, ?)",
                (username, _hash(password), role),
            )
            return cur.lastrowid
        except sqlite3.IntegrityError:
            raise ValueError(f"username '{username}' already exists")


def change_password(username: str, new_password: str) -> None:
    with _connect() as con:
        con.execute(
            "UPDATE users SET password_hash = ? WHERE username = ?",
            (_hash(new_password), username),
        )
