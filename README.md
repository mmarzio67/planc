# plan

Minimal C17 CLI app inspired by `.plan`.

## Build

```bash
make
make clean
```

## Storage

Data is stored in `~/.local/share/plan/`:

- `items` — tasks
- `categories` — categories and subcategories

The directory is created automatically on first run. The location respects `$XDG_DATA_HOME` if set.

## Commands

### Tasks

```bash
./plan add "Investigate renderer regression"
./plan list                        # all tasks
./plan open                        # open tasks only (excludes done)
./plan update 1 --text "Investigate Vulkan path"           # text only, preserve subcategory
./plan update 1 --subcat 2                                 # subcategory only, preserve text
./plan update 1 --text "Investigate Vulkan path" --subcat 2  # both
./plan delete 1
./plan done 1
./plan show 1
```

### Categories

```bash
./plan cat add "job"
./plan cat add "casa"
./plan cat list
```

### Subcategories

A subcategory requires an existing category.

```bash
./plan subcat add <cat_id> "frontend"
./plan subcat list
```

---

## Web application

The same business logic is also exposed as a web application through a layered architecture:

```
libplanc.so  ←  C business logic (plan, storage, category, util, api)
     ↓ ctypes
api/         ←  FastAPI — HTTP endpoints + JWT authentication
     ↓ HTTP
frontend/    ←  Streamlit — browser UI
```

Both layers share the same `planc.db` SQLite database used by the CLI.

### Run with Docker (recommended)

```bash
docker compose up
```

- Streamlit UI: http://localhost:8501
- FastAPI docs: http://localhost:8000/docs

Data is persisted in `./data/` on the host. The directory is created automatically on first run.

To set a custom JWT secret (recommended in production):

```bash
PLANC_JWT_SECRET=your-secret docker compose up
```

### Run locally (development)

Build the shared library first:

```bash
make all
```

Then start the two servers in separate terminals from the project root:

```bash
# terminal 1 — API server
PYTHONPATH=api /path/to/python -m uvicorn main:app --reload --app-dir api

# terminal 2 — Streamlit frontend
PYTHONPATH=frontend /path/to/python -m streamlit run frontend/app.py
```

Install dependencies for each component:

```bash
pip install -r api/requirements.txt
pip install -r frontend/requirements.txt
```

### Authentication

On first startup a default admin user is created automatically:

| username | password |
|----------|----------|
| `admin`  | `admin`  |

Change the password immediately via the sidebar in the Streamlit UI or by calling `PUT /auth/users/me/password`.

New users can be added by an admin via `POST /auth/users`.

### Data migration from flat files

If you have existing data from the flat-file version of planc, run the migration tool once:

```bash
make migrate
./migrate
```

The tool reads the old `items` and `categories` files and writes them into `planc.db`. It is safe to run on an empty database.

---

## Exit codes

| Code | Meaning |
|------|---------|
| 0 | success |
| 1 | bad usage |
| 2 | system error (I/O, memory) |
| 3 | item not found |
