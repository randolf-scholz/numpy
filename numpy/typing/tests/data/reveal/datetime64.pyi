# mypy: enable-error-code="unused-ignore"

import datetime as dt
from typing import Protocol, Self, assert_type, overload, TypeVar, Generic, Any
import numpy as np

import pytest

def untyped_dt() -> Any:
    return dt.datetime(year=2025, month=2, day=1)

def untyped_td() -> Any:
    return dt.timedelta(seconds=2)



py_date = dt.date(year=2025, month=1, day=31)
py_dt = dt.datetime(year=2025, month=1, day=31, hour=1, minute=23, second=45)
py_td = dt.timedelta(seconds=37)

dt64_date = np.datetime64(py_date)
dt64_dt = np.datetime64(py_dt)
dt64_int = np.datetime64(100, "ns")
dt64_nat = np.datetime64(None)
dt64_any = np.datetime64(untyped_dt())
dt64: np.datetime64 = np.datetime64(untyped_dt())

td64_td = np.timedelta64(py_td)
td64_int = np.timedelta64(100, "ns")
td64_nat = np.timedelta64(None)
td64_any = np.timedelta64(untyped_td())
td64: np.timedelta64 = np.timedelta64(untyped_td())

# static checks
assert_type(py_date, dt.date)
assert_type(py_dt, dt.datetime)
assert_type(py_td, dt.timedelta)
# np_datetime64
assert_type(dt64, "np.datetime64")
assert_type(dt64_any, "np.datetime64[Any]")
assert_type(dt64_date, "np.datetime64[dt.date]")
assert_type(dt64_dt, "np.datetime64[dt.datetime]")
assert_type(dt64_int, "np.datetime64[int]")
assert_type(dt64_nat, "np.datetime64[None]")
# np_timedelta64
assert_type(td64, "np.timedelta64")
assert_type(td64_any, "np.timedelta64[Any]")
assert_type(td64_int, "np.timedelta64[int]")
assert_type(td64_nat, "np.timedelta64[None]")
assert_type(td64_td, "np.timedelta64[dt.timedelta]")

# ----------- runtime checks -------------
# fmt: off
# py_date
assert type(py_date - py_td) is dt.date
assert type(py_date - py_date) is dt.timedelta
# py_dt
assert type(py_dt - py_td) is dt.datetime
assert type(py_dt - py_dt) is dt.timedelta

# datetime64[dt.date]
assert type(dt64_date - py_date) is dt.timedelta
with pytest.raises(TypeError):  dt64_date - py_dt  # type: ignore[operator, unused-ignore]
assert type(dt64_date - py_td) is dt.date

assert type(dt64_date - dt64) is np.timedelta64
assert type(dt64_date - dt64_any) is np.timedelta64
assert type(dt64_date - dt64_date) is np.timedelta64
assert type(dt64_date - dt64_dt) is np.timedelta64
assert type(dt64_date - dt64_int) is np.timedelta64
assert type(dt64_date - dt64_nat) is np.timedelta64

assert type(dt64_date - td64) is np.datetime64
assert type(dt64_date - td64_any) is np.datetime64
assert type(dt64_date - td64_int) is np.datetime64
assert type(dt64_date - td64_nat) is np.datetime64
assert type(dt64_date - td64_td) is np.datetime64

# datetime64[dt.datetime]
with pytest.raises(TypeError): dt64_dt - py_date  # type: ignore[operator]
assert type(dt64_dt - py_dt) is dt.timedelta
assert type(dt64_dt - py_td) is dt.datetime

assert type(dt64_dt - dt64) is np.timedelta64
assert type(dt64_dt - dt64_any) is np.timedelta64
assert type(dt64_dt - dt64_date) is np.timedelta64
assert type(dt64_dt - dt64_dt) is np.timedelta64
assert type(dt64_dt - dt64_int) is np.timedelta64
assert type(dt64_dt - dt64_nat) is np.timedelta64

assert type(dt64_dt - td64) is np.datetime64
assert type(dt64_dt - td64_any) is np.datetime64
assert type(dt64_dt - td64_int) is np.datetime64
assert type(dt64_dt - td64_nat) is np.datetime64
assert type(dt64_dt - td64_td) is np.datetime64

# datetime64[int]
with pytest.raises(TypeError): dt64_int - py_date  # type: ignore[operator]
with pytest.raises(TypeError): dt64_int - py_dt  # type: ignore[operator]
with pytest.raises(TypeError): dt64_int - py_td  # type: ignore[operator]

assert type(dt64_int - dt64) is np.timedelta64
assert type(dt64_int - dt64_any) is np.timedelta64
assert type(dt64_int - dt64_date) is np.timedelta64
assert type(dt64_int - dt64_dt) is np.timedelta64
assert type(dt64_int - dt64_int) is np.timedelta64
assert type(dt64_int - dt64_nat) is np.timedelta64

assert type(dt64_int - td64) is np.datetime64
assert type(dt64_int - td64_any) is np.datetime64
assert type(dt64_int - td64_int) is np.datetime64
assert type(dt64_int - td64_nat) is np.datetime64
assert type(dt64_int - td64_td) is np.datetime64

