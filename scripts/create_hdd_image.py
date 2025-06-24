#!/usr/bin/env python3
import argparse
import os
import sys
from pyfatx import Fatx

def inject_folder_to_fatx(fs, folder_path, fatx_base="/"):
    for root, _, files in os.walk(folder_path):
        for file in files:
            abs_path = os.path.join(root, file)
            rel_path = os.path.relpath(abs_path, folder_path)
            fatx_path = os.path.join(fatx_base, rel_path).replace(os.sep, "/")

            # Read file data
            with open(abs_path, "rb") as f:
                data = f.read()

            # Create directory structure if necessary
            parent_dir = os.path.dirname(fatx_path)
            if parent_dir != "/":
                fs.mkdir(parent_dir, recursive=True)

            # Write to FATX image
            fs.write(fatx_path, data)
            print(f"Added: {fatx_path}")


def main():
    parser = argparse.ArgumentParser(
        description="Create a FATX image and inject all files from a directory."
    )
    parser.add_argument("folder", help="Path to folder containing files to inject")
    parser.add_argument(
        "-o", "--output", default="xbox_hdd.img",
        help="Output image file (default: xbox_hdd.img)"
    )

    args = parser.parse_args()

    if not os.path.isdir(args.folder):
        print(f"Error: Folder not found - {args.folder}", file=sys.stderr)
        sys.exit(1)

    # Create new FATX image
    Fatx.create(args.output)

    # Open the C partition
    fs = Fatx(args.output, drive="c")

    # Inject all files
    inject_folder_to_fatx(fs, args.folder)


if __name__ == "__main__":
    main()
