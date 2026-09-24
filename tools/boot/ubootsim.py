"""A small simulator of the U-Boot shell commands used by yocto boot scripts.

It runs the real script text (boot-ab.cmd) against an environment stored in a file, so the
boot-slot decision logic can be tested without a board and shares its state with the
satlink-bootctl tests. It knows only the handful of commands the scripts use and refuses anything
else, so a new command has to be added here on purpose. What it reproduces from U-Boot:

  - the environment survives only what saveenv wrote; a plain setenv is gone after the run
  - numbers in "test" are decimal (or 0x hex), numbers given to setexpr are hex and its result
    is printed as hex
  - "${name}" expands to the variable's value or to the empty string

@implements SRS-BOOT-003
"""
import dataclasses
import pathlib
import re
import shlex
from typing import Dict, List, Optional


class ScriptError(Exception):
    """The script used something the simulator does not implement, or failed."""


def load_env(path: pathlib.Path) -> Dict[str, str]:
    env: Dict[str, str] = {}
    if path.exists():
        for line in path.read_text().splitlines():
            if "=" in line:
                name, value = line.split("=", 1)
                env[name] = value
    return env


def save_env(path: pathlib.Path, env: Dict[str, str]) -> None:
    path.write_text("".join(f"{name}={value}\n" for name, value in sorted(env.items())))


@dataclasses.dataclass
class RunResult:
    env: Dict[str, str]  # environment at the end of the run, saved or not
    log: List[str]       # echo output
    loaded: List[str]    # file names given to fatload, in order
    booted: bool         # bootz was reached

    @property
    def booted_slot(self) -> Optional[str]:
        """The slot whose kernel was loaded, from the zImage.<slot> file name."""
        for name in self.loaded:
            match = re.fullmatch(r"zImage\.([ab])", name)
            if match:
                return match.group(1)
        return None


def _number(text: str, base: int) -> int:
    try:
        return int(text, base)
    except ValueError:
        return 0  # U-Boot's strtoul returns 0 for junk


def _test(args: List[str]) -> bool:
    if len(args) == 2 and args[0] == "-z":
        return args[1] == ""
    if len(args) == 2 and args[0] == "-n":
        return args[1] != ""
    if len(args) == 3 and args[1] == "=":
        return args[0] == args[2]
    if len(args) == 3 and args[1] == "!=":
        return args[0] != args[2]
    if len(args) == 3 and args[1] in ("-ge", "-gt", "-le", "-lt", "-eq", "-ne"):
        a, b = _number(args[0], 0), _number(args[2], 0)
        return {"-ge": a >= b, "-gt": a > b, "-le": a <= b, "-lt": a < b,
                "-eq": a == b, "-ne": a != b}[args[1]]
    raise ScriptError(f"unsupported test expression: {args}")


class UBootSim:
    def __init__(self, env_file: pathlib.Path, defaults: Optional[Dict[str, str]] = None):
        self.env_file = env_file
        self.defaults = {"kernel_addr_r": "0x2000000", "fdt_addr_r": "0x1f00000"}
        self.defaults.update(defaults or {})

    def run(self, script: str) -> RunResult:
        env = dict(self.defaults)
        env.update(load_env(self.env_file))
        result = RunResult(env=env, log=[], loaded=[], booted=False)
        lines = [ln.strip() for ln in script.splitlines()]
        lines = [ln for ln in lines if ln and not ln.startswith("#")]
        self._execute(lines, result)
        return result

    def _expand(self, token: str, env: Dict[str, str]) -> str:
        return re.sub(r"\$\{(\w+)\}", lambda m: env.get(m.group(1), ""), token)

    def _words(self, line: str, env: Dict[str, str]) -> List[str]:
        return [self._expand(word, env) for word in shlex.split(line)]

    def _execute(self, lines: List[str], result: RunResult) -> None:
        env = result.env
        # Each open "if" is (this branch is running, the condition was true).
        stack: List[List[bool]] = []

        def active() -> bool:
            return all(frame[0] for frame in stack)

        for line in lines:
            if line.startswith("if ") and line.endswith("; then"):
                condition = line[3:-len("; then")]
                words = self._words(condition, env)
                if not words or words[0] != "test":
                    raise ScriptError(f"only 'if test ...' is supported: {line}")
                take = active() and _test(words[1:])
                stack.append([take, take])
            elif line == "else":
                frame = stack[-1]
                parent_active = all(f[0] for f in stack[:-1])
                frame[0] = parent_active and not frame[1]
            elif line == "fi":
                stack.pop()
            elif not active():
                continue
            else:
                self._command(line, result)
        if stack:
            raise ScriptError("unterminated if")

    def _command(self, line: str, result: RunResult) -> None:
        env = result.env
        words = self._words(line, env)
        name, args = words[0], words[1:]

        if name == "setenv":
            if len(args) == 1:
                env.pop(args[0], None)
            else:
                env[args[0]] = " ".join(args[1:])
        elif name == "setexpr":
            target, a, op, b = args
            x, y = int(a, 16), int(b, 16)
            value = {"+": x + y, "-": x - y, "*": x * y, "/": x // y if y else 0}[op]
            env[target] = f"{value:x}"
        elif name == "saveenv":
            save_env(self.env_file, env)
        elif name == "echo":
            result.log.append(" ".join(args))
        elif name == "fatload":
            result.loaded.append(args[-1])
        elif name == "bootz":
            result.booted = True
        else:
            raise ScriptError(f"unsupported command: {line}")
