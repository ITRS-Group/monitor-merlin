#!/usr/bin/env python

import os
import sys
import unittest

# needed for finding oconf
sys.path.append(os.path.abspath('apps/libexec'))

# needed for finding compound_config within oconf
sys.path.append(os.path.abspath('apps/libexec/modules'))
import oconf

class CmdPollerFix(unittest.TestCase):

	def test_comment_cfg_file(self):
		wanted_config_result = "cfg_file=oconf/from-master.cfg\n#cfg_file=bye.cfg\nanother_prop=1\n".splitlines()
		config_on_disk = "cfg_file=oconf/from-master.cfg\ncfg_file=bye.cfg\nanother_prop=1".splitlines()
		self.assertEqual(wanted_config_result, oconf.make_config_file_suitable_for_poller(config_on_disk))

	def test_add_from_master_directive(self):
		wanted_config_result = "#cfg_file=bye.cfg\nanother_prop=1\ncfg_file=oconf/from-master.cfg\n".splitlines()
		config_on_disk = "cfg_file=bye.cfg\nanother_prop=1".splitlines()
		self.assertEqual(wanted_config_result, oconf.make_config_file_suitable_for_poller(config_on_disk))

if __name__ == '__main__':
	unittest.main()
