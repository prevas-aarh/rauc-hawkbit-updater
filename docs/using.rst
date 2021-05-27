Using the RAUC hawkbit Updater
==============================

Authentication via tokens
-------------------------

As described on the `hawkBit Authentication page <https://www.eclipse.org/hawkbit/concepts/authentication/>`_
in the "DDI API Authentication Modes" section, a device can be authenticated
with a security token. A security token can be either a "Target" token or a
"Gateway" token. The "Target" security token is specific to a single target
defined in hawkBit. In the RAUC hawkBit updater's configuration file it's
referred to as ``auth_token``.

Targets can also be connected through a gateway which manages the targets
directly and as a result these targets are indirectly connected to the hawkBit
update server. The "Gateway" token is used to authenticate this gateway and
allow it to manage all the targets under its tenant. With RAUC hawkBit updater
such token can be used to authenticate all targets on the server. I.e. same
gateway token can be used in a configuration file replicated on many targets.
In the RAUC hawkBit updater's configuration file it's called ``gateway_token``.
Although gateway token is very handy during development or testing, it's
recommended to use this token with care because it can be used to
authenticate any device.

Authentication via Certificates
-------------------------------

As can be seen in the system configuration settings of hawkBit, there is a
third option to authenticate the targets. An "Allow targets to authenticate via
a certificate authenticated by a reverse proxy" option. To use this
authentication method a TLS reverse proxy server needs to be set up.
The client and reverse proxy server perform a "SSL-handshake" that means the
client validates the server certificate of the reverse proxy server with its
list of trusted certificates.

The clients request has:

        - to have a TLS connection to the reverse proxy server
        - to contain the client certificate
        - to have the common name of the server certificate match the server
          name set in the configuration file as "hawkbit_server"

The purpose of the reverse proxy is to:

        - disband the TLS connection
        - check if sent client certificate is valid
        - extract the common name and fingerprint of the client certificate
        - forward the common name and fingerprint as HTTP headers to the
          hawkBit server

When the hawkBit server receives the request it checks if:

        - sent common name matches with the controller ID of the target
        - sent fingerprint(s) matches the expected fingerprint(s) which is set
          in the system configuration settings of hawkBit

The client certificate will only be used if no tokens are set and a valid path
to a certificate and its key is given in the configuration file.

Here an example of how the configuration file might look like:

[client]
hawkbit_server            = CN_server_certificate:443
ssl                       = true
ssl_verify                = true
tenant_id                 = DEFAULT
target_name               = test-target
auth_token                =
#gateway_token            = bhVahL1Il1shie2aj2poojeChee6ahShu
#client_cert              = /path/to/client_certificate.pem
#client_key               = /path/to/client_certificate.key
bundle_download_location  = /tmp/bundle.raucb
retry_wait                = 60
connect_timeout           = 20
timeout                   = 60
log_level                 = debug
post_update_reboot        = false

[device]
product                   = Terminator
model                     = T-1000
serialnumber              = 8922673153
hw_revision               = 2
key1                      = value
key2                      = value
