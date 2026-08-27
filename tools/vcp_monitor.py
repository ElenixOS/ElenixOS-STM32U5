#!/usr/bin/env python3
"""Small dependency-free ST-Link VCP monitor for the VS Code task runner."""

import glob
import os
import re
import select
import signal
import sys
import termios
import time


BAUD_RATE = termios.B115200
PID_FILE = "/tmp/elenixos-stlink-vcp-monitor.pid"
STOP_REQUESTED = False
LOG_LEVEL_COLORS = {
    "DEBUG": "\033[36m",  # cyan
    "INFO": "\033[32m",   # green
    "WARN": "\033[33m",   # yellow
    "ERROR": "\033[31m",  # red
}
LOG_LEVEL_PATTERN = re.compile(r"\[(DEBUG|INFO|WARN|ERROR)\]")


def log(message):
    print(message, flush=True)


def colorize_line(line):
    if os.environ.get("NO_COLOR") or not sys.stdout.isatty():
        return line

    match = LOG_LEVEL_PATTERN.search(line)
    if not match:
        return line

    color = LOG_LEVEL_COLORS[match.group(1)]
    content = line.rstrip("\r\n")
    line_ending = line[len(content):]
    return "{}{}\033[0m{}".format(color, content, line_ending)


def write_serial_text(text):
    if not text:
        return
    sys.stdout.write(colorize_line(text))
    sys.stdout.flush()


def read_pid():
    try:
        with open(PID_FILE, "r", encoding="ascii") as pid_file:
            return int(pid_file.read().strip())
    except (FileNotFoundError, ValueError, OSError):
        return None


def process_exists(pid):
    if pid is None or pid == os.getpid():
        return False
    try:
        os.kill(pid, 0)
    except ProcessLookupError:
        return False
    except PermissionError:
        return True
    return True


def stop_monitor():
    pid = read_pid()
    if process_exists(pid):
        try:
            os.kill(pid, signal.SIGTERM)
        except OSError:
            pass
    try:
        os.unlink(PID_FILE)
    except FileNotFoundError:
        pass
    return 0


def find_ports():
    configured_port = os.environ.get("STLINK_VCP_PORT")
    if configured_port:
        return [configured_port]

    patterns = (
        "/dev/cu.usbmodem*",
        "/dev/cu.usbserial*",
        "/dev/ttyACM*",
        "/dev/ttyUSB*",
    )
    ports = []
    for pattern in patterns:
        ports.extend(glob.glob(pattern))
    return sorted(set(ports))


def configure_port(fd):
    attributes = termios.tcgetattr(fd)
    attributes[0] = 0
    attributes[1] = 0
    attributes[2] = termios.CLOCAL | termios.CREAD | termios.CS8
    attributes[3] = 0
    attributes[4] = BAUD_RATE
    attributes[5] = BAUD_RATE
    termios.tcsetattr(fd, termios.TCSANOW, attributes)


def handle_stop(_signum, _frame):
    global STOP_REQUESTED
    STOP_REQUESTED = True


def monitor():
    global STOP_REQUESTED
    last_wait_report = 0.0

    previous_pid = read_pid()
    if process_exists(previous_pid):
        log("ST-Link VCP monitor is already running")
        return 0

    with open(PID_FILE, "w", encoding="ascii") as pid_file:
        pid_file.write(str(os.getpid()))

    signal.signal(signal.SIGTERM, handle_stop)
    signal.signal(signal.SIGINT, handle_stop)
    log("ST-Link VCP monitor started")

    try:
        while not STOP_REQUESTED:
            ports = find_ports()
            if not ports:
                now = time.monotonic()
                if now - last_wait_report >= 5.0:
                    log("Waiting for ST-Link VCP (set STLINK_VCP_PORT to override)")
                    last_wait_report = now
                time.sleep(1.0)
                continue

            port = ports[0]
            fd = None
            try:
                fd = os.open(port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
                configure_port(fd)
                log("Connected to {} at 115200 8N1".format(port))
                pending_text = ""

                while not STOP_REQUESTED:
                    readable, _, _ = select.select([fd], [], [], 1.0)
                    if not readable:
                        continue
                    data = os.read(fd, 4096)
                    if not data:
                        break
                    pending_text += data.decode("utf-8", errors="replace")
                    lines = pending_text.split("\n")
                    pending_text = lines.pop()
                    for line in lines:
                        write_serial_text(line + "\n")

                write_serial_text(pending_text)
            except (OSError, termios.error) as error:
                if not STOP_REQUESTED:
                    log("VCP disconnected: {}".format(error))
            finally:
                if fd is not None:
                    os.close(fd)

            if not STOP_REQUESTED:
                time.sleep(1.0)
    finally:
        try:
            if read_pid() == os.getpid():
                os.unlink(PID_FILE)
        except FileNotFoundError:
            pass

    return 0


if __name__ == "__main__":
    if sys.platform == "win32":
        print("ST-Link VCP monitor requires a POSIX serial device.", file=sys.stderr)
        sys.exit(2)
    if len(sys.argv) == 2 and sys.argv[1] == "--stop":
        sys.exit(stop_monitor())
    sys.exit(monitor())
