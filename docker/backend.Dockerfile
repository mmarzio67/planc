# ── stage 1: compile libplanc.so ─────────────────────────────────────────────
FROM gcc:14-bookworm AS builder

WORKDIR /build
RUN apt-get update && apt-get install -y --no-install-recommends \
        libsqlite3-dev \
    && rm -rf /var/lib/apt/lists/*

COPY core/include/ core/include/
COPY core/src/     core/src/
COPY Makefile .

RUN make all

# ── stage 2: runtime ──────────────────────────────────────────────────────────
FROM python:3.12-slim

WORKDIR /app

# sqlite3 shared library needed at runtime by libplanc.so
RUN apt-get update && apt-get install -y --no-install-recommends \
        libsqlite3-0 \
    && rm -rf /var/lib/apt/lists/*

# bring in the compiled library from the builder stage
COPY --from=builder /build/libplanc.so .

# install Python dependencies
COPY api/requirements.txt .
RUN pip install --no-cache-dir -r requirements.txt

# application code
COPY api/ api/

# planc.db and auth.db are expected here (mounted as a named volume)
ENV PLANC_DB_PATH=/data/planc.db
ENV PLANC_AUTH_DB_PATH=/data/auth.db

EXPOSE 8000

# --app-dir tells uvicorn where to find main.py without needing PYTHONPATH
CMD ["uvicorn", "main:app", \
     "--host", "0.0.0.0", \
     "--port", "8000", \
     "--app-dir", "/app/api"]
