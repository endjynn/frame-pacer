#!/usr/bin/env python3
"""Test tool bootstrapping and prove each strict gate rejects a bad fixture."""

import hashlib
import io
from pathlib import Path
import subprocess
import sys
import tarfile
import tempfile
import unittest
from unittest.mock import patch
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "tools"))
import compile_commands
import quality


class QualityToolsTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        self.addCleanup(self.temporary.cleanup)

    def fixture(self, name, text):
        path = self.root / name
        path.write_text(text)
        return path

    def tool(self, name, *args):
        return subprocess.run([quality.executable(name), *map(str, args)],
                              cwd=quality.ROOT, capture_output=True, text=True)

    def test_checksum_mismatch_is_rejected_before_extraction(self):
        spec = {"test": {"url": "https://example.invalid/tool", "version": "1.0",
                         "sha256": "0" * 64, "binary": "tool"}}
        with patch.object(quality, "CACHE", self.root), \
             patch.object(quality, "VERSIONS", spec), \
             patch.object(quality, "urlopen", return_value=io.BytesIO(b"invalid")):
            with self.assertRaisesRegex(ValueError, "checksum mismatch"):
                quality.install()
        self.assertEqual(list(self.root.iterdir()), [])

    def test_missing_tool_fails_instead_of_skipping(self):
        with patch.object(quality, "CACHE", self.root):
            with self.assertRaisesRegex(ValueError, "setup-tools"):
                quality.executable("clang-format")

    def test_wrong_tool_version_is_rejected(self):
        spec = quality.VERSIONS["shfmt"]
        path = self.root / ("shfmt-" + spec["sha256"]) / spec["binary"]
        path.parent.mkdir(parents=True)
        path.touch()
        result = subprocess.CompletedProcess([], 0, stdout="0.0.0\n")
        with patch.object(quality, "CACHE", self.root), \
             patch.object(quality.subprocess, "run", return_value=result):
            with self.assertRaisesRegex(ValueError, "unexpected"):
                quality.executable("shfmt")

    def test_archive_paths_and_links_cannot_escape(self):
        for name in ["../outside", "/outside"]:
            with self.subTest(name=name):
                buffer = io.BytesIO()
                with zipfile.ZipFile(buffer, "w") as archive:
                    archive.writestr(name, b"bad")
                with self.assertRaisesRegex(ValueError, "unsafe"):
                    quality.unpack(buffer.getvalue(), "test.whl", self.root, "tool")
        buffer = io.BytesIO()
        with tarfile.open(fileobj=buffer, mode="w:gz") as archive:
            entry = tarfile.TarInfo("link")
            entry.type = tarfile.SYMTYPE
            entry.linkname = "../outside"
            archive.addfile(entry)
        with self.assertRaisesRegex(ValueError, "non-regular"):
            quality.unpack(buffer.getvalue(), "test.tar.gz", self.root, "tool")

    def test_compile_recipes_preserve_architecture_macros_and_sources(self):
        entries = compile_commands.commands(
            "gcc -m32 -std=c17 -Ibuild -DFRAME_PACER_TEST "
            "-DTEST='\"two words\"' -shared -o build/test src/one.c tests/two.c -ldl\n")
        self.assertEqual([entry["file"] for entry in entries],
                         ["src/one.c", "tests/two.c"])
        for entry in entries:
            self.assertIn("-m32", entry["arguments"])
            self.assertIn("-DFRAME_PACER_TEST", entry["arguments"])
            self.assertIn('-DTEST="two words"', entry["arguments"])
            self.assertNotIn("-shared", entry["arguments"])
            self.assertNotIn("-ldl", entry["arguments"])
            self.assertEqual(entry["arguments"][-2:], ["-c", entry["file"]])

    def test_clang_format_check_is_strict_and_read_only(self):
        path = self.fixture("format.c", "int f( void ){return 1;}\n")
        before = hashlib.sha256(path.read_bytes()).digest()
        result = self.tool("clang-format", "--dry-run", "--Werror",
                           "--style=file:" + str(quality.ROOT / ".clang-format"), path)
        self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(before, hashlib.sha256(path.read_bytes()).digest())

    def test_clang_analyzer_diagnostic_is_an_error(self):
        path = self.fixture("null.c", "int bad(void) { int *p = 0; return *p; }\n")
        result = self.tool("clang-tidy", "--config-file=" + str(quality.ROOT / ".clang-tidy"),
                           path, "--", "-std=c17")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("clang-analyzer-core.NullDereference", result.stdout + result.stderr)

    def test_optin_analyzers_reject_bad_fixtures(self):
        fixtures = {
            "core.EnumCastOutOfRange":
                "enum value { FIRST, LAST };\n"
                "enum value bad(void) { return (enum value)99; }\n",
            "portability.UnixAPI":
                "#include <stdlib.h>\nvoid *bad(void) { return malloc(0); }\n",
            "taint.TaintedDiv":
                '#include <stdio.h>\nint bad(void) { unsigned n; '
                'if (scanf("%u", &n) != 1) return 0; return 100 / n; }\n',
            "taint.GenericTaint":
                '#include <stdio.h>\n#include <stdlib.h>\n'
                'int bad(void) { char command[64]; '
                'if (scanf("%63s", command) != 1) return 0; '
                'return system(command); }\n',
            "performance.Padding":
                "struct padded { char a; long aa; char b; long bb; char c; "
                "long cc; char d; long dd; char e; long ee; };\n"
                "struct padded bad;\n",
        }
        for check, source in fixtures.items():
            with self.subTest(check=check):
                path = self.fixture("optin.c", source)
                result = self.tool(
                    "clang-tidy", "--config-file=" + str(quality.ROOT / ".clang-tidy"),
                    path, "--", "-std=c17")
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("clang-analyzer-optin." + check,
                              result.stdout + result.stderr)

    def test_shellcheck_rejects_unquoted_expansion(self):
        path = self.fixture("bad.sh", '#!/bin/sh\nprintf "%s\\n" $HOME\n')
        result = self.tool("shellcheck", "--severity=style", path)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("SC2086", result.stdout + result.stderr)

    def test_shfmt_check_is_strict_and_read_only(self):
        path = self.fixture("format.sh", "#!/bin/sh\nif true;then echo ok;fi\n")
        before = path.read_bytes()
        result = self.tool("shfmt", "-ln", "posix", "-i", "4", "-ci", "-d", path)
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(before, path.read_bytes())

    def test_actionlint_rejects_invalid_job_dependency(self):
        path = self.fixture("workflow.yml", "name: Test\non: push\njobs:\n"
                            "  test:\n    needs: nonexistent\n"
                            "    runs-on: ubuntu-22.04\n"
                            "    steps:\n      - run: echo ok\n")
        result = self.tool("actionlint", "-shellcheck", quality.executable("shellcheck"), path)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("nonexistent", result.stdout + result.stderr)

    def test_documentation_check_ignores_downloaded_tool_docs(self):
        cache = self.root / ".cache" / "vendor"
        cache.mkdir(parents=True)
        (cache / "README.md").write_text("[Not shipped](MISSING.md)\n")
        document = self.fixture("README.md", "Project documentation.\n")
        command = ["sh", str(quality.ROOT / "tests" / "check_markdown_links.sh")]
        result = subprocess.run(command, cwd=self.root, capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        document.write_text("[Actually broken](MISSING.md)\n")
        result = subprocess.run(command, cwd=self.root, capture_output=True, text=True)
        self.assertNotEqual(result.returncode, 0)


if __name__ == "__main__":
    unittest.main()
