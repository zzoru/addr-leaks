#!/usr/bin/python

import os
import subprocess
import sys
from optparse import OptionParser

from termcolor import colored


def count(output):
    return output.count('An address was leaked\n')

def main():
    parser = OptionParser()
    parser.add_option('-d', '--dumb',
               dest='dumb', default=False, action='store_true',
               help='dumb/full instrumentation')

    options, args = parser.parse_args()
    
    directory = '../AddrLeaks/tests/'

    files = ['direct1.bc', 'direct2.bc', 'direct3.bc', 'direct4.bc', 'direct5.bc',
             'indirect1.bc', 'indirect2.bc', 'indirect3.bc', 'indirect4.bc',
             'array1.bc', 'array2.bc', 'array3.bc', 'array4.bc',
             'struct1.bc', 'struct2.bc', 'struct3.bc',
             'context1.bc',
             'inter1.bc', 'inter2.bc',
             'strings1.bc', 'strings2.bc', 'strings3.bc', 'strings4.bc',
             'list1.bc', 'list2.bc',
             'flow1.bc', 'flow2.bc', 'flow3.bc'
             ]

    print '#### Regression tests ###\n'
    template = "{0:20}|{1:20}|{2:20}"
    print template.format("FILE", "INSTRUMENTATION", "RUNTIME")

    for filename in files:
        _file = open(directory + filename, 'rb')
        instrumented_file = open('instrumented.bc', 'wb')
        
        if options.dumb:
            p = subprocess.Popen(['opt', '-load', 
                                  '/home/gabriel/llvm/llvm-3.0.src/lib/Transforms/InstAddrLeaks/pass.so',
                                  '-c', '-d', '-leak'],
                                 shell=False, stdin=_file, stdout=instrumented_file,
                                 stderr=subprocess.PIPE)
        else:
            p = subprocess.Popen(['opt', '-load', 
                                  '/home/gabriel/llvm/llvm-3.0.src/lib/Transforms/InstAddrLeaks/pass.so',
                                  '-c', '-leak'],
                                 shell=False, stdin=_file, stdout=instrumented_file,
                                 stderr=subprocess.PIPE)

        p.communicate()

        instrumentation = True
        runtime = True

        if p.returncode != 0:
            instrumentation = False
            runtime = False

        if instrumentation:
            p = subprocess.Popen(['llvm-link', 'instrumented.bc', 'hash.bc', '-o', 'final.bc'],
                                 shell=False, stdin=None, stdout=None, stderr=None)
            p.communicate()

            p = subprocess.Popen(['lli', 'final.bc'],
                                 shell=False, stdin=None, stdout=subprocess.PIPE,
                                 stderr=subprocess.PIPE)
            result = p.communicate()[0]

            # TODO: improved this checking. Currently it's based on number of leaks detected.
            if filename == 'direct1.bc':
                if count(result) != 1: runtime = False
            elif filename == 'direct2.bc':
                if count(result) != 1: runtime = False
            elif filename == 'direct3.bc':
                if count(result) != 1: runtime = False
            elif filename == 'direct4.bc':
                if count(result) != 1: runtime = False
            elif filename == 'direct5.bc':
                if count(result) != 2: runtime = False
            elif filename == 'indirect1.bc':
                if count(result) != 1: runtime = False
            elif filename == 'indirect2.bc':
                if count(result) != 4: runtime = False
            elif filename == 'indirect3.bc':
                if count(result) != 3: runtime = False
            elif filename == 'indirect4.bc':
                if count(result) != 0: runtime = False
            elif filename == 'array1.bc':
                if count(result) != 3: runtime = False
            elif filename == 'array2.bc':
                if count(result) != 2: runtime = False
            elif filename == 'array3.bc':
                if count(result) != 1: runtime = False
            elif filename == 'array4.bc':
                if count(result) != 1: runtime = False
            elif filename == 'struct1.bc':
                if count(result) != 2: runtime = False
            elif filename == 'struct2.bc':
                if count(result) != 3: runtime = False
            elif filename == 'struct3.bc':
                if count(result) != 1: runtime = False
            elif filename == 'context1.bc':
                if count(result) != 1: runtime = False
            elif filename == 'inter1.bc':
                if count(result) != 1: runtime = False
            elif filename == 'inter2.bc':
                if count(result) != 4: runtime = False
            elif filename == 'strings1.bc':
                if count(result) != 2: runtime = False
            elif filename == 'strings2.bc':
                if count(result) != 1: runtime = False
            elif filename == 'strings3.bc':
                if count(result) != 0: runtime = False
            elif filename == 'strings4.bc':
                if count(result) != 0: runtime = False
            elif filename == 'list1.bc':
                if count(result) != 1: runtime = False
            elif filename == 'list2.bc':
                if count(result) != 0: runtime = False
            elif filename == 'flow1.bc':
                if count(result) != 1: runtime = False
            elif filename == 'flow2.bc':
                if count(result) != 1: runtime = False
            elif filename == 'flow3.bc':
                if count(result) != 1: runtime = False
        
        print template.format(filename, 
                              colored('\tPASSED', 'green') if instrumentation else colored('FAILED', 'red'), 
                              colored('\tPASSED', 'green') if runtime else colored('FAILED', 'red')) 

if __name__ == '__main__':
    main()

