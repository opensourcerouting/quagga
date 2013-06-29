import os

uname = os.uname()

if uname[0] == 'Linux':
    from .linux import *
elif uname[0] == 'FreeBSD':
    from .freebsd import *
else:
    raise ImportError("Your system isn't supported for tests yet.")