# datetime64[None]
with pytest.raises(TypeError): dt64_nat - py_date  # type: ignore[operator]
with pytest.raises(TypeError): dt64_nat - py_dt  # type: ignore[operator]
with pytest.raises(TypeError): dt64_nat - py_td  # type: ignore[operator]

assert type(dt64_nat - dt64) is np.timedelta64
assert type(dt64_nat - dt64_any) is np.timedelta64
assert type(dt64_nat - dt64_date) is np.timedelta64
assert type(dt64_nat - dt64_dt) is np.timedelta64
assert type(dt64_nat - dt64_int) is np.timedelta64
assert type(dt64_nat - dt64_nat) is np.timedelta64

assert type(dt64_nat - td64) is np.datetime64
assert type(dt64_nat - td64_any) is np.datetime64
assert type(dt64_nat - td64_int) is np.datetime64
assert type(dt64_nat - td64_nat) is np.datetime64
assert type(dt64_nat - td64_td) is np.datetime64

# ---------- static checks ----------

# dt.date - ?
assert_type(py_date - py_td, dt.date)
assert_type(py_date - py_date, dt.timedelta)

# dt.datetime - ?
assert_type(py_dt - py_td, dt.datetime)
assert_type(py_dt - py_dt, dt.timedelta)

# datetime64 - ?
# assert_type(dt64 - py_date,    dt.timedelta)
# assert_type(dt64 - py_dt,      dt.timedelta)
# assert_type(dt64 - py_td,      dt.date)  # ❌ raises [operator]

assert_type(dt64 - dt64, "np.timedelta64")
assert_type(dt64 - dt64_any, "np.timedelta64")
assert_type(dt64 - dt64_date, "np.timedelta64")
assert_type(dt64 - dt64_dt, "np.timedelta64")
assert_type(dt64 - dt64_int, "np.timedelta64[int | None]")
assert_type(dt64 - dt64_nat, "np.timedelta64[None]")

assert_type(dt64 - td64, "np.datetime64")
assert_type(dt64 - td64_any, "np.datetime64")
assert_type(dt64 - td64_int, "np.datetime64[int | None]")
assert_type(dt64 - td64_nat, "np.datetime64[None]")
assert_type(dt64 - td64_td, "np.datetime64")

# datetime64[Any] - ?
# assert_type(dt64_any - py_date,    dt.timedelta)
# assert_type(dt64_any - py_dt,      dt.timedelta)
# assert_type(dt64_any - py_td,      dt.date)  # ❌ raises [operator]

assert_type(dt64_any - dt64, "np.timedelta64[Any]")
assert_type(dt64_any - dt64_any, "np.timedelta64[Any]")
assert_type(dt64_any - dt64_date, "np.timedelta64[Any]")
assert_type(dt64_any - dt64_dt, "np.timedelta64[Any]")
assert_type(dt64_any - dt64_int, "np.timedelta64[Any]")
assert_type(dt64_any - dt64_nat, "np.timedelta64[None]")

assert_type(dt64_any - td64, "np.datetime64[Any]")
assert_type(dt64_any - td64_any, "np.datetime64[Any]")
assert_type(dt64_any - td64_int, "np.datetime64[Any]")
assert_type(dt64_any - td64_nat, "np.datetime64[None]")
assert_type(dt64_any - td64_td, "np.datetime64[Any]")

# datetime64[dt.date] - ?
assert_type(dt64_date - py_date, dt.timedelta)
# assert_type(np_dt_date - py_dt,      dt.timedelta)
assert_type(dt64_date - py_td, dt.date)  # ❌ raises [operator]

assert_type(dt64_date - dt64, "np.timedelta64")
assert_type(dt64_date - dt64_any, "np.timedelta64")
assert_type(dt64_date - dt64_date, "np.timedelta64[dt.timedelta]")
assert_type(dt64_date - dt64_dt, "np.timedelta64[dt.timedelta]")
assert_type(dt64_date - dt64_int, "np.timedelta64[int]")
assert_type(dt64_date - dt64_nat, "np.timedelta64[None]")

assert_type(dt64_date - td64, "np.datetime64[dt.date]")
assert_type(dt64_date - td64_any, "np.datetime64[dt.date]")
assert_type(dt64_date - td64_int, "np.datetime64[int]")
assert_type(dt64_date - td64_nat, "np.datetime64[None]")
assert_type(dt64_date - td64_td, "np.datetime64[dt.date]")

# datetime64[dt.datetime] - ?
# assert_type(dt64_dt - py_date, dt.timedelta)  # unsupported
assert_type(dt64_dt - py_dt, dt.timedelta)
assert_type(dt64_dt - py_td, dt.datetime)  # ❌ raises [operator]

