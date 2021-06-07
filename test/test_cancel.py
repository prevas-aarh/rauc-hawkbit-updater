# SPDX-License-Identifier: LGPL-2.1-only
# SPDX-FileCopyrightText: 2021 Bastian Krause <bst@pengutronix.de>, Pengutronix

from pexpect import TIMEOUT

from helper import run, run_pexpect

def test_cancel_before_poll(hawkbit, config, bundle_assigned, rauc_dbus_install_success):
    """
    Assign distribution containing bundle to target and cancel it right away. Then run
    rauc-hawkbit-updater and make sure it acknowledges the not yet processed action.
    """
    hawkbit.cancel_action()

    out, err, exitcode = run(f'rauc-hawkbit-updater -c "{config}" -r')

    assert f'Received cancelation for unprocessed action {hawkbit.id["action"]}, acknowledging.' \
            in out
    assert 'Action canceled.' in out
    assert err == ''
    assert exitcode == 0

    cancel = hawkbit.get_action()
    assert cancel['type'] == 'cancel'
    assert cancel['status'] == 'finished'

    cancel_status = hawkbit.get_action_status()
    assert cancel_status[0]['type'] == 'canceled'
    assert 'Action canceled.' in cancel_status[0]['messages']

def test_cancel_during_download(hawkbit, adjust_config, bundle_assigned, rate_limited_port):
    """
    Assign distribution containing bundle to target. Run rauc-hawkbit-updater configured to
    comminucate via rate-limited proxy with hawkBit. Cancel the assignment once the download
    started and make sure the cancelation is acknowledged and no installation is started.
    """
    port = rate_limited_port('70k')
    config = adjust_config({'client': {'hawkbit_server': f'{hawkbit.host}:{port}'}})

    # note: we cannot use -r here since that prevents further polling of the base resource
    # announcing the cancelation
    proc = run_pexpect(f'rauc-hawkbit-updater -c "{config}"')
    proc.expect('Start downloading: ')
    proc.expect(TIMEOUT, timeout=1)

    # assuming:
    # - rauc-hawkbit-updater polls base resource every 5 s for cancelations during download
    # - download of 512 KB bundle @ 70 KB/s takes ~7.3 s
    # -> cancelation should be received and processed before download finishes
    hawkbit.cancel_action()

    # do not wait longer than 5 s (poll interval) + 3 s (processing margin)
    proc.expect(f'Received cancelation for action {hawkbit.id["action"]}', timeout=8)
    proc.expect('Action canceled.')
    # wait for feedback to arrive at hawkbit server
    proc.expect(TIMEOUT, timeout=2)
    proc.terminate(force=True)

    cancel = hawkbit.get_action()
    assert cancel['type'] == 'cancel'
    assert cancel['status'] == 'finished'

    cancel_status = hawkbit.get_action_status()
    assert cancel_status[0]['type'] == 'canceled'
    assert 'Action canceled.' in cancel_status[0]['messages']

def test_cancel_during_install(hawkbit, config, bundle_assigned, rauc_dbus_install_success):
    """
    Assign distribution containing bundle to target. Run rauc-hawkbit-updater and cancel the
    assignment once the installation started. Make sure the cancelation does not disrupt the
    installation.
    """
    proc = run_pexpect(f'rauc-hawkbit-updater -c "{config}"')
    proc.expect('MESSAGE: Installing: ')

    hawkbit.cancel_action()

    # wait for installation to finish
    proc.expect('Software bundle installed successfully.')
    # wait for feedback to arrive at hawkbit server
    proc.expect(TIMEOUT, timeout=2)
    proc.terminate(force=True)

    cancel = hawkbit.get_action()
    assert cancel['type'] == 'update'
    assert cancel['status'] == 'finished'

    cancel_status = hawkbit.get_action_status()
    assert cancel_status[0]['type'] == 'finished'
    assert 'Software bundle installed successfully.' in cancel_status[0]['messages']
