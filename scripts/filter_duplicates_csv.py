#!/usr/bin/env python3

import csv
import argparse
import sys
from typing import Any, Iterable, Optional, Tuple


def log(*args: Any) -> None:
    print("{}:".format(sys.argv[0]), *args, file=sys.stderr)


def read_from(path: Optional[str]):
    return sys.stdin if not path else open(path, "r")


def output_to(path: Optional[str]):
    return sys.stdout if not path else open(path, "w")


def add_arguments(parser: argparse.ArgumentParser) -> argparse.ArgumentParser:
    parser.add_argument(
        "source_file",
        action="store",
        help="file to filter (default: stdin)",
        nargs="?",
        type=str,
        default=None,
    )
    parser.add_argument(
        "-o",
        "--output",
        action="store",
        help="destination file (default: stdout)",
        required=False,
        type=str,
        default=None,
    )
    return parser


def read_file(f) -> Tuple[csv.reader, csv.reader]:
    comments = []
    rest = []
    for row in f:
        if row.lstrip().rstrip().startswith("#"):
            comments.append(row)
        else:
            rest.append(row)
    return csv.reader(comments), csv.reader(rest)


def unique_row(prev_row: Iterable, curr_row: Iterable) -> bool:
    if len(prev_row) != len(curr_row):
        raise AssertionError("Malformed file, rows have a different number of columns")
    for v1, v2 in zip(prev_row, curr_row):
        if v1 == v2:
            return False
    return True


def main():
    parser = argparse.ArgumentParser(
        description="""Filter duplicate readings from CSV file;
            one equal reading is enough to make the row a candidate for removal"""
    )
    args = add_arguments(parser).parse_args()
    with read_from(args.source_file) as f:
        comments, data = read_file(f)
        line_num = 0
        with output_to(args.output) as of:
            writer = csv.writer(of)
            writer.writerows(comments)
            line_num += comments.line_num

            # fieldnames row
            prev_row = next(iter(data), None)
            if prev_row is not None:
                writer.writerow(prev_row)
                # first data row
                prev_row = next(iter(data), None)
                if prev_row is not None:
                    writer.writerow(prev_row)
                    for row in data:
                        if unique_row(prev_row, row):
                            writer.writerow(row)
                            prev_row = row
                        else:
                            log("Filtered out line", line_num + data.line_num)


if __name__ == "__main__":
    main()