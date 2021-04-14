from configparser import ConfigParser

from helper import run

def test_version():
    """Test version argument."""
    out, err, exitcode = run('rauc-hawkbit-updater -v')

    assert exitcode == 0
    assert out.startswith('Version ')
    assert err == ''

def test_invalid_arg():
    """Test invalid argument."""
    out, err, exitcode = run('rauc-hawkbit-updater --invalidarg')

    assert exitcode == 1
    assert out == ''
    assert err.strip() == 'option parsing failed: Unknown option --invalidarg'

def test_config_file_non_existent():
    """Test call with inexistent config file."""
    out, err, exitcode = run('rauc-hawkbit-updater -c does-not-exist.conf')

    assert exitcode == 3
    assert out == ''
    assert err.strip() == 'No such configuration file: does-not-exist.conf'

def test_config_trailing_spaces(config):
    """Test if trailing spaces after config options are ignored."""
    # add trailing spaces to all lines in config
    with open(config) as f:
        lines = [line[:-1] + ' \n' for line in f.readlines()]
    with open(config, 'w') as f:
        f.writelines(lines)

    out, err, exitcode = run(f'rauc-hawkbit-updater -c "{config}" -r')

    assert exitcode == 0
    assert 'MESSAGE: Checking for new software...' in out
    assert err == ''

def test_config_no_auth_token(adjust_config):
    """Test config without auth_token option in client section."""
    config = adjust_config(remove={'client': 'auth_token'})

    out, err, exitcode = run(f'rauc-hawkbit-updater -c "{config}" -r')

    assert exitcode == 4
    assert out == ''
    assert err.strip() == \
            'Loading config file failed: Neither auth_token nor gateway_token is set in the config.'

def test_config_multiple_auth_methods(adjust_config):
    """Test config with auth_token and gateway_token options in client section."""
    config = adjust_config({'client': {'gateway_token': 'wrong-gateway-token'}})

    out, err, exitcode = run(f'rauc-hawkbit-updater -c "{config}" -r')

    assert exitcode == 4
    assert out == ''
    assert err.strip() == \
            'Loading config file failed: Both auth_token and gateway_token are set in the config.'

def test_register_and_check_invalid_gateway_token(adjust_config):
    """Test config with invalid gateway_token."""
    config = adjust_config(
            {'client': {'gateway_token': 'wrong-gateway-token'}},
            remove={'client': 'auth_token'}
    )

    out, err, exitcode = run(f'rauc-hawkbit-updater -c "{config}" -r')

    assert exitcode == 1
    assert 'MESSAGE: Checking for new software...' in out
    assert err.strip() == 'WARNING: Failed to authenticate. Check if gateway_token is correct?'

def test_register_and_check_valid_gateway_token(hawkbit, adjust_config):
    """Test config with valid gateway_token."""
    gateway_token = hawkbit.get_config('authentication.gatewaytoken.key')
    config = adjust_config(
            {'client': {'gateway_token': gateway_token}},
            remove={'client': 'auth_token'}
    )

    out, err, exitcode = run(f'rauc-hawkbit-updater -c "{config}" -r')

    assert exitcode == 0
    assert 'MESSAGE: Checking for new software...' in out
    assert err == ''

def test_register_and_check_invalid_auth_token(adjust_config):
    """Test config with invalid auth_token."""
    config = adjust_config({'client': {'auth_token': 'wrong-auth-token'}})

    out, err, exitcode = run(f'rauc-hawkbit-updater -c "{config}" -r')

    assert exitcode == 1
    assert 'MESSAGE: Checking for new software...' in out
    assert err.strip() == 'WARNING: Failed to authenticate. Check if auth_token is correct?'

def test_register_and_check_valid_auth_token(config):
    """Test config with valid auth_token."""
    out, err, exitcode = run(f'rauc-hawkbit-updater -c "{config}" -r')

    assert exitcode == 0
    assert 'MESSAGE: Checking for new software...' in out
    assert err == ''

def test_identify(hawkbit, config):
    """
    Test that supplying target meta information works and information are received correctly by
    hawkBit.
    """
    out, err, exitcode = run(f'rauc-hawkbit-updater -c "{config}" -r')

    assert exitcode == 0
    assert 'Providing meta information to hawkbit server' in out
    assert err == ''

    ref_config = ConfigParser()
    ref_config.read(config)

    assert dict(ref_config.items('device')) == hawkbit.get_attributes()
