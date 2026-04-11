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

## Exit codes

| Code | Meaning |
|------|---------|
| 0 | success |
| 1 | bad usage |
| 2 | system error (I/O, memory) |
| 3 | item not found |
