#!/usr/bin/env python3
import json
import os
import subprocess
import sys
import time


def fetch_release(api_url: str, token: str | None, max_attempts: int = 6) -> str:
    attempt = 0
    while attempt < max_attempts:
        attempt += 1
        cmd = [
            "curl",
            "-fsSL",
            "-L",
            "-w",
            "%{http_code}",
            "--connect-timeout",
            "10",
            "--max-time",
            "30",
        ]
        if token:
            cmd += ["-H", f"Authorization: Bearer {token}"]
        cmd.append(api_url)

        proc = subprocess.run(cmd, capture_output=True, text=True)
        stdout = proc.stdout[:-3] if len(proc.stdout) >= 3 else ""
        status = proc.stdout[-3:]

        if proc.returncode == 0 and status == "200":
            return stdout

        if status.isdigit() and status.startswith("5") and attempt < max_attempts:
            sleep_for = min(3 * attempt, 15)
            sys.stderr.write(
                f"curl returned {status}. Retrying in {sleep_for}s (attempt {attempt}/{max_attempts})\n"
            )
            sys.stderr.flush()
            time.sleep(sleep_for)
            continue

        sys.stderr.write(proc.stderr or f"curl failed with status {status}\n")
        sys.exit(proc.returncode or 1)

    sys.stderr.write("Exceeded retry attempts fetching latest release\n")
    sys.exit(1)


def main() -> int:
    if len(sys.argv) < 3:
        sys.stderr.write(
            "Usage: find-release-asset.py <api_url> <asset_name> [asset_name...]\n"
        )
        return 1

    api_url = sys.argv[1]
    asset_names = sys.argv[2:]
    token = os.environ.get("GITHUB_TOKEN") or os.environ.get("GH_TOKEN")

    payload = fetch_release(api_url, token)
    try:
        release = json.loads(payload)
    except json.JSONDecodeError as exc:
        sys.stderr.write(f"Failed to parse release JSON: {exc}\n")
        return 1

    if os.environ.get("CS_HELPER_SILENT") != "1":
        sys.stderr.write(f"Release tag: {release.get('tag_name', 'Unknown')}\n")
        sys.stderr.write(f"Number of assets: {len(release.get('assets', []))}\n")

    assets = {asset.get("name"): asset.get("browser_download_url", "")
              for asset in release.get("assets", [])}

    missing = []
    for name in asset_names:
        url = assets.get(name, "")
        if not url:
            missing.append(name)
        print(url)

    if missing:
        sys.stderr.write("Missing assets: " + ", ".join(missing) + "\n")
        return 1

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
