#!/usr/bin/env python

import argparse
from support_package_utils import CommandHandler, handle_k8s_universe, add_k8s_subparser, \
    handle_ssh_universe, add_ssh_subparser


# To extend functionality, import handler functions and parsers
# and add (handler, parser) pairs here
# Key of dictionary is name of subcommand
# Handler function must take only args
ops = {
    'k8s': CommandHandler(handle_k8s_universe, add_k8s_subparser),
    'ssh': CommandHandler(handle_ssh_universe, add_ssh_subparser)
}


def parse_args():
    parser = argparse.ArgumentParser(
        formatter_class=argparse.ArgumentDefaultsHelpFormatter)
    subparsers = parser.add_subparsers(help='support package for k8s or ssh', dest='subcommand')
    for key in ops:
        ops[key].parser(subparsers, key)

    return parser.parse_args()


def main():
    args = parse_args()
    ops[args.subcommand].handler(args)


if __name__ == "__main__":
    main()
