#!/usr/bin/env python3
"""Canonical Z017 host/source/SIL closeout runner.

Runs focused Z016/Z017 protocol/monitor evidence and consumes or runs the full
inherited Z015 regression. It never performs target builds, flashing or
physical validation.
"""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time


class GateFailure(RuntimeError):
    pass


def run_stage(name: str, cmd: list[str], cwd: Path, records: list[dict], env=None) -> None:
    print(f"\n=== {name} ===", flush=True)
    print(">>> " + " ".join(cmd), flush=True)
    started=time.monotonic()
    cp=subprocess.run(cmd,cwd=cwd,env=env,check=False)
    records.append({"name":name,"command":cmd,"returncode":cp.returncode,
                    "seconds":round(time.monotonic()-started,3)})
    if cp.returncode != 0:
        raise GateFailure(f"{name} failed with exit code {cp.returncode}")


def load_inherited(path: Path) -> dict:
    try:
        d=json.loads(path.read_text(encoding="utf-8"))
    except Exception as exc:
        raise GateFailure(f"cannot read inherited Z015 report {path}: {exc}") from exc
    if not d.get("success"):
        raise GateFailure("inherited Z015 report is not successful")
    if d.get("skipped"):
        raise GateFailure("inherited Z015 report contains skipped evidence: " + repr(d.get("skipped")))
    if not d.get("thread_sanitizer_performed"):
        raise GateFailure("inherited Z015 report lacks ThreadSanitizer evidence")
    if d.get("target_build_performed") or d.get("hardware_validation_performed"):
        raise GateFailure("inherited host report improperly claims target/hardware work")
    return d


