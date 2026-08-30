#!/usr/bin/env python3
import argparse
import json
import subprocess
from pathlib import Path


def run(executable, operation, arguments):
    return subprocess.run(
        [executable, operation, *arguments],
        check=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("c_oracle")
    parser.add_argument("idric_oracle")
    parser.add_argument(
        "--fixtures",
        default=str(Path(__file__).with_name("transport-fixtures.json")),
    )
    arguments = parser.parse_args()

    fixtures = json.loads(Path(arguments.fixtures).read_text(encoding="utf-8"))
    failures = []
    for fixture in fixtures:
        c_result = run(arguments.c_oracle, fixture["operation"], fixture["arguments"])
        idric_result = run(arguments.idric_oracle, fixture["operation"], fixture["arguments"])
        if (
            c_result.returncode != idric_result.returncode
            or c_result.stdout != idric_result.stdout
            or c_result.stderr != idric_result.stderr
        ):
            failures.append(
                {
                    "name": fixture["name"],
                    "c": (c_result.returncode, c_result.stdout, c_result.stderr),
                    "idric": (
                        idric_result.returncode,
                        idric_result.stdout,
                        idric_result.stderr,
                    ),
                }
            )
        else:
            print("ok  ", fixture["name"])

    for failure in failures:
        print("FAIL", failure["name"])
        print("  C:    ", repr(failure["c"]))
        print("  Idriç:", repr(failure["idric"]))
    raise SystemExit(1 if failures else 0)


if __name__ == "__main__":
    main()
