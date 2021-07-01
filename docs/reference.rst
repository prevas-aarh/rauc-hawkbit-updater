.. _sec_ref:

Reference
=========

.. contents::
   :local:
   :depth: 1

.. _sec_ref_config_file:

Configuration File
------------------

Example configuration:

.. code-block:: cfg

  [client]
  hawkbit_server            = 127.0.0.1:8080
  ssl                       = false
  ssl_verify                = false
  tenant_id                 = DEFAULT
  target_name               = test-target
  auth_token                = bhVahL1Il1shie2aj2poojeChee6ahShu
  bundle_download_location  = /tmp/bundle.raucb

  [device]
  key1                      = valueA
  key2                      = valueB

**[client] section**

Configures how to connect to a hawkBit server, etc.

Mandatory options:

``hawkbit_server=<host>[:<port>]``
  The IP or hostname of the hawkbit server to connect to
  (Punycode representation must be used for host names containing Unicode
  characters).
  The ``port`` can be provided optionally, separated by a colon.

``target_name=<name>``
  Unique ``name`` string to identify controller.

``auth_token=<token>``
  Controller-specific authentication token.
  This is set for each device individually.
  For details, refer to https://www.eclipse.org/hawkbit/concepts/authentication/.

  .. note:: Either ``auth_token`` or ``gateway_token`` must be provided

``gateway_token=<token>``
  Gateway authentication token.
  This is a tenant-wide token and must explicitly be enabled in hakwBit first.
  It is actually meant to authenticate a gateway that itself
  manages/authenticates multiple targets, thus use with care.
  For details, refer to https://www.eclipse.org/hawkbit/concepts/authentication/.

  .. note:: Either ``auth_token`` or ``gateway_token`` must be provided

``bundle_download_location=<path>``
  Full path to where the bundle should be downloaded to.
  E.g. set to ``/tmp/_bundle.raucb`` to let rauc-hawkbit-updater use this
  location within ``/tmp``.

Optional options:

``tenant_id=<ID>``
  ID of the tenant to connect to. Defaults to ``DEFAULT``.

``ssl=<boolean>``
  Whether to use SSL connections (``https``) or not (``http``).
  Defaults to ``true``.

``ssl_verify=<boolean>``
  Whether to enforce SSL verification or not.
  Defaults to ``true``.

``connect_timeout=<seconds>``
  HTTP connection setup timeout [seconds].
  Defaults to ``20`` seconds.

``timeout=<seconds>``
  HTTP request timeout [seconds].
  Defaults to ``60`` seconds.

``retry_wait=<seconds>``
  Time to wait before retrying in case an error occurred [seconds].
  Defaults to ``60`` seconds.

``post_update_reboot=<boolean>``
  Whether to reboot the system after a successful update.
  Defaults to ``false``.

  .. important::
    Note that this results in an immediate reboot without contacting the system
    manager and without terminating any processes or unmounting any file systems.
    This may result in data loss.

``log_level=<level>``
  Log level to print, where ``level`` is a string of

  * ``debug``
  * ``info``
  * ``message``
  * ``critical``
  * ``error``
  * ``fatal``

  Defaults to ``message``.

.. _keyring-section:

**[device] section**

This section allows to set a custom list of key-value pairs that will be used
as config data target attribute for device registration.
They can be used for target filtering.

.. important::
  The [device] section is mandatory and at least one key-value pair must be
  configured.
