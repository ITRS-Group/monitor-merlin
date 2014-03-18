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
    neb_header_fields = OrderedDict()
    neb_header_fields['type'] = 'int32'
    neb_header_fields['flags'] = 'int32'
    neb_header_fields['attr'] = 'int32'
    neb_header_fields['timestamp'] = 'Timeval'
    print ('import "header.proto";')
    print ('message NebCallbackHeader {')
    i = 1
    for ident, type in neb_header_fields.iteritems():
        print ('\toptional %s %s = %d;' % (type, ident, i))
        i += 1
    print ('};')
    print ('')

    for struct in get_structs(structdefs):
        print('message %s {' % snake2camel(struct.name))
        print('\toptional MerlinHeader header = 1;');
        print('\toptional NebCallbackHeader neb_header = 2;');
        i = 3
        for ident, type in struct.fields.iteritems():
            if ident in neb_header_fields:
                continue
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
