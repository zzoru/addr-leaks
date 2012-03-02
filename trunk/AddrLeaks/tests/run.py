#!/usr/bin/python

import os
import subprocess

from termcolor import colored


files = os.listdir('.')
files.sort()

print '#### Regression tests ###\n'
template = "{0:20}|{1:20}"
print template.format("FILE", "RESULT")

for filename in files:
    if filename.endswith('.bc'):
        _file = open(filename, 'rb')
        p = subprocess.Popen(['opt', '-load', '../../../../Release/lib/AddrLeaks.so',
                              '-instnamer', '-internalize', '-inline', '-globaldce', '-addrleaks'],
                             shell=False, stdin=_file, stdout=subprocess.PIPE,
                             stderr=subprocess.PIPE)
        result = p.communicate()[1]

        passed = True

        if filename == 'direct1.bc':
            if result.find('File: direct1.c, Line: 9') == -1: passed = False
        elif filename == 'direct2.bc':
            if result.find('File: direct2.c, Line: 9') == -1: passed = False
        elif filename == 'direct3.bc':
            if result.find('File: direct3.c, Line: 10') == -1: passed = False
        elif filename == 'direct4.bc':
            if result.find('File: direct4.c, Line: 11') == -1: passed = False
        elif filename == 'indirect1.bc':
            if result.find('File: indirect1.c, Line: 10') == -1: passed = False
            if result.find('File: indirect1.c, Line: 14') == -1: passed = False
        elif filename == 'indirect2.bc':
            if result.find('File: indirect2.c, Line: 17') != -1: passed = False
            if result.find('File: indirect2.c, Line: 18') != -1: passed = False
            if result.find('File: indirect2.c, Line: 19') != -1: passed = False
            if result.find('File: indirect2.c, Line: 20') != -1: passed = False
            if result.find('File: indirect2.c, Line: 24') == -1: passed = False
            if result.find('File: indirect2.c, Line: 25') == -1: passed = False
            if result.find('File: indirect2.c, Line: 26') == -1: passed = False
            if result.find('File: indirect2.c, Line: 27') == -1: passed = False
        elif filename == 'indirect3.bc':
            if result.find('File: indirect3.c, Line: 13') == -1: passed = False
            if result.find('File: indirect3.c, Line: 14') == -1: passed = False
            if result.find('File: indirect3.c, Line: 15') == -1: passed = False
        elif filename == 'indirect4.bc':
            if result.find('File: indirect4.c, Line: 12') != -1: passed = False
            if result.find('File: indirect4.c, Line: 13') != -1: passed = False
        elif filename == 'array1.bc':
            if result.find('File: array1.c, Line: 10') == -1: passed = False
            if result.find('File: array1.c, Line: 11') != -1: passed = False
            if result.find('File: array1.c, Line: 12') != -1: passed = False
            if result.find('File: array1.c, Line: 13') != -1: passed = False
            if result.find('File: array1.c, Line: 14') != -1: passed = False
            if result.find('File: array1.c, Line: 15') != -1: passed = False
            if result.find('File: array1.c, Line: 19') == -1: passed = False
            if result.find('File: array1.c, Line: 20') == -1: passed = False
            if result.find('File: array1.c, Line: 21') == -1: passed = False
            if result.find('File: array1.c, Line: 22') == -1: passed = False
            if result.find('File: array1.c, Line: 23') == -1: passed = False
            if result.find('File: array1.c, Line: 24') == -1: passed = False
        elif filename == 'array2.bc':
            if result.find('File: array2.c, Line: 12') != -1: passed = False
            if result.find('File: array2.c, Line: 16') == -1: passed = False
            if result.find('File: array2.c, Line: 17') == -1: passed = False
            if result.find('File: array2.c, Line: 18') == -1: passed = False
            if result.find('File: array2.c, Line: 19') == -1: passed = False
            if result.find('File: array2.c, Line: 20') == -1: passed = False
            if result.find('File: array2.c, Line: 21') == -1: passed = False
        elif filename == 'struct1.bc':
            if result.find('File: struct1.c, Line: 15') != -1: passed = False
            if result.find('File: struct1.c, Line: 16') != -1: passed = False
            if result.find('File: struct1.c, Line: 17') != -1: passed = False
            if result.find('File: struct1.c, Line: 23') != -1: passed = False
            if result.find('File: struct1.c, Line: 24') != -1: passed = False
            if result.find('File: struct1.c, Line: 25') == -1: passed = False
            if result.find('File: struct1.c, Line: 31') == -1: passed = False
            if result.find('File: struct1.c, Line: 32') != -1: passed = False
            if result.find('File: struct1.c, Line: 33') == -1: passed = False
        elif filename == 'struct2.bc':
            if result.find('File: struct2.c, Line: 20') != -1: passed = False
            if result.find('File: struct2.c, Line: 21') != -1: passed = False
            if result.find('File: struct2.c, Line: 22') != -1: passed = False
            if result.find('File: struct2.c, Line: 23') != -1: passed = False
            if result.find('File: struct2.c, Line: 30') != -1: passed = False
            if result.find('File: struct2.c, Line: 31') != -1: passed = False
            if result.find('File: struct2.c, Line: 32') == -1: passed = False
            if result.find('File: struct2.c, Line: 33') != -1: passed = False
            if result.find('File: struct2.c, Line: 40') == -1: passed = False
            if result.find('File: struct2.c, Line: 41') != -1: passed = False
            if result.find('File: struct2.c, Line: 42') == -1: passed = False
            if result.find('File: struct2.c, Line: 43') == -1: passed = False
        elif filename == 'context1.bc':
            passed = True
        elif filename == 'inter1.bc':
            if result.find('File: inter1.c, Line: 16') == -1: passed = False
        elif filename == 'inter2.bc':
            if result.find('File: inter2.c, Line: 27') == -1: passed = False
            if result.find('File: inter2.c, Line: 28') == -1: passed = False
            if result.find('File: inter2.c, Line: 29') == -1: passed = False
            if result.find('File: inter2.c, Line: 30') == -1: passed = False
        elif filename == 'strings1.bc':
            if result.find('File: strings1.c, Line: 12') == -1: passed = False
            if result.find('File: strings1.c, Line: 15') == -1: passed = False
        elif filename == 'list1.bc':
            passed = False
        elif filename == 'list2.bc':
            passed = False
        elif filename == 'flow1.bc':
            if result.find('File: flow1.c, Line: 10') != -1: passed = False
            if result.find('File: flow1.c, Line: 14') == -1: passed = False
        elif filename == 'flow2.bc':
            if result.find('File: flow2.c, Line: 9') != -1: passed = False
            if result.find('File: flow2.c, Line: 13') == -1: passed = False

        if passed:
            print template.format(filename, colored('\tPASSED', 'green'))
        else:
            print template.format(filename, colored('\tFAILED', 'red'))


