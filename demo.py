import datetime as dt
from dataclasses import dataclass
from typing import assert_type, Any
import numpy as np


def my_untyped_fn():
    return dt.timedelta(seconds=2)


py_dt = dt.datetime(year=2025, month=2, day=1)
np_dt = np.datetime64(py_dt)  # resolved as np.datetime64[dt.datetime]
np_td = np.timedelta64(my_untyped_fn())  # resolved as np.datetime64[Any]
np_td2: np.timedelta64 = np_td  # resolved as np.timedelta64

assert_type(np_dt, "np.datetime64[dt.datetime]")
assert_type(np_td, "np.timedelta64[Any]")
assert_type(np_td2, "np.timedelta64[dt.timedelta | int | None]")


@dataclass
class Measurement:
    timestamp: np.datetime64[dt.datetime]


Measurement(np_dt - np_td)  # ❌: got datetime64[int]
Measurement(np_dt - np_td2)  #  ❌: got datetime64[date | int | None]
