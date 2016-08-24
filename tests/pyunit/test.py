#!/usr/bin/env python

import sys
import os
import unittest
import tempfile
import shutil

sys.path.append(os.path.abspath('apps/libexec'))
import log

class PurgeLogTestMalformatCase(unittest.TestCase):
	def setUp(self):
		# Create some files in temporary archive_dir
		log.archive_dir = "./TestPurgeLogs%s" % next(tempfile._get_candidate_names())
		os.mkdir(log.archive_dir)

		files = [
			"baka.log-19900505",
			"kaka-08-24-2000-14.log",
			"nagios.something-313",
			"naemon.log-1337"
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
			log.archive_dir + "/baka.log-19900505",
			log.archive_dir + "/kaka-08-24-2000-14.log",
			log.archive_dir + "/nagios.something-313",
			log.archive_dir + "/naemon.log-1337",
		]
		oldest = time.mktime(time.gmtime(1469048400)) #  2016-07-20T21:00:00+00:00
		log.purge_naemon_log_files(oldest)
		actual_result = glob.glob("%s/*" % log.archive_dir)

		# Compare number of files with the expected result
		self.assertEqual(set(expected_result), set(actual_result))

class PurgeLogTestMoreThan10YearsOldCase(unittest.TestCase):
	def setUp(self):
		# Create some files in temporary archive_dir
		log.archive_dir = "./TestPurgeLogs%s" % next(tempfile._get_candidate_names())
		os.mkdir(log.archive_dir)

		files = [
			"naemon.log-19900505",
			"naemon.log-20000824",
			"nagios-08-24-2000-14.log",
			"nagios-07-21-1992-14.log",
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
		expected_result = []
		oldest = time.mktime(time.gmtime(1469048400)) #  2016-07-20T21:00:00+00:00
		log.purge_naemon_log_files(oldest)
		actual_result = glob.glob("%s/*" % log.archive_dir)

		# Compare number of files with the expected result
		self.assertEqual(set(expected_result), set(actual_result))

class PurgeLogTestCase(unittest.TestCase):
	def setUp(self):
		# Create some files in temporary archive_dir
		log.archive_dir = "./TestPurgeLogs%s" % next(tempfile._get_candidate_names())
		os.mkdir(log.archive_dir)

		files = [
			"naemon.log-20150505",
			"naemon.log-20160824",
			"naemon.log-20160721",
			"naemon.log-20160719",
			"nagios-08-24-2016-14.log",
			"nagios-07-21-2016-14.log",
			"nagios-07-19-2016-14.log",
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
			log.archive_dir + '/naemon.log-20160721',
			log.archive_dir + '/naemon.log-20160824',
			log.archive_dir + '/nagios-07-21-2016-14.log',
			log.archive_dir + '/nagios-08-24-2016-14.log'
		]
		oldest = time.mktime(time.gmtime(1469048400)) #  2016-07-20T21:00:00+00:00
		log.purge_naemon_log_files(oldest)
		actual_result = glob.glob("%s/*" % log.archive_dir)

		# Compare number of files with the expected result
		self.assertEqual(set(expected_result), set(actual_result))

if __name__ == '__main__':
	unittest.main()
