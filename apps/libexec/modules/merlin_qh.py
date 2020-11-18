import nagios_qh


def get_merlin_nodeinfo(query_handler):
    def nodeinfo_sorter(a, b):
        if a["name"] > b["name"]:
            return 1
        if b["name"] > a["name"]:
            return -1
        return 0

    qh = nagios_qh.nagios_qh(query_handler)
    ninfo = []
    for info in qh.get("#merlin nodeinfo\0"):
        ninfo.append(info)
    ninfo.sort(nodeinfo_sorter)
    return ninfo


def get_expired(query_handler):
    def sort(a, b):
        res = cmp(a["responsible"], b["responsible"]) or cmp(
            a["responsible"], b["responsible"]
        )
        if res:
            return res
        if not a.get("service_description"):
            return -1
        if not b.get("service_description"):
            return 1
        return a["service_description"] > b["service_description"]

    qh = nagios_qh.nagios_qh(query_handler)
    expired = []
    for info in qh.get("#merlin expired\0"):
        expired.append(info)
    expired.sort(sort)
    return expired
