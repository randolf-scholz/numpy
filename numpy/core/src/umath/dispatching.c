/*
 * This file implements universal function dispatching and promotion (which
 * is necessary to happen before dispatching).
 * This is part of the UFunc object.  Promotion and dispatching uses the
 * following things:
 *
 * - operand_DTypes:  The datatypes as passed in by the user.
 * - signature: The DTypes fixed by the user with `dtype=` or `signature=`.
 * - ufunc._loops: A list of all ArrayMethods and promoters, it contains
 *   tuples `(dtypes, ArrayMethod)` or `(dtypes, promoter)`.
 * - ufunc._dispatch_cache: A cache to store previous promotion and/or
 *   dispatching results.
 * - The actual arrays are used to support the old code paths where necessary.
 *   (this includes any value-based casting/promotion logic)
 *
 * In general, `operand_Dtypes` is always overridden by `signature`.  If a
 * DType is included in the `signature` it must match precisely.
 *
 * The process of dispatching and promotion can be summarized in the following
 * steps:
 *
 * 1. Override any `operand_DTypes` from `signature`.
 * 2. Check if the new `operand_Dtypes` is cached (got to 4. if it is)
 * 3. Find the best matching "loop".  This is done using multiple dispatching
 *    on all `operand_DTypes` and loop `dtypes`.  A matching loop must be
 *    one whose DTypes are superclasses of the `operand_DTypes` (that are
 *    defined).  The best matching loop must be better than any other matching
 *    loop.  This result is cached.
 * 4. If the found loop is a promoter: We call the promoter. It can modify
 *    the `operand_DTypes` currently.  Then go back to step 2.
 *    (The promoter can call arbitrary code, so it could even add the matching
 *    loop first.)
 * 5. The final `ArrayMethod` is found, its registered `dtypes` is copied
 *    into the `signature` so that it is available to the ufunc loop.
 *
 */
#include <Python.h>

#define _UMATHMODULE
#define _MULTIARRAYMODULE
#define NPY_NO_DEPRECATED_API NPY_API_VERSION

#include "numpy/ndarraytypes.h"
#include "common.h"

#include "dispatching.h"
#include "dtypemeta.h"
#include "npy_hashtable.h"
#include "legacy_array_method.h"
#include "ufunc_object.h"
#include "ufunc_type_resolution.h"


/* forward declaration */
static PyObject *
promote_and_get_info_and_ufuncimpl(PyUFuncObject *ufunc,
        PyArrayObject *const ops[], PyArray_DTypeMeta *signature[],
        PyArray_DTypeMeta *op_dtypes[], int do_legacy_fallback, int cache);


/**
 * Function to add a new loop to the ufunc.  This mainly appends it to the
 * list (as it currently is just a list).
 *
 * @param ufunc The universal function to add the loop to.
 * @param info The tuple (dtype_tuple, ArrayMethod/promoter).
 * @param ignore_duplicate If 1 and a loop with the same `dtype_tuple` is
 *        found, the function does nothing.
 */
static int
add_ufunc_loop(PyUFuncObject *ufunc, PyObject *info, int ignore_duplicate)
{
    assert(PyTuple_CheckExact(info) && PyTuple_GET_SIZE(info) == 2);

    if (ufunc->_loops == NULL) {
        ufunc->_loops = PyList_New(0);
        if (ufunc->_loops == NULL) {
            return -1;
        }
    }

    PyObject *DType_tuple = PyTuple_GetItem(info, 0);

    PyObject *loops = ufunc->_loops;
    Py_ssize_t length = PyList_Size(loops);
    for (Py_ssize_t i = 0; i < length; i++) {
        PyObject *item = PyList_GetItem(loops, i);
        PyObject *cur_DType_tuple = PyTuple_GetItem(item, 0);
        int cmp = PyObject_RichCompareBool(cur_DType_tuple, DType_tuple, Py_EQ);
        if (cmp < 0) {
            return -1;
        }
        if (cmp == 0) {
            continue;
        }
        if (ignore_duplicate) {
            return 0;
        }
        PyErr_Format(PyExc_TypeError,
                "A loop/promoter has already been registered with '%s' for %R",
                ufunc_get_name_cstr(ufunc), DType_tuple);
        return -1;
    }

    PyList_Append(loops, info);
    return 0;
}


