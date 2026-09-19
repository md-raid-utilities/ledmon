# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2023 Intel Corporation.

from ledctl.ledctl_cmd import LedctlCmd
import pytest
import logging

LOGGER = logging.getLogger(__name__)

SUCCESS_EXIT_CODE = 0
CMDLINE_ERROR_EXIT_CODE = 35

# Line length limit for best user experience.
MAX_LINE_LENGTH = 100


def test_parameters_are_valid_long_test_flag(ledctl_binary):
    cmd = LedctlCmd(ledctl_binary)
    cmd.is_test_flag_enabled()
    cmd.run_ledctl_cmd("--list-controllers --test".split())


@pytest.mark.parametrize(
    "valid_mode_commands",
    [
        "-h", "--help", "-v", "--version", "-L -T", "--list-controllers -T",
        "-P -n vmd -T", "--list-slots --controller-type=vmd -T",
        "-P -n amd -T", "--list-slots --controller-type=amd -T",
        "-G -n vmd -d /dev/nvme0n1 -T",
        "--get-slot --controller-type=vmd --device=/dev/nvme0n1 -T",
        "-G -n vmd -p 1 -T", "--get-slot --controller-type=vmd --slot=1 -T",
        "-G -n amd -p ata5-bay3 -T",
        "--get-slot --controller-type=amd --slot=ata5-bay3 -T",
        "-G -n vmd -d /dev/nvme0n1 -r state -T",
        "--get-slot --controller-type=vmd --device=/dev/nvme0n1 --print=state -T",
        "-G -n vmd -p 1 -r state -T",
        "--get-slot --controller-type=vmd --slot=1 --print=state -T",
        "-S -n vmd -p 1 -s normal -T",
        "--set-slot --controller-type=vmd --slot=1 --state=normal -T",
        "-S -n amd -p ata5-bay3 -s locate -T",
        "--set-slot --controller-type=amd --slot=ata5-bay3 --state=locate_off -T",
        "-B -d /dev/nvme0n1 -T",
        "--default-controller --device /dev/nvme0n1 -T"
    ],
)
def test_parameters_are_valid_short_test_flag(ledctl_binary,
                                              valid_mode_commands):
    cmd = LedctlCmd(ledctl_binary)
    cmd.is_test_flag_enabled()
    cmd.run_ledctl_cmd(valid_mode_commands.split())


@pytest.mark.parametrize(
    "valid_ibpi_commands",
    [
        "normal=/dev/nvme0n1 -T", "normal=/dev/nvme0n1 -x -T",
        "normal=/dev/nvme0n1 --listed-only -T",
        "normal=/dev/nvme0n1 -l /var/log/ledctl.log -T",
        "normal=/dev/nvme0n1 --log=/var/log/ledctl.log -T",
        "normal=/dev/nvme0n1 --log-level=all -T",
        "normal=/dev/nvme0n1 --all -T", "-x normal={ /dev/nvme0n1 } -T",
        "locate=/dev/nvme0n1 rebuild={ /sys/block/sd[a-b] } -T",
        "off={ /dev/sda /dev/sdb } -T", "locate=/dev/sda,/dev/sdb,/dev/sdc -T"
    ],
)
def test_ibpi_parameters_are_valid_short_test_flag(ledctl_binary,
                                                   valid_ibpi_commands):
    cmd = LedctlCmd(ledctl_binary)
    cmd.is_test_flag_enabled()
    cmd.run_ledctl_cmd(valid_ibpi_commands.split())


@pytest.mark.parametrize(
    "log_path_commands",
    [
        "--list-controllers --log=/root/test.log -T",
        "normal=/dev/nvme0n1 --listed-only -l /root/test.log -T",
    ],
)
def test_parameters_log_path(ledctl_binary, log_path_commands):
    cmd = LedctlCmd(ledctl_binary)
    cmd.is_test_flag_enabled()
    output = cmd.run_ledctl_cmd_valid(log_path_commands.split()).stdout
    assert "LOG_PATH=/root/test.log" in output


