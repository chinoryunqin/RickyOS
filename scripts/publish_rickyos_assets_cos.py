#!/usr/bin/env python3
"""Publish a built RickyOS catalog (fonts or standby pictures) to the RickyOS COS bucket.

Takes the output directory of build_rickyos_font_catalog.py or build_rickyos_wallpapers.py,
built with --cos-base, and uploads with coscli (https://github.com/tencentyun/coscli):

  1. <dir>/<kind>/*  ->  cos://<bucket>/<kind>/<tag>/   immutable, never overwritten
  2. <dir>/cos.json  ->  cos://<bucket>/<kind>/cos.json  no-cache, last

The manifest goes last, so a reader never sees a list whose files are not there yet;
the release folders are immutable, so a reader mid-download keeps its files.

Credentials come from the environment, never the command line or the repository:
COS_SECRET_ID, COS_SECRET_KEY, COS_BUCKET (<bucket>-<appid>), COS_REGION (ap-guangzhou)
and optionally COS_SESSION_TOKEN.

Run: python3 scripts/publish_rickyos_assets_cos.py --kind fonts --output DIR --tag v1.1.0 [--coscli PATH] [--dry-run]
"""
import argparse
import json
import os
from pathlib import Path
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
from rickyos_asset_routes import check_cos_base  # noqa: E402

KINDS = ("fonts", "wallpapers")
IMMUTABLE = "Cache-Control:public,max-age=31536000,immutable"
ROLLING = "Cache-Control:no-cache"


def plan(kind, output, tag, bucket, region):
    """The coscli copies to run, in order, after checking the build output."""
    if kind not in KINDS:
        raise ValueError(f"kind must be one of {KINDS}")
    files = output / kind
    manifest = output / "cos.json"
    if not files.is_dir() or not any(files.iterdir()):
        raise ValueError(f"No files in {files}")
    if not manifest.is_file():
        raise ValueError(f"{manifest} is missing: build with --cos-base")
    document = json.loads(manifest.read_text())
    base = document.get("baseUrl", "")
    expected_host = f".cos.{region}.myqcloud.com"
    domain = base.split(f"/{kind}/")[0]
    check_cos_base(domain)
    if domain != f"https://{bucket}{expected_host}" or base != f"{domain}/{kind}/{tag}/":
        raise ValueError(f"cos.json baseUrl {base} does not match bucket {bucket} in {region}, {kind} {tag}")
    return [
        (f"{files}/", f"cos://{bucket}/{kind}/{tag}/", ["--recursive", "--forbid-overwrite", "--meta", IMMUTABLE]),
        (str(manifest), f"cos://{bucket}/{kind}/cos.json", ["--meta", ROLLING]),
    ]


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--kind", choices=KINDS, required=True)
    parser.add_argument("--output", type=Path, required=True, help="the build script's output directory")
    parser.add_argument("--tag", required=True, help="the release tag the catalog was built with")
    parser.add_argument("--coscli", default="coscli")
    parser.add_argument("--dry-run", action="store_true", help="print the copies without credentials or uploads")
    args = parser.parse_args()

    bucket = os.environ.get("COS_BUCKET", "")
    region = os.environ.get("COS_REGION", "ap-guangzhou")
    if not bucket:
        parser.error("set COS_BUCKET (<bucket>-<appid>) in the environment")
    copies = plan(args.kind, args.output.resolve(), args.tag, bucket, region)
    if args.dry_run:
        for source, target, options in copies:
            print(f"{source} -> {target} {' '.join(options)}")
        return

    secret_id = os.environ.get("COS_SECRET_ID", "")
    secret_key = os.environ.get("COS_SECRET_KEY", "")
    if not secret_id or not secret_key:
        parser.error("set COS_SECRET_ID and COS_SECRET_KEY in the environment")
    credentials = ["--init-skip", "--disable-log", "-i", secret_id, "-k", secret_key,
                   "-e", f"cos.{region}.myqcloud.com"]
    if os.environ.get("COS_SESSION_TOKEN"):
        credentials += ["--token", os.environ["COS_SESSION_TOKEN"]]
    for source, target, options in copies:
        print(f"Uploading {source} -> {target}", flush=True)
        subprocess.run([args.coscli, "cp", *credentials, source, target, *options], check=True)
    print(f"Published: https://{bucket}.cos.{region}.myqcloud.com/{args.kind}/cos.json")


if __name__ == "__main__":
    main()
