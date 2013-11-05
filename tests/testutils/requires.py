import os
import unittest

from . import quagga_constants
from . import system

class _Requirement(object):
    """
    Allows to express that a test depends on a specific
    feature or precondition and should otherwise be skipped.

    An instance of this class can be used as a decorator
    much like the unittest.skip decorator. It will skip
    the test with the given 'message' if the condition
    is not 'satisfied'.
    Additionally, the instance has a 'satisfied' property
    that may be used for checks.
    """
    def __init__(self, satisfied, message):
        self.satisfied = satisfied
        self.message = message

    def __call__(self, func):
        if self.satisfied:
            return func
        else:
            return unittest.skip(self.message)(func)

bgpd = _Requirement(
        quagga_constants.BGPD != "",
        'Test requires bgpd'
)

ipv6 = _Requirement(
        'HAVE_IPV6' in dir(quagga_constants),
        'Test requires IPv6'
)

multipath = _Requirement(
        system.SUPPORTS_MULTIPATH and quagga_constants.MULTIPATH_NUM != 1,
        'Test requires multipath support'
)

prefsrc = _Requirement(
        system.SUPPORTS_PREFSRC,
        'Test requires prefsrc support'
)

root = _Requirement(
        os.getuid() == 0,
        'Test requires root privileges'
)

#def satisfied(func):
#    return func
#
#def _define_requirement(name, condition, message):
#    if condition:
#        globals()[name] = satisfied
#    else:
#        globals()[name] = unittest.skip(message)
#
#_define_requirement(
#        'bgpd',
#        quagga_constants.BGPD != ""
#        'Test requires bgpd'
#)
#
#_define_requirement(
#        'ipv6',
#        'HAVE_IPV6' in dir(quagga_constants),
#        'Test requires IPv6'
#)
#
#_define_requirement(
#        'root',
#        os.getuid() == 0,
#        'Test requires root privileges'
#)
#
#_define_requirement(
#        'multipath',
#        system.SUPPORTS_MULTIPATH and quagga_constants.MULTIPATH_NUM != 1,
#        'Test requires multipath support'
#)
#
#_define_requirement(
#        'prefsrc',
#        system.SUPPORTS_PREFSRC,
#        'Test requires prefsrc support'
#)
