"""The A/B boot logic as it will run on the board: the real boot-ab.cmd (through the U-Boot
simulator) together with the real satlink-bootctl (with a fake fw_printenv / fw_setenv), sharing
one environment. The scenarios are those of the C reference model in tests/unit/boot/test_ab_slot.c.

@verifies SRS-BOOT-003
"""
import os
import pathlib
import shutil
import subprocess

import pytest

from tools.boot.ubootsim import ScriptError, UBootSim, load_env, save_env

ROOT = pathlib.Path(__file__).resolve().parents[3]
BOOT_AB = ROOT / "yocto/meta-satlink/recipes-bsp/satlink-boot-scr/files/boot-ab.cmd"
BOOTCTL = ROOT / "yocto/meta-satlink/recipes-support/satlink-bootctl/files/satlink-bootctl"

FAKE_PRINTENV = """#!/bin/sh
[ "$1" = "-n" ] && shift
grep -m1 "^$1=" "$ENVFILE" | cut -d= -f2-
"""

# Optionally fails after FAIL_AFTER successful writes, to model a power loss in between.
FAKE_SETENV = """#!/bin/sh
count_file="$ENVFILE.writes"
count=$(cat "$count_file" 2>/dev/null || echo 0)
if [ -n "${FAIL_AFTER:-}" ] && [ "$count" -ge "$FAIL_AFTER" ]; then
    exit 1
fi
echo $((count + 1)) > "$count_file"
{ grep -v "^$1=" "$ENVFILE" 2>/dev/null || true; echo "$1=$2"; } > "$ENVFILE.tmp"
mv "$ENVFILE.tmp" "$ENVFILE"
"""

needs_sh = pytest.mark.skipif(shutil.which("sh") is None, reason="a POSIX sh is required")


class Board:
    """A board with an SD card: the U-Boot environment file and the tools that touch it."""

    def __init__(self, tmp_path: pathlib.Path):
        self.envfile = tmp_path / "uboot.env"
        self.printenv = tmp_path / "fw_printenv.sh"
        self.setenv = tmp_path / "fw_setenv.sh"
        self.printenv.write_text(FAKE_PRINTENV)
        self.setenv.write_text(FAKE_SETENV)
        self.cmdline = tmp_path / "cmdline"
        self.cmdline.write_text("console=ttyPS0,115200 root=/dev/mmcblk0p2 rw\n")
        self.sim = UBootSim(self.envfile)
        self.script = BOOT_AB.read_text()

    def power_on(self):
        result = self.sim.run(self.script)
        self.cmdline.write_text(f"root=/dev/mmcblk0p{result.env.get('rootpart', '2')} "
                                f"{result.env.get('bootargs', '')}\n")
        return result

    @property
    def env(self):
        return load_env(self.envfile)

    def set(self, **values):
        env = self.env
        env.update({k: str(v) for k, v in values.items()})
        save_env(self.envfile, env)

    def bootctl(self, *args, healthy=True, fail_after=None):
        env = dict(os.environ)
        env.update(
            ENVFILE=self.envfile.as_posix(),
            FW_PRINTENV=f"sh {self.printenv.as_posix()}",
            FW_SETENV=f"sh {self.setenv.as_posix()}",
            CMDLINE_FILE=self.cmdline.as_posix(),
            SATLINK_HEALTH_CMD="true" if healthy else "false",
        )
        if fail_after is not None:
            env["FAIL_AFTER"] = str(fail_after)
        for leftover in (pathlib.Path(str(self.envfile) + ".writes"),):
            if leftover.exists():
                leftover.unlink()
        return subprocess.run(["sh", str(BOOTCTL), *args], env=env, capture_output=True, text=True)


@pytest.fixture
def board(tmp_path):
    return Board(tmp_path)


# ---- the boot script on its own -----------------------------------------------------------


def test_first_boot_creates_the_state_and_boots_slot_a(board):
    result = board.power_on()

    assert result.booted and result.booted_slot == "a"
    assert result.loaded == ["zImage.a", "system-a.dtb"]
    assert "root=/dev/mmcblk0p2" in result.env["bootargs"]
    assert "satlink.slot=a" in result.env["bootargs"]
    assert board.env["boot_slot"] == "a"
    assert board.env["prev_slot"] == "a"
    assert board.env["bootcount"] == "0"
    assert board.env["bootlimit"] == "3"
    assert board.env["upgrade_available"] == "0"


def test_only_what_saveenv_wrote_survives_a_power_cycle(board):
    board.power_on()
    saved = board.env

    # bootargs and rootpart are set after the last saveenv, so they are gone at the next power-on.
    assert "bootargs" not in saved and "rootpart" not in saved


def test_steady_state_boots_leave_the_state_alone(board):
    board.power_on()
    before = board.env
    for _ in range(10):
        assert board.power_on().booted_slot == "a"
    assert board.env == before


def test_the_script_uses_only_what_the_simulator_knows(board):
    # A guard for the simulator itself: an unsupported command must fail loudly, not be skipped.
    with pytest.raises(ScriptError):
        board.sim.run("mmc dev 0\n")


# ---- with satlink-bootctl ----------------------------------------------------------------


