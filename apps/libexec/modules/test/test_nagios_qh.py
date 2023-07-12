from modules.nagios_qh import nagios_qh


def test_format_one_block_one_row_one_value():
    n = nagios_qh(None)
    result = list(n.format(["test=1"]))
    assert result == [{"test": "1"}], "Unexpected result %s" % result


def test_format_one_block_one_row_two_values():
    n = nagios_qh(None)
    result = list(n.format(["test=1;test2=13"]))
    assert result == [{"test": "1", "test2": "13"}], "Unexpected result %s" % result


def test_format_no_block():
    n = nagios_qh(None)
    result = list(n.format([]))
    assert result == [], "Unexpected result %s" % result


def test_format_one_block_two_rows_two_values():
    n = nagios_qh(None)
    result = list(n.format(["test=1\ntest2=13"]))
    assert result == [{"test": "1"}, {"test2": "13"}], "Unexpected result %s" % result


def test_format_one_block_two_rows_three_values():
    n = nagios_qh(None)
    result = list(n.format(["test=1\ntest2=13;test3=42"]))
    assert result == [{"test": "1"}, {"test2": "13", "test3": "42"}], "Unexpected result %s" % result


def test_format_one_block_three_rows_two_values():
    n = nagios_qh(None)
    result = list(n.format(["test=1\n\ntest2=13"]))
    assert result == [{"test": "1"}, {"test2": "13"}], "Unexpected result %s" % result


def test_format_two_blocks_one_row_one_value():
    n = nagios_qh(None)
    result = list(n.format(["test", "=1"]))
    assert result == [{"test": "1"}], "Unexpected result %s" % result


def test_format_two_blocks_one_row_two_values():
    n = nagios_qh(None)
    result = list(n.format(["test", "=1", ";test2=2"]))
    assert result == [{"test": "1", "test2": "2"}], "Unexpected result %s" % result


def test_format_two_blocks_two_rows_two_values():
    n = nagios_qh(None)
    result = list(n.format(["test=1", "\ntest2=2"]))
    assert result == [{"test": "1"}, {"test2": "2"}], "Unexpected result %s" % result
