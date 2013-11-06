import logging
import shutil
import subprocess
import sys
import tempfile

log = logging.getLogger('generic')

def do_log(message):
    log.debug(message)


def call_log(*args, **kwargs):
    do_log('Calling: {0}'.format(' '.join(args)))
    subprocess.check_call(args, **kwargs)


class LogFile(object):
    """A file-like object that logs to the python log"""
    def __init__(self, prefix=None):
        if prefix is None:
            self.log = log
        else:
            self.log = logging.getLogger(prefix)

    def write(self, data):
        data = data.replace('\r', '')
        for line in data.splitlines():
            self.log.debug(line)

    def flush(self):
        pass

class TemporaryDir(object):
    def __init__(self, suffix="", prefix=None, dir=None):
        if prefix is None:
            prefix = tempfile.gettempprefix()
        if dir is None:
            dir = tempfile.gettempdir()

        self.rmtree = shutil.rmtree
        self.name = tempfile.mkdtemp(suffix, prefix, dir)

    def __enter__(self):
        if self.name is None:
            raise RuntimeError("TemporaryDir can't be reused. Create a new instance.")
        return self

    def __exit__(self, exc, value, tb):
        self.destroy()
        return False # Raise exceptions which occured

    def __del__(self):
        self.destroy()

    def destroy(self):
        if self.name is None:
            return

        self.rmtree(self.name)
        self.name = None
