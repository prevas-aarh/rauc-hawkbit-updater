#!/usr/bin/env python3

import hashlib
import time
from pathlib import Path

from gi.repository import GLib
from pydbus.generic import signal


class Installer:
    """
    D-Bus interface `de.pengutronix.rauc.Installer`, to be used with
    `pydbus.{SessionBus,SystemBus}.publish()`
    """
    dbus = Path('../src/rauc-installer.xml').read_text()
    interface = 'de.pengutronix.rauc.Installer'

    Completed = signal()
    PropertiesChanged = signal()

    def __init__(self, bundle, completed_code=0):
        self._bundle = bundle
        self._completed_code = completed_code

        self._operation = 'idle'
        self._last_error = ''
        self._progress = 0, '', 1

    def Install(self, source):
        def mimic_install():
            """Mimics a sucessful/failing installation, depending on `self._completed_code`."""
            progresses = [
                'Installing',
                'Determining slot states',
                'Determining slot states done.',
                'Checking bundle',
                'Verifying signature',
                'Verifying signature done.',
                'Checking bundle done.',
                'Loading manifest file',
                'Loading manifest file done.',
                'Determining target install group',
                'Determining target install group done.',
                'Updating slots',
                'Checking slot rootfs.1',
                'Checking slot rootfs.1 done.',
                'Copying image to rootfs.1',
                'Copying image to rootfs.1 done.',
                'Updating slots done.',
                'Install failed.' if self._completed_code else 'Installing done.',
            ]

            self.Operation = 'installing'

            for i, progress in enumerate(progresses):
                percentage = (i+1)*100 / len(progresses)
                self.Progress = percentage, progress, 1
                time.sleep(0.1)

            self.Completed(self._completed_code)

            if not self._completed_code:
                self.LastError = 'Installation error'

            self.Operation = 'idle'

            # do not call again
            return False

        print(f'installing {source}')

        # check bundle checksum matches expected checksum (passed to constructor)
        assert self._get_bundle_sha1(source) == self._get_bundle_sha1(self._bundle)

        GLib.timeout_add_seconds(interval=1, function=mimic_install)

    @staticmethod
    def _get_bundle_sha1(bundle):
        """Calculates the SHA1 checksum of `self._bundle`."""
        sha1 = hashlib.sha1()

        with open(bundle, 'rb') as f:
            while True:
                chunk = f.read(sha1.block_size)
                if not chunk:
                    break
                sha1.update(chunk)

        return sha1.hexdigest()

    @property
    def Operation(self):
        return self._operation

    @Operation.setter
    def Operation(self, value):
        self._operation = value
        self.PropertiesChanged(Installer.interface, {'Operation': self.Operation}, [])

    @property
    def Progress(self):
        return self._progress

    @Progress.setter
    def Progress(self, value):
        self._progress = value
        self.PropertiesChanged(Installer.interface, {'Progress': self.Progress}, [])

    @property
    def LastError(self):
        return self._last_error

    @LastError.setter
    def LastError(self, value):
        self._last_error = value
        self.PropertiesChanged(Installer.interface, {'LastError': self.LastError}, [])


if __name__ == '__main__':
    import argparse
    from pydbus import SessionBus

    parser = argparse.ArgumentParser()
    parser.add_argument('bundle', help='Expected RAUC bundle')
    parser.add_argument('--completed-code', type=int, default=0,
                        help='Code to emit as D-Bus Completed signal')
    args = parser.parse_args()

    loop = GLib.MainLoop()
    bus = SessionBus()
    installer = Installer(args.bundle, args.completed_code)
    with bus.publish('de.pengutronix.rauc', ('/', installer)):
        print('Interface published')
        loop.run()