/**
 * Resolves the implementation to use, this uses typical multiple dispatching
 * methods of finding the best matching implementation or resolver.
 * (Based on `isinstance()`, the knowledge that non-abstract DTypes cannot
 * be subclassed is used, however.)
 *
 * @param ufunc
 * @param op_dtypes The DTypes that are either passed in (defined by an
 *        operand) or defined by the `signature` as also passed in as
 *        `fixed_DTypes`.
 * @param out_info Returns the tuple describing the best implementation
 *        (consisting of dtypes and ArrayMethod or promoter).
 *        WARNING: Returns a borrowed reference!
 * @returns -1 on error 0 on success.  Note that the output can be NULL on
 *          success if nothing is found.
 */
static int
resolve_implementation_info(PyUFuncObject *ufunc,
        PyArray_DTypeMeta *op_dtypes[], PyObject **out_info)
{
    int nin = ufunc->nin, nargs = ufunc->nargs;
    /* Use new style type resolution has to happen... */
    Py_ssize_t size = PySequence_Length(ufunc->_loops);
    PyObject *best_dtypes = NULL;
    PyObject *best_resolver_info = NULL;

    for (Py_ssize_t res_idx = 0; res_idx < size; res_idx++) {
        /* Test all resolvers  */
        PyObject *resolver_info = PySequence_Fast_GET_ITEM(
                ufunc->_loops, res_idx);
        PyObject *curr_dtypes = PyTuple_GET_ITEM(resolver_info, 0);
        /*
         * Test if the current resolver matches, it could make sense to
         * reorder these checks to avoid the IsSubclass check as much as
         * possible.
         */

        npy_bool matches = NPY_TRUE;
        /*
         * NOTE: We check also the output DType.  In principle we do not
         *       have to strictly match it (unless it is provided by the
         *       `signature`).  This assumes that a (fallback) promoter will
         *       unset the output DType if no exact match is found.
         */
        for (Py_ssize_t i = 0; i < nargs; i++) {
            PyArray_DTypeMeta *given_dtype = op_dtypes[i];
            PyArray_DTypeMeta *resolver_dtype = (
                    (PyArray_DTypeMeta *)PyTuple_GET_ITEM(curr_dtypes, i));
            assert((PyObject *)given_dtype != Py_None);
            if (given_dtype == NULL && i >= nin) {
                /* Unspecified out always matches (see below for inputs) */
                continue;
            }
            if (given_dtype == resolver_dtype) {
                continue;
            }
            if (!resolver_dtype->abstract) {
                matches = NPY_FALSE;
                break;
            }
            if (given_dtype == NULL) {
                /*
                 * If the (input) was not specified, this is a reduce-like
                 * operation.  Some ufuncs may have non-trivial promotion
                 * (e.g. add/multiply ensure high precision).
                 * Continuing here matches promoters: those can deal with that.
                 * If we allow this path for ArrayMethod, the person
                 * registering will have it works sense for the ufunc, a
                 * counter example is `(BoolLike, Bool, Bool)` for `add`.
                 * It should resolve to an integer result (sum the bools)
                 * in a reduction. But the ArrayMethod cannot work with that
                 * (NumPy will prevent it to ensure correctness).
                 */
                continue;
            }
            int subclass = PyObject_IsSubclass(
                    (PyObject *)given_dtype, (PyObject *)resolver_dtype);
            if (subclass < 0) {
                return -1;
            }
            if (!subclass) {
                matches = NPY_FALSE;
                break;
            }
            /*
             * TODO: Could consider allowing reverse subclass relation, i.e.
             *       the operation DType passed in to be abstract.  That
             *       definitely is OK for outputs (and potentially useful,
             *       you could enforce e.g. an inexact result).
             *       It might also be useful for some stranger promoters.
             */
        }
        if (!matches) {
            continue;
        }

        /* The resolver matches, but we have to check if it is better */
        if (best_dtypes != NULL) {
            int current_best = -1;  /* -1 neither, 0 current best, 1 new */
            /*
             * If both have concrete and None in the same position and
             * they are identical, we will continue searching using the
             * first best for comparison, in an attempt to find a better
             * one.
             * In all cases, we give up resolution, since it would be
             * necessary to compare to two "best" cases.
             */
            int unambiguous_equivally_good = 1;
            for (Py_ssize_t i = 0; i < nargs; i++) {
                int best;

                /* Whether this (normally output) dtype was specified at all */
                int is_not_specified = (
                        op_dtypes[i] == (PyArray_DTypeMeta *)Py_None);

                PyObject *prev_dtype = PyTuple_GET_ITEM(best_dtypes, i);
                PyObject *new_dtype = PyTuple_GET_ITEM(curr_dtypes, i);

                if (prev_dtype == new_dtype) {
                    /* equivalent, so this entry does not matter */
                    continue;
                }
                if (is_not_specified) {
                    /*
                     * When DType is completely unspecified, prefer abstract
                     * over concrete, assuming it will resolve.
                     * Furthermore, we cannot decide which abstract/None
                     * is "better", only concrete ones which are subclasses
                     * of Abstract ones are defined as worse.
                     */
                    int prev_is_concrete = 0, new_is_concrete = 0;
                    if ((prev_dtype != Py_None) &&
                        (!((PyArray_DTypeMeta *)prev_dtype)->abstract)) {
                        prev_is_concrete = 1;
                    }
                    if ((new_dtype != Py_None) &&
                        (!((PyArray_DTypeMeta *)new_dtype)->abstract)) {
                        new_is_concrete = 1;
                    }
                    if (prev_is_concrete == new_is_concrete) {
                        best = -1;
                    }
                    else if (prev_is_concrete) {
                        unambiguous_equivally_good = 0;
                        best = 1;
                    }
                    else {
                        unambiguous_equivally_good = 0;
                        best = 0;
                    }
                }
                    /* If either is None, the other is strictly more specific */
                else if (prev_dtype == Py_None) {
                    unambiguous_equivally_good = 0;
                    best = 1;
                }
                else if (new_dtype == Py_None) {
                    unambiguous_equivally_good = 0;
                    best = 0;
                }
                    /*
                     * If both are concrete and not identical, this is
                     * ambiguous.
                     */
                else if (!((PyArray_DTypeMeta *)prev_dtype)->abstract &&
                         !((PyArray_DTypeMeta *)new_dtype)->abstract) {
                    /*
                     * Ambiguous unless the are identical (checked above),
                     * but since they are concrete it does not matter which
                     * best to compare.
                     */
                    best = -1;
                }
                else if (!((PyArray_DTypeMeta *)prev_dtype)->abstract) {
                    /* old is not abstract, so better (both not possible) */
                    unambiguous_equivally_good = 0;
                    best = 0;
                }
                else if (!((PyArray_DTypeMeta *)new_dtype)->abstract) {
                    /* new is not abstract, so better (both not possible) */
                    unambiguous_equivally_good = 0;
                    best = 1;
                }
                /*
                 * Both are abstract DTypes, there is a clear order if
                 * one of them is a subclass of the other.
                 * If this fails, reject it completely (could be changed).
                 * The case that it is the same dtype is already caught.
                 */
                else {
                    /* Note the identity check above, so this true subclass */
                    int new_is_subclass = PyObject_IsSubclass(
                            new_dtype, prev_dtype);
                    if (new_is_subclass < 0) {
                        return -1;
                    }
                    /*
                     * Could optimize this away if above is True, but this
                     * catches inconsistent definitions of subclassing.
                     */
                    int prev_is_subclass = PyObject_IsSubclass(
                            prev_dtype, new_dtype);
                    if (prev_is_subclass < 0) {
                        return -1;
                    }
                    if (prev_is_subclass && new_is_subclass) {
                        /* should not happen unless they are identical */
                        PyErr_SetString(PyExc_RuntimeError,
                                "inconsistent subclassing of DTypes; if "
                                "this happens, two dtypes claim to be a "
                                "superclass of the other one.");
                        return -1;
                    }
                    if (!prev_is_subclass && !new_is_subclass) {
                        /* Neither is more precise than the other one */
                        PyErr_SetString(PyExc_TypeError,
                                "inconsistent type resolution hierarchy; "
                                "DTypes of two matching loops do not have "
                                "a clear hierarchy defined. Diamond shape "
                                "inheritance is unsupported for use with "
                                "UFunc type resolution. (You may resolve "
                                "this by inserting an additional common "
                                "subclass). This limitation may be "
                                "partially resolved in the future.");
                        return -1;
                    }
                    if (new_is_subclass) {
                        unambiguous_equivally_good = 0;
                        best = 1;
                    }
                    else {
                        unambiguous_equivally_good = 0;
                        best = 2;
                    }
                }
                if ((current_best != -1) && (current_best != best)) {
                    /*
                     * We need a clear best, this could be tricky, unless
                     * the signature is identical, we would have to compare
                     * against both of the found ones until we find a
                     * better one.
                     * Instead, only support the case where they are
                     * identical.
                     */
                    /* TODO: Document the above comment, may need relaxing? */
                    current_best = -1;
                    break;
                }
                current_best = best;
            }

            if (current_best == -1) {
                /*
                 * TODO: It would be nice to have a "diagnostic mode" that
                 *       informs if this happens! (An immediate error current
                 *       blocks later legacy resolution, but may work in the
                 *       future.)
                 */
                if (unambiguous_equivally_good) {
                    /* unset the best resolver to indicate this */
                    best_resolver_info = NULL;
                    continue;
                }
                *out_info = NULL;
                return 0;
            }
            else if (current_best == 0) {
                /* The new match is not better, continue looking. */
                continue;
            }
        }
        /* The new match is better (or there was no previous match) */
        best_dtypes = curr_dtypes;
        best_resolver_info = resolver_info;
    }
    if (best_dtypes == NULL) {
        /* The non-legacy lookup failed */
        *out_info = NULL;
        return 0;
    }

    *out_info = best_resolver_info;
    return 0;
}


