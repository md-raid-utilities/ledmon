# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2023 Intel Corporation.

import subprocess
import logging
from os import listdir
from os.path import join, realpath, exists
import re

LOGGER = logging.getLogger(__name__)


class Slot:

    def __init__(self, cntrl_type, slot_id, state, device_node):
        self.cntrl_type = cntrl_type
        self.slot = slot_id
        self.state = state.lower()
        self.device_node = None
        if device_node.startswith('/dev/'):
            self.device_node = device_node

    def __str__(self):
        return "slot: %s device: %s" % (self.slot, self.device_node)


class LedctlCmd:

    slot_mgmt_ctrls = ["SCSI", "VMD", "NPEM", "AMD"]

    # These base states should be supported by all controllers
    base_states = ["failure", "locate", "normal", "rebuild"]

    def __init__(self,
                 ledctl_bin=["none"],
                 slot_filters="none",
                 controller_filters="none"):
        # We cares about first entry (not full valdation but it is enough for test purposes)
        self.bin = ledctl_bin[0]

        # The purpose or slot filters is exclude unsupported but recognized slots, apply it
        # globally.
        self.slot_filters = slot_filters

        # Give possibility to skip controllers.
        self.slot_ctrls = [
            i for i in self.slot_mgmt_ctrls if i not in controller_filters
        ]

    def run_ledctl_cmd(self, params: list, output=False, check=True):
        params.insert(0, "sudo")
        params.insert(1, self.bin)

        LOGGER.debug(f"Command: {params}")
        # Without "shell=True" returncode and some output lines are omitted
        return subprocess.run(" ".join(params),
                              shell=True,
                              universal_newlines=True,
                              capture_output=output)

    # Run ledctl command and expect it to succeed
    def run_ledctl_cmd_valid(self, params: list):
        result = self.run_ledctl_cmd(params, output=True)
        if result.returncode != 0:
            raise Exception(
                "Command expected to succeed, but non 0 status was returned!")

        LOGGER.debug(f"Command returned:\n {result.stdout}")
        return result

    # Run ledctl command and expect it to fail
    def run_ledctl_cmd_not_valid(self, params: list):
        result = self.run_ledctl_cmd(params, output=True)
        if result.returncode == 0:
            raise Exception("Command succeed, but was expected to fail!")

        LOGGER.debug(f"Command returned:\n {result.stderr}")
        return result

    # Ledctl Commands

    def set_slot_state(self, slot: Slot, state):
        self.run_ledctl_cmd([
            "--set-slot", "--controller-type", slot.cntrl_type, "--slot",
            slot.slot, "--state", state
        ])

    def set_device_state(self, slot: Slot, state):
        self.run_ledctl_cmd([
            "--set-slot", "--controller-type", slot.cntrl_type, "--device",
            slot.device_node, "--state", state
        ])

    def get_slot(self, slot: Slot):
        out = self.run_ledctl_cmd_valid([
            "--get-slot", "--controller-type", slot.cntrl_type, "--slot",
            slot.slot
        ]).stdout
        return self.parse_slot_line(slot.cntrl_type, out)

    def get_slot_by_device(self, slot: Slot):
        out = self.run_ledctl_cmd_valid([
            "--get-slot", "--controller-type", slot.cntrl_type, "--device",
            slot.device_node
        ]).stdout
        return self.parse_slot_line(slot.cntrl_type, out)

    def get_slot_by_device_cntrl(self, dev_node, cntrl):
        # While using this method controllers may be not filtered out.
        # Do not return slot for controller removed from test.
        if cntrl not in self.slot_ctrls:
            return None

        out = self.run_ledctl_cmd_valid(
            ["--get-slot", "--controller-type", cntrl, "--device",
             dev_node]).stdout
        return self.parse_slot_line(cntrl, out)

    def default_controller_by_device(self, dev_node):
        result = self.run_ledctl_cmd_valid(
            ["--default-controller", "--device", dev_node]).stdout
        return result.rstrip()

    def list_slots(self, controller_type):
        rc = []
        out = self.run_ledctl_cmd_valid(
            ["--list-slots", "--controller-type", controller_type]).stdout

        for line in out.split("\n"):
            s = self.parse_slot_line(controller_type, line)
            if s is not None:
                rc.append(s)
        return rc

    def set_ibpi(self, dev_node, state):
        option = "%s=%s" % (state, dev_node)
        self.run_ledctl_cmd([option])

    def is_test_flag_enabled(self):
        try:
            self.run_ledctl_cmd(["-T"])
        except subprocess.CalledProcessError:
            raise AssertionError(
                "Test flag is disabled. Please add configure option \"--enable-test\"."
            )

    # Helper Functions

    def is_slot_excluded(self, slot: Slot):
        if self.slot_filters == "none":
            return False

        for slot_filter in self.slot_filters:
            if not slot_filter:
                return False
            if slot.slot.startswith(slot_filter):
                # Filter out this slot
                return True
        return False

    def get_controllers_with_slot_functionality(self):
        rc = {}

        out = self.run_ledctl_cmd_valid(["--list-controllers"]).stdout
        for raw_line in out.split("\n"):
            line = raw_line.strip()
            for ctrl in self.slot_ctrls:
                if ctrl in line:
                    rc[ctrl] = True
                    break
        return rc.keys()

    # Respect controller filter
    def get_slots(self, cntrl):
        if cntrl not in self.slot_ctrls:
            raise AssertionError(f"Controller \"{cntrl}\" filtered out")
        return self.list_slots(cntrl)

    # Respect controller filter
    def get_slots_with_device(self, cntrl):
        return [s for s in self.get_slots(cntrl) if s.device_node is not None]

    def get_all_slots(self):
        all_slots = []
        for controller in self.get_controllers_with_slot_functionality():
            all_slots.extend(self.list_slots(controller))
        return all_slots

    # Output parsers

    def parse_slot_line(self, controller, rawline):
        regex_pat = r"^slot: (.+)led state: (.+)device: (.*)$"
        slot_line_re = re.compile(regex_pat)

        line = rawline.strip()
        if len(line) == 0:
            return None

        match = slot_line_re.match(line)
        if match is None:
            raise Exception(
                f"Text line '{line}' did not match regex '{regex_pat}'")

        slot = Slot(controller,
                    match.group(1).strip(),
                    match.group(2).strip(),
                    match.group(3).strip())

        if self.is_slot_excluded(slot):
            return None
        return slot

    # Other

    def get_mp_nodes(self):
        sys_block_path = "/sys/block"
        nvme_subsys_subpath = "/nvme-subsystem/nvme-subsys"

        mp_drives = []
        for f in listdir(sys_block_path):
            rp = realpath(join(sys_block_path, f))
            if nvme_subsys_subpath in rp:
                node = join("/dev", f)
                if exists(node):
                    mp_drives.append(node)
        return mp_drives
