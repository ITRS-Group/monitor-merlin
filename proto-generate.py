#!/usr/bin/env python2.7
import sys
import re
from collections import OrderedDict

type_map = {
        'int': 'int32',
        'struct timeval': 'Timeval',
        'char *': 'string',
        'double': 'double',
        'time_t': 'int64',
        'unsigned long': 'fixed64'
        }

class struct(object):
    def __init__(self):
        self.name = 'Not found'
        self.fields = OrderedDict()

    def add_field(self, ident, type):
        self.fields[ident] = type

def get_structs(filename):
    in_structdef = False
    structs = []
    a_struct = struct()
    with open(filename, 'r') as f:
        for line in f:
            if in_structdef:
                if line.strip().startswith('}'):
                    name = line.strip().split('}')[1].strip()[len('nebstruct_'):-1]
                    a_struct.name = name
                    structs.append(a_struct)
                    in_structdef = False
                elif len(line.strip()) > 0:
                    typestr = line.strip().split()[:-1]
                    ident = line.strip().split()[-1][:-1]
                    type = ' '.join(typestr)
                    if ident.startswith('*'):
                        type = type + ' *'
                        ident = ident[1:]
                    a_struct.add_field(ident, type)

            elif line.startswith('typedef struct nebstruct_'):
                a_struct = struct()
                in_structdef = True
            else:
                pass

    return structs


def snake2camel(str):
    comps = str.split('_')
    return ''.join([c.title() for c in comps])

def generate_proto(structdefs):
    print ('import "header.proto";')
    print ('message NebCallbackHeader {')
    print ('\toptional int32 flags = 1;')
    print ('\toptional int32 attr = 2;')
    print ('\toptional Timeval timestamp = 3;')
    print ('};')
    print ('')

    for struct in get_structs(structdefs):
        print('message %s {' % snake2camel(struct.name))
        print('\toptional MerlinHeader header = 1;');
        print('\toptional NebCallbackHeader neb_header = 2;');
        i = 3
        for ident, type in struct.fields.iteritems():
            try:
                mapped_type = type_map[type]
                print('\toptional %s %s = %d;' % ( mapped_type, ident, i))
            except KeyError:
                print >> sys.stderr, 'Ignoring unmappable type %s for field %s' % (type, ident)
            i = i+1

        print('};')
        print("")

if __name__ == '__main__':
    generate_proto(sys.argv[1])