/*
 * A promoter can currently be either a C-Capsule containing a promoter
 * function pointer, or a Python function.  Both of these can at this time
 * only return new operation DTypes (i.e. mutate the input while leaving
 * those defined by the `signature` unmodified).
 */
static PyObject *
call_promoter_and_recurse(
        PyUFuncObject *NPY_UNUSED(ufunc), PyObject *NPY_UNUSED(promoter),
        PyArray_DTypeMeta *NPY_UNUSED(op_dtypes[]),
        PyArray_DTypeMeta *NPY_UNUSED(signature[]),
        PyArrayObject *const NPY_UNUSED(operands[]))
{
    PyErr_SetString(PyExc_NotImplementedError,
            "Internal NumPy error, promoters are not used/implemented yet.");
    return NULL;
}


/*
 * Used for the legacy fallback promotion when `signature` or `dtype` is
 * provided.
 * We do not need to pass the type tuple when we use the legacy path
 * for type resolution rather than promotion; the old system did not
 * differentiate between these two concepts.
 */
static int
_make_new_typetup(
        int nop, PyArray_DTypeMeta *signature[], PyObject **out_typetup) {
    *out_typetup = PyTuple_New(nop);
    if (*out_typetup == NULL) {
        return -1;
    }

    int none_count = 0;
    for (int i = 0; i < nop; i++) {
        PyObject *item;
        if (signature[i] == NULL) {
            item = Py_None;
            none_count++;
        }
        else {
            if (!signature[i]->legacy || signature[i]->abstract) {
                /*
                 * The legacy type resolution can't deal with these.
                 * This path will return `None` or so in the future to
                 * set an error later if the legacy type resolution is used.
                 */
                PyErr_SetString(PyExc_RuntimeError,
                        "Internal NumPy error: new DType in signature not yet "
                        "supported. (This should be unreachable code!)");
                Py_SETREF(*out_typetup, NULL);
                return -1;
            }
            item = (PyObject *)signature[i]->singleton;
        }
        Py_INCREF(item);
        PyTuple_SET_ITEM(*out_typetup, i, item);
    }
    if (none_count == nop) {
        /* The whole signature was None, simply ignore type tuple */
        Py_DECREF(*out_typetup);
        *out_typetup = NULL;
    }
    return 0;
}


