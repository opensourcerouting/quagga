import logging
import subprocess
import sys

log = logging.getLogger(__name__)

def do_log(message):
    # sys.stderr.write('{0}\n'.format(message))
    log.debug(message)

def call_log(*args, **kwargs):
    do_log('#{0}'.format(' '.join(args)))
    subprocess.check_call(args, **kwargs)