@needs_sh
def test_an_update_boots_the_other_slot_on_trial_and_confirms(board):
    board.power_on()
    assert board.bootctl("begin-update").returncode == 0

    assert board.env["boot_slot"] == "b"
    assert board.env["prev_slot"] == "a"
    assert board.env["upgrade_available"] == "1"
    assert board.env["bootcount"] == "0"

    result = board.power_on()
    assert result.booted_slot == "b"
    assert "root=/dev/mmcblk0p3" in result.env["bootargs"]
    assert board.env["bootcount"] == "1"

    assert board.bootctl("mark-good").returncode == 0
    assert board.env["upgrade_available"] == "0"
    assert board.env["prev_slot"] == "b"
    assert board.power_on().booted_slot == "b"
    assert board.env["bootcount"] == "0"


@needs_sh
@pytest.mark.parametrize("limit", [1, 2, 3, 4, 5])
def test_a_slot_that_never_confirms_is_tried_bootlimit_times_and_abandoned(board, limit):
    board.power_on()
    board.set(bootlimit=limit)
    board.bootctl("begin-update")

    trial_boots = 0
    fell_back = False
    for _ in range(20):
        result = board.power_on()
        if result.booted_slot == "b":
            trial_boots += 1
        else:
            fell_back = any("falling back to a" in line for line in result.log)
            assert result.booted_slot == "a"
            break

    assert trial_boots == limit
    assert fell_back
    assert board.env["boot_slot"] == "a"
    assert board.env["upgrade_available"] == "0"
    assert board.env["bootcount"] == "0"
    assert board.power_on().booted_slot == "a"  # and it stays there
    assert not any("falling back" in line for line in board.power_on().log)


@needs_sh
def test_confirming_on_the_last_allowed_attempt_keeps_the_new_slot(board):
    board.power_on()
    board.bootctl("begin-update")
    for _ in range(3):
        assert board.power_on().booted_slot == "b"
    assert board.bootctl("mark-good").returncode == 0
    assert board.power_on().booted_slot == "b"


@needs_sh
@pytest.mark.parametrize("limit", [1, 2, 3, 4])
def test_whenever_the_new_slot_is_confirmed_the_outcome_matches_the_model(board, limit):
    # The property from test_ab_slot.c: confirmed within bootlimit attempts -> stays; else abandoned.
    for confirm_on in range(0, limit + 3):
        board.envfile.unlink(missing_ok=True)
        board.power_on()
        board.set(bootlimit=limit)
        board.bootctl("begin-update")

        trial_boots = 0
        confirmed = False
        for _ in range(12):
            slot = board.power_on().booted_slot
            if slot == "b" and not confirmed:
                trial_boots += 1
                if trial_boots == confirm_on:
                    board.bootctl("mark-good")
                    confirmed = True

        if 1 <= confirm_on <= limit:
            assert confirmed and board.env["boot_slot"] == "b"
        else:
            assert not confirmed and trial_boots == limit and board.env["boot_slot"] == "a"


@needs_sh
def test_a_second_update_is_refused_while_the_first_is_on_trial(board):
    board.power_on()
    board.bootctl("begin-update")
    before = board.env

    refused = board.bootctl("begin-update")
    assert refused.returncode == 2
    assert "on trial" in refused.stderr
    assert board.env == before

    board.bootctl("mark-good")
    assert board.bootctl("begin-update").returncode == 0  # b -> a
    assert board.env["boot_slot"] == "a" and board.env["prev_slot"] == "b"


@needs_sh
def test_after_a_rollback_a_new_update_starts_from_the_good_slot(board):
    board.power_on()
    board.set(bootlimit=1)
    board.bootctl("begin-update")
    assert board.power_on().booted_slot == "b"
    assert board.power_on().booted_slot == "a"  # rolled back

    assert board.bootctl("begin-update").returncode == 0
    assert board.power_on().booted_slot == "b"


@needs_sh
def test_confirm_needs_a_healthy_system_and_is_a_no_op_when_not_on_trial(board):
    board.power_on()
    idle = board.bootctl("confirm")
    assert idle.returncode == 0 and "not on trial" in idle.stdout

    board.bootctl("begin-update")
    board.power_on()
    unhealthy = board.bootctl("confirm", healthy=False)
    assert unhealthy.returncode == 1
    assert board.env["upgrade_available"] == "1"

    assert board.bootctl("confirm", healthy=True).returncode == 0
    assert board.env["upgrade_available"] == "0"


@needs_sh
@pytest.mark.parametrize("writes_before_power_loss", [0, 1, 2, 3])
def test_a_power_loss_in_the_middle_of_begin_update_never_strands_the_board(
        board, writes_before_power_loss):
    board.power_on()

    interrupted = board.bootctl("begin-update", fail_after=writes_before_power_loss)
    assert interrupted.returncode != 0

    # The slot switch is the last write, so the old slot keeps booting; a stray trial flag
    # clears itself after bootlimit boots.
    for _ in range(8):
        assert board.power_on().booted_slot == "a"
    assert board.env["upgrade_available"] == "0"
    assert board.env["boot_slot"] == "a"


@needs_sh
def test_status_inactive_and_error_reporting(board):
    no_state = board.bootctl("status")
    assert no_state.returncode == 2 and "boot once" in no_state.stderr

    board.power_on()
    status = board.bootctl("status")
    assert status.returncode == 0
    assert "boot_slot=a" in status.stdout and "running_slot=a" in status.stdout

    inactive = board.bootctl("inactive")
    assert inactive.stdout.strip() == "slot=b rootpart=3 kernel=zImage.b dtb=system-b.dtb"

    assert board.bootctl("nonsense").returncode == 2
    assert board.bootctl().returncode == 2