/*
 * Fills in the operation_DTypes with borrowed references.  This may change
 * the content, since it will use the legacy type resolution, which can special
 * case 0-D arrays (using value-based logic).
 */
static int
legacy_promote_using_legacy_type_resolver(PyUFuncObject *ufunc,
        PyArrayObject *const *ops, PyArray_DTypeMeta *signature[],
        PyArray_DTypeMeta *operation_DTypes[], int *out_cacheable)
{
    int nargs = ufunc->nargs;
    PyArray_Descr *out_descrs[NPY_MAXARGS];
    memset(out_descrs, 0, nargs * sizeof(*out_descrs));

    PyObject *type_tuple = NULL;
    if (_make_new_typetup(nargs, signature, &type_tuple) < 0) {
        return -1;
    }

    /*
     * We use unsafe casting. This is of course not accurate, but that is OK
     * here, because for promotion/dispatching the casting safety makes no
     * difference.  Whether the actual operands can be casts must be checked
     * during the type resolution step (which may _also_ calls this!).
     */
    if (ufunc->type_resolver(ufunc,
            NPY_UNSAFE_CASTING, (PyArrayObject **)ops, type_tuple,
            out_descrs) < 0) {
        Py_XDECREF(type_tuple);
        return -1;
    }
    Py_XDECREF(type_tuple);

    for (int i = 0; i < nargs; i++) {
        operation_DTypes[i] = NPY_DTYPE(out_descrs[i]);
        Py_INCREF(operation_DTypes[i]);
        Py_DECREF(out_descrs[i]);
    }
    if (ufunc->type_resolver == &PyUFunc_SimpleBinaryComparisonTypeResolver) {
        /*
         * In this one case, the deprecation means that we actually override
         * the signature.
         */
        for (int i = 0; i < nargs; i++) {
            if (signature[i] != NULL && signature[i] != operation_DTypes[i]) {
                Py_INCREF(operation_DTypes[i]);
                Py_SETREF(signature[i], operation_DTypes[i]);
                *out_cacheable = 0;
            }
        }
    }
    return 0;
}


