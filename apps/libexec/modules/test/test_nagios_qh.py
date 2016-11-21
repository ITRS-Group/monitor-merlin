from nagios_qh import nagios_qh


def test_format_one_value():
    n = nagios_qh(None)
    result = list(n.format("test=1"))
    assert result == [{"test": "1"}], "Unexpected result %s" % result


def test_format_two_values_one_row():
    n = nagios_qh(None)
    result = list(n.format("test=1;test2=13"))
    assert result == [{"test": "1", "test2": "13"}], "Unexpected result %s" % result


def test_format_no_values_one_row():
    n = nagios_qh(None)
    result = list(n.format(""))
    assert result == [], "Unexpected result %s" % result


def test_format_two_values_two_rows():
    n = nagios_qh(None)
    result = list(n.format("test=1\ntest2=13"))
    assert result == [{"test": "1"}, {"test2": "13"}], "Unexpected result %s" % result


def test_format_three_values_two_rows():
    n = nagios_qh(None)
    result = list(n.format("test=1\ntest2=13;test3=42"))
    assert result == [{"test": "1"}, {"test2": "13", "test3": "42"}], "Unexpected result %s" % result


def test_format_two_values_three_rows():
    n = nagios_qh(None)
    result = list(n.format("test=1\n\ntest2=13"))
    assert result == [{"test": "1"}, {"test2": "13"}], "Unexpected result %s" % result
