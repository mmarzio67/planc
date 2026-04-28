"""
planc FastAPI layer — HTTP endpoints over libplanc.so + JWT authentication.

Start with:
    uvicorn api.main:app --reload   (from the project root)
or:
    cd api && uvicorn main:app --reload
"""

from typing import Annotated

from fastapi import Depends, FastAPI, HTTPException, status
from fastapi.security import OAuth2PasswordBearer, OAuth2PasswordRequestForm
from jose import JWTError
from pydantic import BaseModel

import auth
import planc

app = FastAPI(title="planc API")

oauth2 = OAuth2PasswordBearer(tokenUrl="/auth/login")


@app.on_event("startup")
def _startup():
    auth.init_db()


# ─── auth dependency ─────────────────────────────────────────────────────────

def current_user(token: Annotated[str, Depends(oauth2)]) -> dict:
    try:
        return auth.decode_token(token)
    except JWTError:
        raise HTTPException(status_code=status.HTTP_401_UNAUTHORIZED,
                            detail="invalid or expired token",
                            headers={"WWW-Authenticate": "Bearer"})


def admin_user(user: Annotated[dict, Depends(current_user)]) -> dict:
    if user.get("role") != "admin":
        raise HTTPException(status_code=status.HTTP_403_FORBIDDEN,
                            detail="admin role required")
    return user


# ─── auth routes ─────────────────────────────────────────────────────────────

class Token(BaseModel):
    access_token: str
    token_type:   str = "bearer"


@app.post("/auth/login", response_model=Token)
def login(form: Annotated[OAuth2PasswordRequestForm, Depends()]):
    user = auth.authenticate(form.username, form.password)
    if not user:
        raise HTTPException(status_code=status.HTTP_401_UNAUTHORIZED,
                            detail="incorrect username or password",
                            headers={"WWW-Authenticate": "Bearer"})
    return Token(access_token=auth.create_token(user))


class NewUser(BaseModel):
    username: str
    password: str
    role:     str = "user"


@app.post("/auth/users", status_code=status.HTTP_201_CREATED)
def create_user(body: NewUser, _: Annotated[dict, Depends(admin_user)]):
    try:
        uid = auth.create_user(body.username, body.password, body.role)
    except ValueError as e:
        raise HTTPException(status_code=status.HTTP_409_CONFLICT, detail=str(e))
    return {"id": uid, "username": body.username, "role": body.role}


class PasswordChange(BaseModel):
    new_password: str


@app.put("/auth/users/me/password", status_code=status.HTTP_204_NO_CONTENT)
def change_password(body: PasswordChange,
                    user: Annotated[dict, Depends(current_user)]):
    auth.change_password(user["sub"], body.new_password)


# ─── item routes ─────────────────────────────────────────────────────────────

@app.get("/items")
def list_items(show_all:  bool        = False,
               status:    str | None  = None,
               priority:  str | None  = None,
               _:         Annotated[dict, Depends(current_user)] = None):
    try:
        return planc.list_items(show_all=show_all, status=status, priority=priority)
    except RuntimeError as e:
        raise HTTPException(status_code=500, detail=str(e))


class NewItem(BaseModel):
    text:       str
    cat_id:     int = -1
    subcat_id:  int = -1


@app.post("/items", status_code=status.HTTP_201_CREATED)
def add_item(body: NewItem, _: Annotated[dict, Depends(current_user)]):
    try:
        new_id = planc.add_item(body.text, body.cat_id, body.subcat_id)
    except RuntimeError as e:
        raise HTTPException(status_code=500, detail=str(e))
    return {"id": new_id}


class UpdateItem(BaseModel):
    text:       str | None  = None
    cat_id:     int         = -2   # -2 = keep current
    subcat_id:  int         = -2
    priority:   str | None  = None
    status:     str | None  = None


@app.put("/items/{item_id}", status_code=status.HTTP_204_NO_CONTENT)
def update_item(item_id: int, body: UpdateItem,
                _: Annotated[dict, Depends(current_user)]):
    try:
        planc.update_item(item_id,
                          text=body.text,
                          cat_id=body.cat_id,
                          subcat_id=body.subcat_id,
                          priority=body.priority,
                          status=body.status)
    except RuntimeError as e:
        raise HTTPException(status_code=404, detail=str(e))


@app.delete("/items/{item_id}", status_code=status.HTTP_204_NO_CONTENT)
def delete_item(item_id: int, _: Annotated[dict, Depends(current_user)]):
    try:
        planc.delete_item(item_id)
    except RuntimeError as e:
        raise HTTPException(status_code=404, detail=str(e))


# ─── category routes ──────────────────────────────────────────────────────────

@app.get("/categories")
def get_categories(_: Annotated[dict, Depends(current_user)]):
    try:
        return planc.get_categories()
    except RuntimeError as e:
        raise HTTPException(status_code=500, detail=str(e))


class NewCategory(BaseModel):
    name: str


@app.post("/categories", status_code=status.HTTP_201_CREATED)
def add_category(body: NewCategory, _: Annotated[dict, Depends(current_user)]):
    try:
        new_id = planc.add_category(body.name)
    except RuntimeError as e:
        raise HTTPException(status_code=500, detail=str(e))
    return {"id": new_id}


class NewSubcategory(BaseModel):
    cat_id: int
    name:   str


@app.post("/subcategories", status_code=status.HTTP_201_CREATED)
def add_subcategory(body: NewSubcategory,
                    _: Annotated[dict, Depends(current_user)]):
    try:
        new_id = planc.add_subcategory(body.cat_id, body.name)
    except RuntimeError as e:
        raise HTTPException(status_code=404, detail=str(e))
    return {"id": new_id}


# ─── time tracking routes ────────────────────────────────────────────────────

@app.post("/items/{item_id}/start", status_code=status.HTTP_204_NO_CONTENT)
def start_timer(item_id: int, _: Annotated[dict, Depends(current_user)]):
    try:
        planc.time_start(item_id)
    except RuntimeError as e:
        raise HTTPException(status_code=status.HTTP_409_CONFLICT, detail=str(e))


@app.post("/items/{item_id}/stop", status_code=status.HTTP_204_NO_CONTENT)
def stop_timer(item_id: int, _: Annotated[dict, Depends(current_user)]):
    try:
        planc.time_stop(item_id)
    except RuntimeError as e:
        raise HTTPException(status_code=status.HTTP_409_CONFLICT, detail=str(e))


@app.get("/timesheet/active")
def get_active(_: Annotated[dict, Depends(current_user)]):
    try:
        return planc.time_active()
    except RuntimeError as e:
        raise HTTPException(status_code=500, detail=str(e))


@app.get("/timesheet/totals")
def get_totals(_: Annotated[dict, Depends(current_user)]):
    try:
        return planc.time_totals()
    except RuntimeError as e:
        raise HTTPException(status_code=500, detail=str(e))


# ─── health ───────────────────────────────────────────────────────────────────

@app.get("/health")
def health():
    return {"status": "ok"}
