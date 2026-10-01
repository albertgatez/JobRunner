# JobRunner (J.U.A.N.) — User Manual

**Job Utility for Administration of Nodes**

A Linux-based background job execution and management system. Submit OS commands as background jobs, monitor their status, retrieve their output, and cancel them — all through a simple CLI.

---

## Table of Contents

1. [Overview](#overview)
2. [Installation](#installation)
3. [Quick Start](#quick-start)
4. [Commands](#commands)
5. [Job States](#job-states)
6. [Output Format](#output-format)
7. [Exit Codes](#exit-codes)
8. [Architecture Overview](#architecture-overview)
9. [Troubleshooting](#troubleshooting)
10. [Limitations & Planned Features](#limitations--planned-features)

---

## Overview

JobRunner consists of two programs:

| Program | Purpose |
|---------|---------|
| `jobrunner-server` | Background daemon that receives, runs, and manages jobs |
| `jobrunner-cli` | Command-line client to interact with the server |

The server listens on a **Unix domain socket** (default: `/tmp/jobrunner.sock`). The client sends JSON-formatted requests to that socket and prints the JSON response to stdout.

---

## Installation

### Prerequisites

- Linux operating system
- C++20-compatible compiler (`g++` or `clang++`)
- [`nlohmann/json`](https://github.com/nlohmann/json) header library installed on your system

### Build

Compile all source files together:

```bash
# Server
g++ -std=c++20 -O2 \
  src/server/*.cpp \
  src/common/*.cpp \
  src/domain/*.cpp \
  src/io/*.cpp \
  src/network/*.cpp \
  src/process/*.cpp \
  src/protocol/*.cpp \
  -I/usr/include/nlohmann \
  -o ./src/build/jobrunner-server 

# CLI client
g++ -std=c++20 -O2 \
  src/client/*.cpp \
  src/protocol/*.cpp \
  -I/usr/include/nlohmann \
  -o ./src/build/jobrunner-cli
```

---

## Quick Start

Open two terminal windows.

**Terminal 1 — Start the server:**

```bash
./jobrunner-server
```

The server prints log messages to stderr and runs in the foreground. Press `Ctrl+C` to shut it down gracefully.

**Terminal 2 — Submit a job:**

```bash
./jobrunner-cli <socket> submit echo "Hello from JobRunner"
```

You should see a JSON response like:

```json
{"ok":true,"job":{"id":1,"state":"RUNNING","command":"echo","args":["Hello from JobRunner"]}}
```

Now check the result:

```bash
./jobrunner-cli <socket> status 1
```

```json
{"ok":true,"job":{"id":1,"state":"SUCCEEDED","command":"echo","args":["Hello from JobRunner"],"exit_code":0,"stdout":"Hello from JobRunner\n","stderr":""}}
```

---

## Commands

### `submit` — Run a command as a background job

```bash
./jobrunner-cli <socket> submit <command> [args...]
```

| Parameter | Required | Description |
|-----------|----------|-------------|
| `command` | Yes | The executable to run (e.g., `ls`, `python3`, `sleep`) |
| `args` | No | Arguments to pass to the command |

**Examples:**

```bash
# Simple command
./jobrunner-cli <socket> submit ls -la /tmp

# Command with multiple arguments
./jobrunner-cli <socket> submit python3 -c "print(42)"

# Long-running command
./jobrunner-cli <socket> submit sleep 30
```

**Notes:**

- Empty commands are rejected with an error.
- If you submit the identical command with identical arguments within **3 seconds**, the server returns the original job ID instead of creating a duplicate.
- There is no concurrency limit — all submitted jobs start immediately.

---

### `status` — Check a specific job

```bash
./jobrunner-cli <socket> status <id>
```

| Parameter | Required | Description |
|-----------|----------|-------------|
| `id` | Yes | The numeric job ID returned by `submit` |

**Example:**

```bash
./jobrunner-cli <socket> status 1
```

```json
{
  "ok": true,
  "job": {
    "id": 1,
    "command": "echo",
    "args": ["hello"],
    "state": "SUCCEEDED",
    "exit_code": 0,
    "exit_signal": null,
    "stdout": "hello\n",
    "stderr": "",
    "received_at": "2026-09-26T10:00:00Z",
    "started_at": "2026-09-26T10:00:00Z",
    "finished_at": "2026-09-26T10:00:00Z"
  }
}
```

---

### `list` — List all jobs (with optional filter)

```bash
./jobrunner-cli <socket> list [state]
```

| Parameter | Required | Description |
|-----------|----------|-------------|
| `state` | No | Filter by state: `QUEUED`, `RUNNING`, `SUCCEEDED`, `FAILED`, or `CANCELED` |

**Examples:**

```bash
# List all jobs
./jobrunner-cli <socket> list

# List only running jobs
./jobrunner-cli <socket> list RUNNING

# List only failed jobs
./jobrunner-cli <socket> list FAILED
```

**Response format:**

```json
{
  "ok": true,
  "jobs": [
    {"id": 1, "command": "echo", "state": "SUCCEEDED"},
    {"id": 2, "command": "sleep", "state": "RUNNING"}
  ]
}
```

---

### `cancel` — Cancel a queued or running job

```bash
./jobrunner-cli <socket> cancel <id>
```

| Parameter | Required | Description |
|-----------|----------|-------------|
| `id` | Yes | The numeric job ID to cancel |

**Behavior:**

| Current State | Result |
|---------------|--------|
| `QUEUED` | Marked as `CANCELED` immediately (never starts) |
| `RUNNING` | SIGTERM sent to the job's process group |
| `SUCCEEDED` / `FAILED` / `CANCELED` | No-op — returns success, no side effects |

Canceling an already-finished job is **idempotent** — it always returns `"ok": true`.

---

## Job States

Every job moves through a defined state machine:

```
  submit
    │
    ▼
 ┌────────┐
 │ QUEUED │
 └────────┘
    │          │
    │ start    │ cancel
    ▼          ▼
 ┌────────┐  ┌──────────┐
 │RUNNING │  │ CANCELED │
 └────────┘  └──────────┘
    │
    ├── exit code 0 ──▶ SUCCEEDED
    ├── exit code ≠ 0 ─▶ FAILED
    ├── killed by signal ▶ FAILED
    └── cancel (SIGTERM) ▶ CANCELED
```

| State | Meaning |
|-------|---------|
| `QUEUED` | Accepted by the server, waiting to start |
| `RUNNING` | Child process is currently executing |
| `SUCCEEDED` | Process exited normally with exit code 0 |
| `FAILED` | Process exited with non-zero code or was killed by a signal |
| `CANCELED` | Canceled by user before or during execution |

Terminal states (`SUCCEEDED`, `FAILED`, `CANCELED`) are final and cannot change.

---

## Output Format

All responses are JSON objects. Every response contains an `"ok"` field:

### Success

```json
{"ok": true, ...}
```

### Error

```json
{"ok": false, "error": "human-readable error message"}
```

### Job Object Fields

| Field | Type | Description |
|-------|------|-------------|
| `id` | integer | Unique job identifier |
| `command` | string | Executable that was run |
| `args` | array of strings | Arguments passed to the command |
| `state` | string | Current state (see [Job States](#job-states)) |
| `exit_code` | integer / null | Exit code if the process exited normally, otherwise `null` |
| `exit_signal` | integer / null | Signal number if the process was killed, otherwise `null` |
| `stdout` | string | Captured standard output |
| `stderr` | string | Captured standard error |
| `received_at` | string (ISO 8601) | When the job was accepted |
| `started_at` | string (ISO 8601) | When the process was forked |
| `finished_at` | string (ISO 8601) | When the job reached a terminal state |

---

## Exit Codes

The CLI process exits with:

| Code | Meaning |
|------|---------|
| `0` | The server returned `"ok": true` |
| `1` | The server returned `"ok": false`, or a communication error occurred |

---

## Architecture Overview

```
┌──────────────┐     Unix socket      ┌─────────────────────────┐
│ jobrunner-cli│ ◄──────────────────► │    jobrunner-server     │
│   (client)   │   length-prefixed    │                         │
│              │    JSON messages     │  ┌───────────────────┐  │
└──────────────┘                      │  │  epoll reactor    │  │
                                      │  │  (event loop)     │  │
                                      │  └──────┬────────────┘  │
                                      │         │               │
                                      │  ┌──────▼────────────┐  │
                                      │  │   JobManager      │  │
                                      │  │ (business logic)  │  │
                                      │  └──┬──────────┬─────┘  │
                                      │     │          │        │
                                      │  ┌──▼──┐   ┌───▼──────┐ │
                                      │  │Store│   │ Process  │ │
                                      │  │(mem)│   │ Launcher │ │
                                      │  └─────┘   └──────────┘ │
                                      └─────────────────────────┘
```

Key design points:

- **Single-threaded event loop** — all I/O and signal handling happens on one thread using `epoll(7)`. No locks, no race conditions.
- **Length-prefixed framing** — each message has a 4-byte big-endian length header followed by a UTF-8 JSON payload (max 1 MiB).
- **In-memory storage** — job data is held in memory and is lost when the server restarts.
- **Separate stdout/stderr** — each job's output streams are captured independently through pipes.

---

## Troubleshooting

| Problem | Cause | Solution |
|---------|-------|----------|
| `connect: No such file or directory` | Server is not running | Start the server first: `./jobrunner-server` |
| `connect: Connection refused` | Wrong socket path | Use the same socket path for server and client. Default is `/tmp/jobrunner.sock` |
| `{"ok":false,"error":"empty command"}` | No command provided | Provide a command: `./jobrunner-cli <socket> submit ls` |
| Job stuck in `QUEUED` | (Currently unlikely) | Jobs start immediately in this version; this will change when a real queue is implemented |
| Server doesn't shut down on Ctrl+C | — | Send SIGTERM: `kill <pid>` |

---

## Limitations & Planned Features

The following are **not yet implemented** in this version:

| Feature | Description |
|---------|-------------|
| **Persistence** | Jobs are stored in memory only. A server restart loses all job data. SQLite persistence is planned. |
| **Remote access** | Only local Unix socket communication. TCP listener for LAN/VPN access is planned. |
| **Configuration file** | No config file support. All settings use hardcoded defaults. |