"""Parser for command line arguments."""

import argparse
import shlex

from buildscripts.resmokelib import configure_resmoke
from buildscripts.resmokelib.discovery import DiscoveryPlugin
from buildscripts.resmokelib.generate_fcv_constants import \
    GenerateFCVConstantsPlugin
from buildscripts.resmokelib.hang_analyzer import HangAnalyzerPlugin
from buildscripts.resmokelib.powercycle import PowercyclePlugin
from buildscripts.resmokelib.run import RunPlugin
from buildscripts.resmokelib.symbolizer import SymbolizerPlugin
from buildscripts.resmokelib.undodb import UndoDbPlugin

_PLUGINS = [
    RunPlugin(),
    HangAnalyzerPlugin(),
    UndoDbPlugin(),
    PowercyclePlugin(),
    SymbolizerPlugin(),
    GenerateFCVConstantsPlugin(),
    DiscoveryPlugin(),
]


def parse(sys_args, usage=None):
    """Parse the CLI args."""

    parser = argparse.ArgumentParser(usage=usage)
    subparsers = parser.add_subparsers(dest="command")

    # Add sub-commands.
    for plugin in _PLUGINS:
        plugin.add_subcommand(subparsers)

    parsed_args = parser.parse_args(sys_args)

    return parser, parsed_args


def parse_command_line(sys_args, usage=None, **kwargs):
    """Parse the command line arguments passed to resmoke.py and return the subcommand object to execute."""
    parser, parsed_args = parse(sys_args, usage)

    subcommand = parsed_args.command

    for plugin in _PLUGINS:
        subcommand_obj = plugin.parse(subcommand, parser, parsed_args, **kwargs)
        if subcommand_obj is not None:
            return subcommand_obj

    raise RuntimeError(f"Resmoke configuration has invalid subcommand: {subcommand}. Try '--help'")


def set_run_options(argstr=''):
    """Populate the config module variables for the 'run' subcommand with the default options."""
    parser, parsed_args = parse(['run'] + shlex.split(argstr))
    configure_resmoke.validate_and_update_config(parser, parsed_args)
