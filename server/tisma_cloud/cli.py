"""tisma-cloud: one program for the master server, a computing node and the administration. It is the executable built
by the CI (PyInstaller), to run the service on a machine without Docker or Python:

    tisma-cloud master [--host 0.0.0.0] [--port 8000] [--env .env]
    tisma-cloud node [--config node.toml]
    tisma-cloud admin <command> ...        (as tisma-cloud-admin)

The settings of the master are environment variables (.env.example); "master" and "admin" also read them from the file
given by --env (default: .env in the current folder, if there is one). The variables already set win."""

from __future__ import annotations

import os
import sys
from pathlib import Path

USAGE = __doc__.split("\n\n", 1)[1]


def load_env(path: str | os.PathLike) -> int:
    """Sets the variables of a .env file (KEY=VALUE lines, # comments, optional quotes) which are not set yet."""
    file = Path(path)
    if not file.is_file():
        return 0
    count = 0
    for line in file.read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if not line or line.startswith("#") or "=" not in line:
            continue
        key, value = line.split("=", 1)
        key, value = key.strip(), value.strip()
        if len(value) >= 2 and value[0] == value[-1] and value[0] in "\"'":
            value = value[1:-1]
        if key and key not in os.environ:
            os.environ[key] = value
            count += 1
    return count


def take_option(args: list[str], name: str, default: str) -> str:
    """Removes "--name value" (or --name=value) from args and returns the value."""
    for i, a in enumerate(args):
        if a == name and i + 1 < len(args):
            value = args[i + 1]
            del args[i:i + 2]
            return value
        if a.startswith(name + "="):
            del args[i]
            return a.split("=", 1)[1]
    return default


def run_master(args: list[str]) -> int:
    import uvicorn

    from .api import create_app

    host = take_option(args, "--host", "0.0.0.0")
    port = int(take_option(args, "--port", "8000"))
    load_env(take_option(args, "--env", ".env"))
    if args:
        print(f"Unknown arguments: {' '.join(args)}\n\n{USAGE}", file=sys.stderr)
        return 1
    # As deploy/Dockerfile.master: behind Caddy, at most 300 requests at a time, short keep-alive.
    uvicorn.run(create_app(), host=host, port=port, proxy_headers=True, forwarded_allow_ips="*",
                limit_concurrency=300, timeout_keep_alive=5, server_header=False)
    return 0


def main(argv: list[str] | None = None) -> int:
    args = list(sys.argv[1:] if argv is None else argv)
    if not args or args[0] in ("-h", "--help", "help"):
        print(USAGE)
        return 0 if args else 1
    command, rest = args[0], args[1:]
    if command == "master":
        return run_master(rest)
    if command == "node":
        from .node.__main__ import main as node_main

        return node_main(rest)
    if command == "admin":
        from .admin import main as admin_main

        load_env(take_option(rest, "--env", ".env"))
        return admin_main(rest)
    print(f"Unknown command {command}\n\n{USAGE}", file=sys.stderr)
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
