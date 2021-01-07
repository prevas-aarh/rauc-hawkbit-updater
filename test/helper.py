import os
import subprocess
import shlex
import logging
import socket
from contextlib import closing


class PExpectLogger:
    """
    pexpect Logger, allows to use Python's logging stdlib. To be passed as pexpect 'logfile".
    Logs linewise to given logger at given level.
    """
    def __init__(self, level=logging.INFO, logger=None):
        self.level = level
        self.data = b''
        self.logger = logger if logger else logging.getLogger()

    def write(self, data):
        self.data += data

    def flush(self):
        for line in self.data.splitlines():
            self.logger.log(self.level, line.decode())

        self.data = b''


def run_pexpect(command, *, timeout=30, cwd=None):
    """
    Runs given command via pexpect with DBUS_STARTER_BUS_TYPE=session and PATH+=./build. Returns
    process handle immediately allowing further expect calls. Logs command (with updated env) and
    its stdout/stderr/exit code.
    """
    import pexpect

    logger = logging.getLogger(command.split()[0])

    env = os.environ.copy()
    env.update({'DBUS_STARTER_BUS_TYPE': 'session'})
    env['PATH'] += f':{os.path.dirname(os.path.abspath(__file__))}/../build'

    log_env = [ f'{key}={value}' for key, value in set(env.items()) - set(os.environ.items()) ]
    logger.info('running: %s %s', ' '.join(log_env), command)

    pexpect_log = PExpectLogger(logger=logger)
    return pexpect.spawn(command, env=env, timeout=timeout, cwd=cwd, logfile=pexpect_log)

def run(command, *, timeout=30):
    """
    Runs given command as subprocess with DBUS_STARTER_BUS_TYPE=session and PATH+=./build. Blocks
    until command terminates. Logs command (with updated env) and its stdout/stderr/exit code.
    Returns tuple (stdout, stderr, exit code).
    """
    env = os.environ.copy()
    env.update({'DBUS_STARTER_BUS_TYPE': 'session'})
    env['PATH'] += f':{os.path.dirname(os.path.abspath(__file__))}/../build'

    logger = logging.getLogger(command.split()[0])

    log_env = [ f'{key}={value}' for key, value in set(env.items()) - set(os.environ.items()) ]
    logger.info('running: %s %s', ' '.join(log_env), command)

    proc = subprocess.run(shlex.split(command), capture_output=True, text=True, check=False,
                          env=env, timeout=timeout)

    for line in proc.stdout.splitlines():
        if line:
            logger.info('stdout: %s', line)
    for line in proc.stderr.splitlines():
        if line:
            logger.warning('stderr: %s', line)

    logger.info('exitcode: %d', proc.returncode)

    return proc.stdout, proc.stderr, proc.returncode

def available_port():
    """Returns an available local port."""
    with closing(socket.socket(socket.AF_INET, socket.SOCK_STREAM)) as sock:
        sock.bind(('localhost', 0))
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        return sock.getsockname()[1]