def main() -> int:
    ap=argparse.ArgumentParser()
    ap.add_argument("repo_root", nargs="?", type=Path, default=Path("."))
    ap.add_argument("--report", type=Path, default=None)
    ap.add_argument("--inherited-report", type=Path, default=None,
                    help="Successful full Z015 host/SIL report to consume instead of rerunning it")
    ap.add_argument("--require-clang", action="store_true")
    args=ap.parse_args()

    repo=args.repo_root.resolve()
    report=(args.report.resolve() if args.report else
            repo/"build/z017_host_validation_report.json")
    records=[]
    inherited=None
    start=time.monotonic()
    success=False

    required=(
        repo/"scripts/check_z017_contract.py",
        repo/"scripts/check_z017_mutations.py",
        repo/"scripts/check_z016_link_contract.py",
        repo/"scripts/check_z016_mutations.py",
        repo/"tests/unit/adbms_monitor/Makefile",
        repo/"tests/unit/adbms_link/Makefile",
        repo/"docs/migration/evidence/Z017_ORACLE_SHA256SUMS_2026-09-08.txt",
    )
    for p in required:
        if not p.is_file():
            print(f"FAIL: missing Z017 closeout artifact: {p}",file=sys.stderr)
            return 2

    try:
        clang=shutil.which("clang")
        if args.require_clang and not clang:
            raise GateFailure("clang required but unavailable")

        print("Z-017 canonical host/source/SIL closeout")
        print(f"repo: {repo}")
        print("scope: inherited Z001..Z015 + Z016 String-B link + Z017 init/POST/cell acquisition")
        print("excluded: target build/flash, physical SPI/isoSPI/cell validation, authority promotion, Z018+")

        run_stage("Z017 checker syntax",
                  [sys.executable,"-m","py_compile",
                   str(repo/"scripts/check_z017_contract.py"),
                   str(repo/"scripts/check_z017_mutations.py"),
                   str(repo/"scripts/build_manifest.py")],repo,records)
        run_stage("Z017 source/profile/ownership contract",
                  [sys.executable,str(repo/"scripts/check_z017_contract.py"),str(repo)],repo,records)
        run_stage("Z017 mutation/negative controls",
                  [sys.executable,str(repo/"scripts/check_z017_mutations.py"),str(repo)],repo,records)
        run_stage("Z016 inherited link contract",
                  [sys.executable,str(repo/"scripts/check_z016_link_contract.py"),str(repo)],repo,records)
        run_stage("Z016 inherited mutations",
                  [sys.executable,str(repo/"scripts/check_z016_mutations.py"),str(repo)],repo,records)

        mon=repo/"tests/unit/adbms_monitor"
        run_stage("Z017 monitor directed/randomized + adapter",
                  ["make","-C",str(mon),"clean","test","adapter"],repo,records)
        run_stage("Z017 monitor ASan/UBSan",
                  ["make","-C",str(mon),"asan","ubsan"],repo,records)
        run_stage("Z017 monitor GCC analyzer",
                  ["make","-C",str(mon),"analyze"],repo,records)
        if clang:
            env=os.environ.copy(); env["CLANG"]=clang
            run_stage("Z017 monitor Clang analyzer",
                      ["make","-C",str(mon),"clang-analyze"],repo,records,env)
        elif args.require_clang:
            raise GateFailure("clang analyzer unavailable")

        link=repo/"tests/unit/adbms_link"
        run_stage("Z016 protocol differential + adapter/probe",
                  ["make","-C",str(link),"clean","test","adapter","probe"],repo,records)
        run_stage("Z016 protocol ASan/UBSan",
                  ["make","-C",str(link),"asan"],repo,records)
        run_stage("Z016 adapter/probe sanitizer build",
                  ["make","-C",str(link),"adapter","probe",
                   "EXTRA=-fsanitize=address,undefined -fno-omit-frame-pointer"],repo,records)

        if args.inherited_report:
            inherited=load_inherited(args.inherited_report.resolve())
            records.append({"name":"Inherited Z015 canonical report consumed",
                            "command":[str(args.inherited_report.resolve())],
                            "returncode":0,"seconds":0.0,
                            "inherited_stage_count":len(inherited.get("stages",[])),
                            "inherited_seconds":inherited.get("seconds")})
            print(f"PASS: inherited Z015 report {args.inherited_report.resolve()}")
        else:
            inherited_path=repo/"build/z017_inherited_z015_report.json"
            run_stage("Full inherited Z015 canonical host/SIL",
                      [sys.executable,str(repo/"scripts/run_z015_host_validation.py"),str(repo),
                       "--require-clang","--tsan","--report",str(inherited_path)],repo,records)
            inherited=load_inherited(inherited_path)

        success=True
    except GateFailure as exc:
        print("FAIL: "+str(exc),file=sys.stderr)
    finally:
        payload={
            "schema":"der27-ams-z017-host-validation-v1",
            "success":success,
            "repo":str(repo),
            "scope":"Z017 source/host/SIL only; no target/hardware/authority/Z018+",
            "target_build_performed":False,
            "hardware_validation_performed":False,
            "authority_promoted":False,
            "later_migration_stage_performed":False,
            "inherited_z015_success":bool(inherited and inherited.get("success")),
            "inherited_z015_stage_count":len(inherited.get("stages",[])) if inherited else 0,
            "inherited_z015_tsan_performed":bool(inherited and inherited.get("thread_sanitizer_performed")),
            "seconds":round(time.monotonic()-start,3),
            "stages":records,
        }
        report.parent.mkdir(parents=True,exist_ok=True)
        report.write_text(json.dumps(payload,indent=2)+"\n",encoding="utf-8")
        print(f"Report: {report}")

    # Clean focused products only after report has been written.
    if success:
        subprocess.run(["make","-C",str(repo/"tests/unit/adbms_monitor"),"clean"],cwd=repo,check=False)
        subprocess.run(["make","-C",str(repo/"tests/unit/adbms_link"),"clean"],cwd=repo,check=False)
        print("\nPASS: complete Z017 host/source/SIL closeout")
        print("Target build and physical validation remain separate gates.")
        return 0
    return 1


if __name__ == "__main__":
    sys.exit(main())
