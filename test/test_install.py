from helper import run

def test_install_bundle_no_dbus_iface(hawkbit, bundle_assigned, config):
    """Assign bundle to target and test installation without RAUC D-Bus interface available."""
    out, err, exitcode = run(f'rauc-hawkbit-updater -c "{config}" -r')

    err_lines = err.splitlines()

    assert 'New software ready for download' in out
    assert 'Download complete' in out
    assert err_lines.pop(0) == \
            'WARNING: GDBus.Error:org.freedesktop.DBus.Error.ServiceUnknown: The name de.pengutronix.rauc was not provided by any .service files'
    assert err_lines.pop(0) == 'WARNING: Failed to install software bundle.'
    assert not err_lines
    assert exitcode == 1

    status = hawkbit.get_action_status()
    assert status[0]['type'] == 'error'

def test_install_success(hawkbit, config, bundle_assigned, rauc_dbus_install_success):
    """
    Assign bundle to target and test successful download and installation. Make sure installation
    result is received correctly by hawkBit.
    """
    out, err, exitcode = run(f'rauc-hawkbit-updater -c "{config}" -r')

    assert 'New software ready for download' in out
    assert 'Download complete' in out
    assert 'Software bundle installed successfully.' in out
    assert err == ''
    assert exitcode == 0

    status = hawkbit.get_action_status()
    assert status[0]['type'] == 'finished'

def test_install_failure(hawkbit, config, bundle_assigned, rauc_dbus_install_failure):
    """
    Assign bundle to target and test successful download and failing installation. Make sure
    installation result is received correctly by hawkBit.
    """
    out, err, exitcode = run(f'rauc-hawkbit-updater -c "{config}" -r')

    assert 'New software ready for download' in out
    assert err.strip() == 'WARNING: Failed to install software bundle.'
    assert exitcode == 1

    status = hawkbit.get_action_status()
    assert status[0]['type'] == 'error'
    assert 'Failed to install software bundle.' in status[0]['messages']
