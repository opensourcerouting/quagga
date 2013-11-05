import logging
import subprocess
import sys

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
