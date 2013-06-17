import os

uname = os.uname()

if uname[0] == 'Linux':
    from .linux import *
else:
    raise ImportError("Your system isn't supported for tests yet.")
