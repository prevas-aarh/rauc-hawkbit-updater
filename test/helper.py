import os
import subprocess
import shlex
import logging


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
