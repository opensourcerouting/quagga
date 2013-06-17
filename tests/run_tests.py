import unittest

class MyTestProgram(unittest.TestProgram):
    def createTests(self):
        self.test = self.testLoader.discover('testcases', pattern='*.py')

if __name__ == '__main__':
    MyTestProgram()
