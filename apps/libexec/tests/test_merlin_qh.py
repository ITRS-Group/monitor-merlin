#!/usr/bin/env python3

import os
import sys
import unittest
from unittest.mock import patch
from unittest.mock import MagicMock

sys.path.append('modules')

from modules.merlin_qh import get_expired 

class merlin_qh_test(unittest.TestCase):
    @patch('nagios_qh.nagios_qh')  
    def test_get_expired(self, mock_nagios_qh):
        # Configure the mock to return a specific value if needed
        mock_nagios_qh.return_value = MagicMock()
        mock_nagios_qh.return_value.get.return_value = [
            {
                'host_name': 'example_host',
                'service_description': 'example_service02',
                'added': 'example_date',
                'responsible': 'peer02'
            },
            {
                'host_name': 'example_host',
                'service_description': 'example_service01',
                'added': 'example_date',
                'responsible': 'peer02'
            },
            {
                'host_name': 'example_host',
                'added': 'example_date',
                'responsible': 'peer02'
            },
            {
                'host_name': 'example_host',
                'added': 'example_date',
                'responsible': 'peer01'
            },
            {
                'host_name': 'example_host',
                'service_description': 'example_service03',
                'added': 'example_date',
                'responsible': 'peer01'
            },
        ]

        expected = [
            {
                'host_name': 'example_host',
                'added': 'example_date',
                'responsible': 'peer01'
            },
            {
                'host_name': 'example_host',
                'service_description': 'example_service03',
                'added': 'example_date',
                'responsible': 'peer01'
            },
            {
                'host_name': 'example_host',
                'added': 'example_date',
                'responsible': 'peer02'
            },
            {
                'host_name': 'example_host',
                'service_description': 'example_service01',
                'added': 'example_date',
                'responsible': 'peer02'
            },
            {
                'host_name': 'example_host',
                'service_description': 'example_service02',
                'added': 'example_date',
                'responsible': 'peer02'
            },
        ]

        # Call the function you're testing
        result = get_expired("mockedvalue")

        # Assert that nagios_qh.nagios_qh was called with the expected arguments
        mock_nagios_qh.assert_called_once_with('mockedvalue')  # Replace with the actual argument

        # Further assertions depending on what my_function does and what you expect
        self.assertEqual(result, expected)

if __name__ == '__main__':
    unittest.main()
