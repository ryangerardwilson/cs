import os
import shutil
import subprocess
import tempfile
from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]
INSTALLER = ROOT / "install.sh"
SOURCE = ROOT / "cs.c"
VERSION_FILE = ROOT / "_version.py"


def _runtime_version() -> str:
    marker = '__version__ = "'
    for line in VERSION_FILE.read_text(encoding="utf-8").splitlines():
        if line.startswith(marker) and line.endswith('"'):
            return line[len(marker):-1]
    raise AssertionError("unable to parse _version.py")


class CsContractTests(unittest.TestCase):
    @staticmethod
    def _write_executable(path: Path, body: str) -> None:
        path.write_text(body, encoding="utf-8")
        path.chmod(0o755)

    @unittest.skipUnless(shutil.which("cc"), "cc is required")
    def test_version_flag_prints_single_value(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            output = Path(tmp) / "cs"
            subprocess.run(
                [
                    "cc",
                    "-O2",
                    "-Wall",
                    "-Wextra",
                    "-std=c11",
                    f'-DCS_VERSION="{_runtime_version()}"',
                    '-DCS_REPO_OWNER="ryangerardwilson"',
                    '-DCS_REPO_NAME="cs"',
                    str(SOURCE),
                    "-o",
                    str(output),
                ],
                check=True,
                cwd=ROOT,
            )

            result = subprocess.run(
                [str(output), "-v"],
                capture_output=True,
                text=True,
                check=True,
            )

            self.assertEqual(result.stdout, f"{_runtime_version()}\n")
            self.assertEqual(result.stderr, "")

    @unittest.skipUnless(shutil.which("cc"), "cc is required")
    def test_update_flag_delegates_to_installer_upgrade_mode(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            tmp_path = Path(tmp)
            output = tmp_path / "cs"
            subprocess.run(
                [
                    "cc",
                    "-O2",
                    "-Wall",
                    "-Wextra",
                    "-std=c11",
                    f'-DCS_VERSION="{_runtime_version()}"',
                    '-DCS_REPO_OWNER="ryangerardwilson"',
                    '-DCS_REPO_NAME="cs"',
                    str(SOURCE),
                    "-o",
                    str(output),
                ],
                check=True,
                cwd=ROOT,
            )

            bin_dir = tmp_path / "bin"
            bin_dir.mkdir()
            log_path = tmp_path / "sh-args.txt"

            self._write_executable(
                bin_dir / "curl",
                "#!/usr/bin/bash\n"
                "url=\"${@: -1}\"\n"
                "if [[ \"$url\" == \"https://api.github.com/repos/ryangerardwilson/cs/releases/latest\" ]]; then\n"
                "  printf '%s\n' '{\"tag_name\":\"v0.1.3\"}'\n"
                "  exit 0\n"
                "fi\n"
                "if [[ \"$url\" == \"https://raw.githubusercontent.com/ryangerardwilson/cs/main/install.sh\" ]]; then\n"
                "  printf '#!/usr/bin/env bash\nexit 0\n'\n"
                "  exit 0\n"
                "fi\n"
                "echo unexpected curl call >&2\n"
                "exit 1\n",
            )
            self._write_executable(
                bin_dir / "sh",
                "#!/usr/bin/bash\n"
                f"printf '%s\n' \"$*\" > {log_path}\n"
                "cat >/dev/null\n"
                "exit 0\n",
            )

            env = os.environ.copy()
            env["PATH"] = f"{bin_dir}:{env['PATH']}"

            result = subprocess.run(
                [str(output), "-u"],
                capture_output=True,
                text=True,
                env=env,
                check=True,
            )

            self.assertIn("Upgrading to cs 0.1.3...", result.stdout)
            self.assertEqual(log_path.read_text(encoding="utf-8").strip(), "-s -- -u")

    def test_installer_version_flag_without_argument_prints_latest_release(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            tmp_path = Path(tmp)
            bin_dir = tmp_path / "bin"
            bin_dir.mkdir()

            self._write_executable(
                bin_dir / "curl",
                "#!/usr/bin/bash\n"
                "if [[ \"$1\" == \"-fsSL\" && \"$2\" == \"https://api.github.com/repos/ryangerardwilson/cs/releases/latest\" ]]; then\n"
                "  printf '%s\n' '{\"tag_name\":\"v0.1.3\"}'\n"
                "  exit 0\n"
                "fi\n"
                "echo unexpected curl call >&2\n"
                "exit 1\n",
            )

            env = os.environ.copy()
            env["PATH"] = f"{bin_dir}:{env['PATH']}"

            result = subprocess.run(
                ["/usr/bin/bash", str(INSTALLER), "-v"],
                capture_output=True,
                text=True,
                env=env,
                check=True,
            )

            self.assertEqual(result.stdout, "0.1.3\n")
            self.assertEqual(result.stderr, "")

    def test_installer_upgrade_same_version_uses_cs_v_and_exits_cleanly(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            tmp_path = Path(tmp)
            bin_dir = tmp_path / "bin"
            home_dir = tmp_path / "home"
            bin_dir.mkdir()
            home_dir.mkdir()

            self._write_executable(
                bin_dir / "curl",
                "#!/usr/bin/bash\n"
                "if [[ \"$1\" == \"-fsSL\" && \"$2\" == \"https://api.github.com/repos/ryangerardwilson/cs/releases/latest\" ]]; then\n"
                "  printf '%s\n' '{\"tag_name\":\"v0.1.3\"}'\n"
                "  exit 0\n"
                "fi\n"
                "echo unexpected curl call >&2\n"
                "exit 1\n",
            )
            self._write_executable(
                bin_dir / "cs",
                "#!/usr/bin/bash\n"
                "if [[ \"$1\" == \"-v\" ]]; then\n"
                "  printf '0.1.3\\n'\n"
                "  exit 0\n"
                "fi\n"
                "echo unexpected cs invocation >&2\n"
                "exit 1\n",
            )

            env = os.environ.copy()
            env["PATH"] = f"{bin_dir}:{env['PATH']}"
            env["HOME"] = str(home_dir)

            result = subprocess.run(
                ["/usr/bin/bash", str(INSTALLER), "-u"],
                capture_output=True,
                text=True,
                env=env,
                check=True,
            )

            self.assertEqual(result.stdout, "")
            self.assertEqual(result.stderr, "")


if __name__ == "__main__":
    unittest.main()