assert_type(dt64_dt - dt64, "np.timedelta64[dt.timedelta]")
assert_type(dt64_dt - dt64_any, "np.timedelta64[dt.timedelta]")
assert_type(dt64_dt - dt64_date, "np.timedelta64[dt.timedelta]")
assert_type(dt64_dt - dt64_dt, "np.timedelta64[dt.timedelta]")
assert_type(dt64_dt - dt64_int, "np.timedelta64[int]")
assert_type(dt64_dt - dt64_nat, "np.timedelta64[None]")

assert_type(dt64_dt - td64, "np.datetime64[dt.datetime]")
assert_type(dt64_dt - td64_any, "np.datetime64[dt.datetime]")
assert_type(dt64_dt - td64_int, "np.datetime64[int]")
assert_type(dt64_dt - td64_nat, "np.datetime64[None]")
assert_type(dt64_dt - td64_td, "np.datetime64[dt.datetime]")

# datetime64[int] - ?
# assert_type(np_dt_int - py_date,    dt.timedelta)
# assert_type(np_dt_int - py_dt,      dt.timedelta)
# assert_type(np_dt_int - py_td,      dt.date)  # ❌ raises [operator]

assert_type(dt64_int - dt64, "np.timedelta64[int]")
assert_type(dt64_int - dt64_any, "np.timedelta64[int | None]")
assert_type(dt64_int - dt64_date, "np.timedelta64[int]")
assert_type(dt64_int - dt64_dt, "np.timedelta64[int]")
assert_type(dt64_int - dt64_int, "np.timedelta64[int]")
assert_type(dt64_int - dt64_nat, "np.timedelta64[None]")

assert_type(dt64_int - td64, "np.datetime64")
assert_type(dt64_int - td64_any, "np.datetime64")
assert_type(dt64_int - td64_int, "np.datetime64[int]")
assert_type(dt64_int - td64_nat, "np.datetime64[None]")
assert_type(dt64_int - td64_td, "np.datetime64[int]")

# datetime64[None] - ?
# assert_type(dt64_nat - py_date,    dt.timedelta)
# assert_type(dt64_nat - py_dt,      dt.timedelta)
# assert_type(dt64_nat - py_td,      dt.date)  # ❌ raises [operator]

assert_type(dt64_nat - dt64, "np.timedelta64[None]")
assert_type(dt64_nat - dt64_any, "np.timedelta64[None]")
assert_type(dt64_nat - dt64_date, "np.timedelta64[None]")
assert_type(dt64_nat - dt64_dt, "np.timedelta64[None]")
assert_type(dt64_nat - dt64_int, "np.timedelta64[None]")
assert_type(dt64_nat - dt64_nat, "np.timedelta64[None]")

assert_type(dt64_nat - td64, "np.datetime64")
assert_type(dt64_nat - td64_any, "np.datetime64[None]")
assert_type(dt64_nat - td64_int, "np.datetime64[None]")
assert_type(dt64_nat - td64_nat, "np.datetime64[None]")
assert_type(dt64_nat - td64_td, "np.datetime64[None]")
# fmt: on




# class Timedelta(Protocol):
#     def __add__(self, other: Self, /) -> Self: ...
#     def __radd__(self, other: Self, /) -> Self: ...
#     def __sub__(self, other: Self, /) -> Self: ...
#     def __rsub__(self, other: Self, /) -> Self: ...
#
#
# class Timestamp[TD: Timedelta](Protocol):
#     @overload
#     def __sub__(self, other: Self, /) -> TD: ...
#     @overload
#     def __sub__(self, other: TD, /) -> Self: ...
#
#
# class SupportsSubSelf[TD: Timedelta](Protocol):
#     def __sub__(self, other: Self, /) -> TD: ...
#
#
# class SupportsSubTD[TD: Timedelta](Protocol):
#     def __sub__(self, other: TD, /) -> Self: ...
#
#
# td: Timedelta = np_td
#
# _a1: SupportsSubTD = py_dt  # ✅
# _a2: SupportsSubTD = np_dt  # ✅
# _a3: SupportsSubTD[np.timedelta64] = np_dt  # ❌
#
# _b1: SupportsSubSelf = py_dt  # ✅
# _b2: SupportsSubSelf = np_dt  # ✅
# _b3: SupportsSubSelf[np.timedelta64] = np_dt  # ❌
#
# # w/o generic
# _5: Timestamp = py_dt  # ✅
# _6: Timestamp = np_dt  # ❌ (not fixed by reorder)
# # w/ generic
# _7: Timestamp[dt.timedelta] = py_dt  # ✅
# _8: Timestamp[np.timedelta64] = np_dt  # ❌ (not fixed by reorder)
# # w/ nested generic
# _9: Timestamp[np.timedelta64[dt.timedelta]] = np_dt  # ❌ (fixed by reorder)
#
#
# def infer_td_type[TD: Timedelta](x: Timestamp[TD]) -> Timestamp[TD]:
#     return x
#
# # mypy fails these but pyright passes
# assert_type(infer_td_type(np_dt), "Timestamp[np.timedelta64[dt.timedelta]]")
# assert_type(infer_td_type(np_dt_int), "Timestamp[np.timedelta64[int]]")
# assert_type(infer_td_type(dt64_nat), "Timestamp[np.timedelta64[None]]")
