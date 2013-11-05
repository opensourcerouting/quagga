import collections
import re
import sys
import contextlib

import pexpect
import unittest

from . import generic

class spawn(object):
    def __init__(self, cmdline, timeout=5):
        self._process = pexpect.spawn(cmdline,
                                     timeout=timeout,
                                     logfile=generic.LogFile(cmdline)
                                    )

        self._ok_pattern = '32mOK'
        self._pass_information = {}
        self._failure_information = {}
        self._requirements = []

    def set_ok_pattern(self, pattern):
        self._ok_pattern = pattern

    def set_requirements(self, requirements):
        if not isinstance(requirements, collections.Iterable):
            requirements = [requirements]

        self._requirements = requirements

    @contextlib.contextmanager
    def _failure_recorder(self, identifier):
        try:
            yield
        except Exception:
            if identifier is not None:
                self._failure_information[identifier] = sys.exc_info()

    def _test_result(self, identifier):
        if identifier is None:
            return None

        if identifier in self._failure_information:
            return self._fail, identifier
        elif identifier in self._pass_information:
            return self._pass, identifier
        else:
            raise RuntimeError("Unknown test {0}".format(repr(identifier)))

    def _assert_unique_test_id(self, identifier):
        try:
            self._test_result(identifier)
        except Exception:
            return
        raise RuntimeError("A test has already been recorded with this name")

    def _check_requirements(self, identifier, requirements):
        if identifier is None:
            return True

        if not isinstance(requirements, collections.Iterable):
            requirements = [requirements]

        specimen = lambda : None
        for requirement in self._requirements + requirements:
            specimen = requirement(specimen)

        with self._failure_recorder(identifier):
            specimen()

        try:
            self._test_result(identifier)
        except Exception:
            return True
        return False

    def multiline_test(self, identifier, teststring=None, requirements=[]):
        if teststring is None:
            teststring = identifier

        if not self._check_requirements(identifier, requirements):
            return self._test_result(identifier)

        self.expect(re.escape(teststring))
        self.expect("\r\n\r\n")
        lines = self.before.splitlines()

        if len(lines) and self._ok_pattern in lines[-1]:
            return self.passed(identifier)
        else:
            return self.failed(identifier)

    def oneline_test(self, identifier, teststring=None, requirements=[]):
        if teststring is None:
            teststring = '{0}: '.format(identifier)

        if not self._check_requirements(identifier, requirements):
            return self._test_result(identifier)

        self.expect(re.escape(teststring))
        self.expect("\r\n")
        test_result = self.before

        if test_result and self._ok_pattern in test_result:
            return self.passed(identifier)
        else:
            return self.failed(identifier)

    def _fail(self, identifier):
        if identifier not in self._failure_information:
            raise RuntimeError("Couldn't find cause of failure")

        info = self._failure_information[identifier]
        raise info[0], info[1], info[2]

    def _pass(self, identifier):
        if identifier not in self._pass_information:
            raise RuntimeError("Pass of test was never recorded")

    def skip(self, identifier, reason):
        self._assert_unique_test_id(identifier)
        with self._failure_recorder(identifier):
            raise unittest.SkipTest(reason)
        return self._test_result(identifier)

    def failed(self, identifier):
        if identifier is None:
            return
        self._assert_unique_test_id(identifier)
        with self._failure_recorder(identifier):
            raise AssertionError("C programm asserted failure")
        return self._test_result(identifier)

    def passed(self, identifier):
        if identifier is None:
            return

        self._assert_unique_test_id(identifier)
        self._pass_information[identifier] = ':)'
        return self._test_result(identifier)

    def expect(self, pattern, identifier=None, timeout=-1, requirements=[]):
        if not self._check_requirements(identifier, requirements):
            return self._test_result(identifier)

        with self._failure_recorder(identifier):
            try:
                if self._process is not None:
                    self._process.expect(pattern, timeout=timeout)
                else:
                    raise RuntimeError("Program misbehaved before executing this test")
            except Exception:
                if self._process is not None:
                    self._process.close()
                    self._process = None
                    raise

        try:
            self._test_result(identifier)
        except Exception:
            # Pass the test if we didn't record an error
            self.passed(identifier)

        return self._test_result(identifier)

    @property
    def before(self):
        if self._process is not None:
            return self._process.before
        else:
            return ''

    def finish(self, requirements=[]):
        identifier = 'Program Termination'

        if self._check_requirements(identifier, requirements):
            # Wait for program termination and return with error if it doesn't finish
            rv = self.expect(pexpect.EOF, identifier, timeout=2)
            if identifier in self._failure_information:
                return rv
            del self._pass_information[identifier]

            # Check that exit status is 0
            self._process.isalive() # Updates exitstatus
            if self._process.exitstatus != 0:
                with self._failure_recorder(identifier):
                    raise AssertionError("Progam didn't exit with code 0 (rv==%r)" % self._process.exitstatus)
            else:
                self.passed(identifier)
        self._process = None

        return self._test_result(identifier)