/*
 * Note, this function returns a BORROWED references to info since it adds
 * it to the loops.
 */
NPY_NO_EXPORT PyObject *
add_and_return_legacy_wrapping_ufunc_loop(PyUFuncObject *ufunc,
        PyArray_DTypeMeta *operation_dtypes[], int ignore_duplicate)
{
    PyObject *DType_tuple = PyArray_TupleFromItems(ufunc->nargs,
            (PyObject **)operation_dtypes, 0);
    if (DType_tuple == NULL) {
        return NULL;
    }

    PyArrayMethodObject *method = PyArray_NewLegacyWrappingArrayMethod(
            ufunc, operation_dtypes);
    if (method == NULL) {
        Py_DECREF(DType_tuple);
        return NULL;
    }
    PyObject *info = PyTuple_Pack(2, DType_tuple, method);
    Py_DECREF(DType_tuple);
    Py_DECREF(method);
    if (info == NULL) {
        return NULL;
    }
    if (add_ufunc_loop(ufunc, info, ignore_duplicate) < 0) {
        Py_DECREF(info);
        return NULL;
    }

    return info;
}


/*
 * The central entry-point for the promotion and dispatching machinery.
 * It currently works with the operands (although it would be possible to
 * only work with DType (classes/types).
 */
static NPY_INLINE PyObject *
promote_and_get_info_and_ufuncimpl(PyUFuncObject *ufunc,
        PyArrayObject *const ops[], PyArray_DTypeMeta *signature[],
        PyArray_DTypeMeta *op_dtypes[], int do_legacy_fallback, int cache)
{
    /*
     * Fetch the dispatching info which consists of the implementation and
     * the DType signature tuple.  There are three steps:
     *
     * 1. Check the cache.
     * 2. Check all registered loops/promoters to find the best match.
     * 3. Fall back to the legacy implementation if no match was found.
     */
    PyObject *info = PyArrayIdentityHash_GetItem(ufunc->_dispatch_cache,
                (PyObject **)op_dtypes);
    if (info != NULL && PyObject_TypeCheck(
            PyTuple_GET_ITEM(info, 1), &PyArrayMethod_Type)) {
        return info;
    }

    if (info == NULL) {
        if (resolve_implementation_info(ufunc, op_dtypes, &info) < 0) {
            return NULL;
        }
        if (info != NULL && PyObject_TypeCheck(
                PyTuple_GET_ITEM(info, 1), &PyArrayMethod_Type)) {
            /*
             * Cache the new one.  NOTE: If we allow a promoter to return
             * a new ArrayMethod, we should also cache such a promoter also.
             */
            if (cache && PyArrayIdentityHash_SetItem(ufunc->_dispatch_cache,
                    (PyObject **)op_dtypes, info, 0) < 0) {
                return NULL;
            }
            return info;
        }
    }

    if (info != NULL) {
        PyObject *promoter = PyTuple_GET_ITEM(info, 1);

        info = call_promoter_and_recurse(ufunc,
                promoter, op_dtypes, signature, ops);
        if (info == NULL && PyErr_Occurred()) {
            return NULL;
        }
        else if (info != NULL) {
            return info;
        }
    }

    /*
     * Using promotion failed, this should normally be an error.
     * However, we need to give the legacy implementation a chance here.
     * (it will modify `op_dtypes`).
     */
    if (!do_legacy_fallback || ufunc->type_resolver == NULL ||
            (ufunc->ntypes == 0 && ufunc->userloops == NULL)) {
        /* Already tried or not a "legacy" ufunc (no loop found, return) */
        return NULL;
    }

    PyArray_DTypeMeta *new_op_dtypes[NPY_MAXARGS];
    int cacheable = 1;  /* TODO: only the comparison deprecation needs this */
    if (legacy_promote_using_legacy_type_resolver(ufunc,
            ops, signature, new_op_dtypes, &cacheable) < 0) {
        return NULL;
    }
    return promote_and_get_info_and_ufuncimpl(ufunc,
            ops, signature, new_op_dtypes, 0, cacheable);
}