def test_parameter_log_level_all_values(ledctl_binary):
    cmd = LedctlCmd(ledctl_binary)
    cmd.is_test_flag_enabled()
    tested_log_levels = ["warning", "debug", "all", "info", "quiet", "error"]
    for level in tested_log_levels:
        args = "--list-controllers -T --log-level=" + level
        output = cmd.run_ledctl_cmd_valid(args.split()).stdout
        assert "LOG_LEVEL=" + level.upper() in output
        args = "--list-controllers -T --" + level
        output = cmd.run_ledctl_cmd_valid(args.split()).stdout
        assert "LOG_LEVEL=" + level.upper() in output


def parse_version(lines):
    help_header = [
        "Enclosure LED Control Application",
        "Copyright (C) 2009-2024 Intel Corporation."
    ]

    assert (lines[0].startswith(help_header[0]))
    assert (lines[1] == help_header[1])
    assert (lines[2] == "")


# Check help style and verify that size is no longer than 100.
def parse_help(lines):
    parse_version(lines)

    line_inter = iter(lines[3:])

    line = next(line_inter)
    assert (line.startswith("Usage: ledctl --"))
    assert (line.endswith(" [option...] ..."))
    assert (next(line_inter) == "")

    # Now description comes, ended by empty line.
    while next(line_inter) != "":
        continue

    # Here options comes.
    line = next(line_inter)
    assert (line == "Options:" or line == "Modes:")

    # Control place of short option printing to keep columns equals
    length = 0
    while True:
        line = next(line_inter)
        if line == "": break
        LOGGER.debug(f"line {line}")
        if length == 0:
            length = line.find(" -")
        else:
            assert (line.find(" -") == length)

    help_footer = [
        "Refer to ledctl(8) man page for more detailed description (man ledctl).",
        "Bugs should be reported at: https://github.com/md-raid-utilities/ledmon/issues"
    ]

    assert (next(line_inter) == help_footer[0])
    assert (next(line_inter) == help_footer[1])
    assert (next(line_inter) == "")
    assert (next(line_inter, "end") == "end")

    # Check globally that line limit is not exceeded.
    for line in lines:
        assert (len(line) <= MAX_LINE_LENGTH)


@pytest.mark.parametrize(
    "help_cmd",
    [
        "--help",
        "-h",
        "--help --badflag",
        "-hd",
        "-h -s",
        "--set-slot --help",
        "-S -h --badflag",
        "-Sh --badflag",
        "--get-slot --help",
        "-Ghb",
        "--list-controllers --help",
        "--ibpi --help",
        "--list-slots --help",
        "-L -h",
        "--default-controller --help",
        "-Bh",
    ],
)
# Check formatting, header and footer.
# Test does not check options/mode content and description.
def test_main_help(ledctl_binary, help_cmd):
    cmd = LedctlCmd(ledctl_binary)
    cmd.is_test_flag_enabled()
    res = cmd.run_ledctl_cmd_valid(help_cmd.split()).stdout
    lines = res.split('\n')
    parse_help(lines)


@pytest.mark.parametrize(
    "version_cmd",
    ["--version", "-v", "-vh", "--version --badflag"],
)
def test_version(ledctl_binary, version_cmd):
    cmd = LedctlCmd(ledctl_binary)
    cmd.is_test_flag_enabled()
    res = cmd.run_ledctl_cmd_valid(version_cmd.split()).stdout
    lines = res.split('\n')
    parse_version(lines)


@pytest.mark.parametrize("nexist_dev", ["/dev/nvr_gon_giv_u_up"])
# Check if proper message is returned for not existing device.
def test_nexist_dev_output(ledctl_binary, nexist_dev):
    cmd = LedctlCmd(ledctl_binary)
    cmd.is_test_flag_enabled()
    res = cmd.run_ledctl_cmd_not_valid(["locate={}".format(nexist_dev)])
    assert "Could not find {}".format(nexist_dev) in res.stderr


@pytest.mark.parametrize("unsprtd_dev", ["/dev/zero"])
# Check if proper message is returned for unsupported device.
def test_unsprtd_dev_output(ledctl_binary, unsprtd_dev):
    cmd = LedctlCmd(ledctl_binary)
    cmd.is_test_flag_enabled()
    res = cmd.run_ledctl_cmd_not_valid(["locate={}".format(unsprtd_dev)])
    assert "{}: device not supported".format(unsprtd_dev) in res.stderr
