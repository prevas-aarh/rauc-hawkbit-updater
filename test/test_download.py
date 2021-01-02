from helper import run

def test_download_inexistent_location(hawkbit, bundle_assigned, adjust_config):
    """
    Assign bundle to target and test download to inexistent/unallowed locations specified in config.
    """
    location = '/tmp/does_not_exist/foo'
    config = adjust_config(
        {'client': {'bundle_download_location': location}}
    )
    out, err, exitcode = run(f'rauc-hawkbit-updater -c "{config}" -r')

    assert 'New software ready for download' in out
    # same warning from feedback() and from hawkbit_pull_cb()
    assert err == \
            f'WARNING: Failed to calculate free space for {location}: No such file or directory\n'*2
    assert exitcode == 1

    status = hawkbit.get_action_status()
    assert status[0]['type'] == 'error'
    assert f'Failed to calculate free space for {location}: No such file or directory' in \
            status[0]['messages']

def test_download_unallowed_location(hawkbit, bundle_assigned, adjust_config):
    """
    Assign bundle to target and test download to inexistent/unallowed locations specified in config.
    """
    location = '/root/foo'
    config = adjust_config(
        {'client': {'bundle_download_location': location}}
    )
    out, err, exitcode = run(f'rauc-hawkbit-updater -c "{config}" -r')

    assert 'Start downloading' in out
    assert err.strip() == \
            f'WARNING: Download failed: Failed to open {location} for download: Permission denied'
    assert exitcode == 1

    status = hawkbit.get_action_status()
    assert status[0]['type'] == 'error'
    assert f'Download failed: Failed to open {location} for download: Permission denied' in \
            status[0]['messages']

def test_download_too_slow(hawkbit, bundle_assigned, adjust_config, rate_limited_port):
    """Assign bundle to target and test too slow download of bundle."""
    # limit to 50 bytes/s
    port = rate_limited_port(50)
    config = adjust_config(
        {'client': {'hawkbit_server': f'{hawkbit.host}:{port}'}}
    )

    out, err, exitcode = run(f'rauc-hawkbit-updater -c "{config}" -r', timeout=90)

    assert 'Start downloading: ' in out
    assert err.strip() == 'WARNING: Download failed: Timeout was reached'
    assert exitcode == 1
