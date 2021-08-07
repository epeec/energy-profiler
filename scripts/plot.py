#! /usr/bin/env python3

import csv
import sys
import argparse
import itertools
import distutils.util
from typing import Dict, Iterable
import matplotlib
import matplotlib.pyplot as plt


def str_as_bool(s=None) -> bool:
    return False if not s else bool(distutils.util.strtobool(s))


def raise_empty_value() -> None:
    raise ValueError("Value cannot be empty")


class store_key_pairs(argparse.Action):
    def __init__(
        self,
        option_strings,
        dest=None,
        nargs=None,
        store_as=str,
        empty_val=None,
        **kwargs
    ) -> None:
        self.store_as = store_as
        self.empty_val = empty_val
        super().__init__(option_strings, dest, nargs, **kwargs)

    def __call__(self, parser, namespace, values, option_string) -> None:
        retval = {}
        for kv in values:
            k, *v = kv.split("=", 1)
            if not k:
                raise argparse.ArgumentError(self, "Key cannot be empty")
            try:
                if not v or not v[0]:
                    retval[k] = self.empty_val()
                else:
                    retval[k] = self.store_as(v[0])
            except ValueError as err:
                raise argparse.ArgumentError(
                    self, err.args[0] if err.args else "<empty>"
                )
        setattr(namespace, self.dest, retval)


def read_from(path):
    if not path:
        return sys.stdin
    else:
        return open(path, "r")


def output_to(path):
    if not path:
        return sys.stdout
    else:
        return open(path, "wb")


def add_arguments(parser):
    parser.add_argument(
        "source_file",
        action="store",
        help="file to extract from",
        nargs="?",
        type=str,
        default=None,
    )
    parser.add_argument(
        "-o",
        "--output",
        action="store",
        help="destination file or stdout if not present",
        required=False,
        type=str,
        default=None,
    )
    parser.add_argument(
        "-x",
        "--x-plots",
        action=store_key_pairs,
        store_as=str_as_bool,
        empty_val=str_as_bool,
        help="""column(s) to use as the x values as keys,
            with booleans as values that indicate whether to subtract
            the first x value of the corresponding column from the rest""",
        required=True,
        nargs="+",
        metavar="NAME=BOOL",
        default=None,
        dest="x",
    )
    parser.add_argument(
        "-y",
        "--y-plots",
        action=store_key_pairs,
        store_as=str_as_bool,
        empty_val=str_as_bool,
        help="""column(s) to use as the y values as keys,
            with booleans as values that indicate whether to subtract
            the first y value of the corresponding column from the rest""",
        required=True,
        nargs="+",
        metavar="NAME=BOOL",
        default=None,
        dest="y",
    )
    parser.add_argument(
        "-t",
        "--title",
        action="store",
        help="plot title",
        required=False,
        type=str,
        default=None,
    )
    parser.add_argument(
        "-u",
        "--units",
        action=store_key_pairs,
        store_as=str,
        empty_val=raise_empty_value,
        help="plot units",
        required=False,
        nargs="*",
        metavar="NAME=UNIT",
        default={},
    )
    parser.add_argument(
        "-b",
        "--backend",
        action="store",
        type=str,
        help="backend to use when generating plot",
        required=False,
        choices=["agg", "pdf", "svg"],
        default="agg",
    )


def convert_input(fields, data) -> dict:
    retval = {f: [] for f in fields}
    first_row = None
    for row in data:
        if not first_row:
            first_row = row
        for k, v in row.items():
            if k in fields:
                retval[k].append(
                    float(v) - float(first_row[k]) if fields[k] else float(v)
                )
    return retval


def assert_key_pairs(kps, fieldnames):
    for k in kps:
        if k not in fieldnames:
            raise ValueError("'{}' is not a valid column".format(k))


def unique_units(plots: Iterable[str], units: Dict[str, str]):
    units = {v for k, v in units.items() if k in plots}
    return len(units) <= 1


def set_legend(line, x, y):
    line.set_label("{}({})".format(y, x))


def get_label(plots: Iterable[str], units: Dict[str, str]):
    if len(plots) == 1:
        p = next(iter(plots))
        if not units:
            return p
        else:
            u = units.get(p, None)
            return "{} ({})".format(p, u) if not u is None else p
    return " / ".join({units[p] for p in plots if p in units}) if units else ""


def main():
    parser = argparse.ArgumentParser(description="Generate plot from CSV file")
    add_arguments(parser)
    args = parser.parse_args()
    if len(args.x) > 1 and len(args.y) > 1 and len(args.x) != len(args.y):
        raise ValueError("if more than one X, then number must match Y count")
    if not unique_units(args.x, args.units):
        raise ValueError("units for x plots must be the same")

    with read_from(args.source_file) as f, output_to(args.output) as o:
        csvrdr = csv.DictReader(row for row in f if not row.startswith("#"))
        assert_key_pairs(args.x, csvrdr.fieldnames)
        assert_key_pairs(args.y, csvrdr.fieldnames)
        converted = convert_input({**args.x, **args.y}, csvrdr)

        matplotlib.use(args.backend)
        with plt.ioff():
            fig, ax = plt.subplots()
            ax.set_title(args.title if args.title else f.name)
            ax.minorticks_on()
            ax.set_xlabel(get_label(args.x, args.units))
            ax.set_ylabel(get_label(args.y, args.units))
            for x, y in itertools.zip_longest(
                args.x, args.y, fillvalue=next(iter(args.x))
            ):
                (line,) = ax.plot(converted[x], converted[y])
                set_legend(line, x, y)
            legend = ax.legend(
                bbox_to_anchor=(0.0, 1.1, 1.0, 0.1),
                loc="lower left",
                ncol=1,
                mode="expand",
                borderaxespad=0.0,
            )
            plt.grid(which="major", axis="both", linestyle="dotted", alpha=0.5)
            plt.savefig(o, bbox_extra_artists=(legend,), bbox_inches="tight")


if __name__ == "__main__":
    main()