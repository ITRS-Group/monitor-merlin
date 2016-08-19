#!/usr/bin/env python

import sys, os
sys.path.append(os.path.abspath('../../apps/libexec'))
import unittest
#from log import purge_naemon_log_files, archive_dir
import log
import tempfile
import shutil

class PurgeLogTestCase(unittest.TestCase):
	def setUp(self):
		# Create some files in temporary archive_dir
		log.archive_dir = "./TestPurgeLogs%s" % next(tempfile._get_candidate_names())
		os.mkdir(log.archive_dir)

		files = [
			"naemon.log-20150505",
			"naemon.log-20160824",
			"nagios-08-24-2016-14.log",
			"nagios-08-03-2015-14.log",
		]
		for file in files:
			with open(os.path.join(log.archive_dir, file), 'wb') as temp_file:
				temp_file.write('Testing file')

	def tearDown(self):
		# Remove the temporary archive_dir
		shutil.rmtree(log.archive_dir)

	def testPurge_log_files(self):
		import glob
		import time
		expected_result = [
			log.archive_dir + '/naemon.log-20160824',
			log.archive_dir + '/nagios-08-24-2016-14.log'
		]
		oldest = time.mktime(time.gmtime(1469048400)) #  2016-07-20T21:00:00+00:00
		log.purge_naemon_log_files(oldest)
		actual_result = glob.glob("%s/*" % log.archive_dir)

		# Compare number of files with the expected result
		self.assertEqual(expected_result, actual_result)

if __name__ == '__main__':
	unittest.main()
