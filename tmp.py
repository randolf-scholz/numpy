import datetime as dt
from typing import Protocol, Self, overload, reveal_type

import numpy as np

class Timedelta(Protocol):
    def __add__(self, other: Self, /) -> Self: ...
    def __radd__(self, other: Self, /) -> Self: ...
    def __sub__(self, other: Self, /) -> Self: ...
    def __rsub__(self, other: Self, /) -> Self: ...

class Timestamp[TD: Timedelta](Protocol):
    @overload
    def __sub__(self, other: Self, /) -> TD: ...
    @overload
    def __sub__(self, other: TD, /) -> Self: ...

class SupportsSubSelf[TD: Timedelta](Protocol):
    def __sub__(self, other: Self, /) -> TD: ...

class SupportsSubTD[TD: Timedelta](Protocol):
    def __sub__(self, other: TD, /) -> Self: ...

py_dt = dt.datetime(year=2025, month=1, day=31)
py_td = dt.timedelta(days=1)

np_dt = np.datetime64("2025-01-31")
np_dt_from_py = np.datetime64(py_dt)
np_td = np.timedelta64(1, 'D')
np_td_from_py = np.timedelta64(py_td)

reveal_type(np_dt)
reveal_type(np_dt_from_py)
reveal_type(np_td)
reveal_type(np_td_from_py)
#
reveal_type(np_dt - py_dt)
reveal_type(np_dt - np_dt)
reveal_type(np_dt - np_dt_from_py)
#
reveal_type(np_dt - py_td)
reveal_type(np_dt - np_td)
reveal_type(np_dt - np_td_from_py)
#
reveal_type(np_dt_from_py - py_dt)
reveal_type(np_dt_from_py - np_dt)
reveal_type(np_dt_from_py - np_dt_from_py)
#
reveal_type(np_dt_from_py - py_td)
reveal_type(np_dt_from_py - np_td)
reveal_type(np_dt_from_py - np_td_from_py)




# foo1: SupportsSubTD = py_dt  # ✅
# bar1: SupportsSubTD = np.datetime64(py_dt)  # ✅
# foo2: SupportsSubSelf = py_dt  # ✅
# bar2: SupportsSubSelf = np.datetime64(py_dt)  # ✅
# foo3: Timestamp = py_dt  # ✅
# bar3: Timestamp = np.datetime64(py_dt)  # ❌