/*
 * The central entry-point for the promotion and dispatching machinery.
 * It currently works with the operands (although it would be possible to
 * only work with DType (classes/types).
 */
NPY_NO_EXPORT PyArrayMethodObject *
promote_and_get_ufuncimpl(PyUFuncObject *ufunc,
        PyArrayObject *const ops[], PyArray_DTypeMeta *signature[],
        PyArray_DTypeMeta *op_dtypes[], int force_legacy_promotion)
{
    int nargs = ufunc->nargs;

    /*
     * Get the actual DTypes we operate with by mixing the operand array
     * ones with the passed signature.
     */
    for (int i = 0; i < nargs; i++) {
        if (signature[i] != NULL) {
            /*
             * ignore the operand input, we cannot overwrite signature yet
             * since it is fixed (cannot be promoted!)
             */
            op_dtypes[i] = signature[i];
            assert(i >= ufunc->nin || !signature[i]->abstract);
        }
    }

    if (force_legacy_promotion) {
        /*
         * We must use legacy promotion for value-based logic. Call the old
         * resolver once up-front to get the "actual" loop dtypes.
         * After this (additional) promotion, we can even use normal caching.
         */
        int cacheable = 1;  /* unused, as we modify the original `op_dtypes` */
        if (legacy_promote_using_legacy_type_resolver(ufunc,
                ops, signature, op_dtypes, &cacheable) < 0) {
            return NULL;
        }
    }

    PyObject *info = promote_and_get_info_and_ufuncimpl(ufunc,
            ops, signature, op_dtypes, 1, 1);

    if (info == NULL) {
        if (!PyErr_Occurred()) {
            raise_no_loop_found_error(ufunc, (PyObject **)op_dtypes);
        }
        return NULL;
    }

    PyArrayMethodObject *method = (PyArrayMethodObject *)PyTuple_GET_ITEM(info, 1);

    /* Fill in the signature with the signature that we will be working with */
    PyObject *all_dtypes = PyTuple_GET_ITEM(info, 0);
    for (int i = 0; i < nargs; i++) {
        if (signature[i] == NULL) {
            signature[i] = (PyArray_DTypeMeta *)PyTuple_GET_ITEM(all_dtypes, i);
            Py_INCREF(signature[i]);
        }
        else {
            assert((PyObject *)signature[i] == PyTuple_GET_ITEM(all_dtypes, i));
        }
    }

    return method;
}
