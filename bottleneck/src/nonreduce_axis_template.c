#include "bottleneck.h"
#include "iterators.h"

/* function signatures --------------------------------------------------- */

/* low-level functions such as move_sum_float64 */
#define NRA(name, dtype) \
    static PyObject * \
    name##_##dtype(PyArrayObject *a, int axis, int n)

/* top-level functions such as move_sum */
#define NRA_MAIN(name, parse) \
    static PyObject * \
    name(PyObject *self, PyObject *args, PyObject *kwds) \
    { \
        return nonreducer_axis(#name, \
                               args, \
                               kwds, \
                               name##_float64, \
                               name##_float32, \
                               name##_int64, \
                               name##_int32, \
                               parse); \
    }

/* typedefs and prototypes ----------------------------------------------- */

/* how should input be parsed? */
typedef enum {PARSE_PARTSORT, PARSE_RANKDATA, PARSE_PUSH} parse_type;

/* function pointer for functions passed to nonreducer_axis */
typedef PyObject *(*nra_t)(PyArrayObject *, int, int);

static PyObject *
nonreducer_axis(char *name,
                PyObject *args,
                PyObject *kwds,
                nra_t,
                nra_t,
                nra_t,
                nra_t,
                parse_type);

/* partsort -------------------------------------------------------------- */

#define B(dtype, i) AX(dtype, i) /* used by PARTITION */

/* dtype = [['float64'], ['float32'], ['int64'], ['int32']] */
NRA(partsort, DTYPE0)
{
    npy_intp i;
    npy_intp j, l, r, k;
    iter it;

    a = (PyArrayObject *)PyArray_NewCopy(a, NPY_ANYORDER);
    init_iter_one(&it, a, axis);

    if (LENGTH == 0) return (PyObject *)a;
    if (n < 1 || n > LENGTH) {
        PyErr_Format(PyExc_ValueError,
                     "`n` (=%d) must be between 1 and %zd, inclusive.",
                     n, LENGTH);
        return NULL;
    }

    BN_BEGIN_ALLOW_THREADS
    k = n - 1;
    WHILE {
        l = 0;
        r = LENGTH - 1;
        PARTITION(DTYPE0)
        NEXT
    }
    BN_END_ALLOW_THREADS

    return (PyObject *)a;
}
/* dtype end */

NRA_MAIN(partsort, PARSE_PARTSORT)


/* argpartsort ----------------------------------------------------------- */

#define BUFFER_NEW(dtype) dtype *B = malloc(LENGTH * sizeof(dtype));
#define BUFFER_DELETE free(B);

#define ARGWIRTH(dtype0, dtype1) \
    x = B[k]; \
    i = l; \
    j = r; \
    do { \
        while (B[i] < x) i++; \
        while (x < B[j]) j--; \
        if (i <= j) { \
            npy_##dtype0 atmp = B[i]; \
            B[i] = B[j]; \
            B[j] = atmp; \
            ytmp = YX(dtype1, i); \
            YX(dtype1, i) = YX(dtype1, j); \
            YX(dtype1, j) = ytmp; \
            i++; \
            j--; \
        } \
    } while (i <= j); \
    if (j < k) l = i; \
    if (k < i) r = j;

#define ARGPARTITION(dtype0, dtype1) \
    while (l < r) { \
        npy_##dtype0 x; \
        npy_##dtype0 al = B[l]; \
        npy_##dtype0 ak = B[k]; \
        npy_##dtype0 ar = B[r]; \
        npy_##dtype1 ytmp; \
        if (al > ak) { \
            if (ak < ar) { \
                if (al < ar) { \
                    B[k] = al; \
                    B[l] = ak; \
                    ytmp = YX(dtype1, k); \
                    YX(dtype1, k) = YX(dtype1, l); \
                    YX(dtype1, l) = ytmp; \
                } \
                else { \
                    B[k] = ar; \
                    B[r] = ak; \
                    ytmp = YX(dtype1, k); \
                    YX(dtype1, k) = YX(dtype1, r); \
                    YX(dtype1, r) = ytmp; \
                } \
            } \
        } \
        else { \
            if (ak > ar) { \
                if (al > ar) { \
                    B[k] = al; \
                    B[l] = ak; \
                    ytmp = YX(dtype1, k); \
                    YX(dtype1, k) = YX(dtype1, l); \
                    YX(dtype1, l) = ytmp; \
                } \
                else { \
                    B[k] = ar; \
                    B[r] = ak; \
                    ytmp = YX(dtype1, k); \
                    YX(dtype1, k) = YX(dtype1, r); \
                    YX(dtype1, r) = ytmp; \
                } \
            } \
        } \
        ARGWIRTH(dtype0, dtype1) \
    }

#define ARGPARTSORT(dtype0, dtype1) \
    for (i = 0; i < LENGTH; i++) { \
        B[i] = AX(dtype0, i); \
        YX(dtype1, i) = i; \
    } \
    k = n - 1; \
    l = 0; \
    r = LENGTH - 1; \
    ARGPARTITION(dtype0, dtype1)

/* dtype = [['float64', 'intp'], ['float32', 'intp'],
            ['int64',   'intp'], ['int32',   'intp']] */
NRA(argpartsort, DTYPE0)
{
    npy_intp i;
    PyObject *y = PyArray_EMPTY(PyArray_NDIM(a), PyArray_SHAPE(a),
                                NPY_DTYPE1, 0);
    iter2 it;
    init_iter2(&it, a, y, axis);
    if (LENGTH == 0) return y;
    if (n < 1 || n > LENGTH) {
        PyErr_Format(PyExc_ValueError,
                     "`n` (=%d) must be between 1 and %zd, inclusive.",
                     n, LENGTH);
        return NULL;
    }
    BN_BEGIN_ALLOW_THREADS
    BUFFER_NEW(npy_DTYPE0)
    npy_intp j, l, r, k;
    k = n - 1;
    WHILE {
        l = 0;
        r = LENGTH - 1;
        ARGPARTSORT(DTYPE0, DTYPE1)
        NEXT2
    }
    BUFFER_DELETE
    BN_END_ALLOW_THREADS
    return y;
}
/* dtype end */

NRA_MAIN(argpartsort, PARSE_PARTSORT)


/* rankdata -------------------------------------------------------------- */

/* dtype = [['float64', 'float64', 'intp'], ['float32', 'float64', 'intp'],
            ['int64',   'float64', 'intp'], ['int32',   'float64', 'intp']] */
NRA(rankdata, DTYPE0)
{
    Py_ssize_t j=0, k, idx, dupcount=0, i;
    npy_DTYPE1 old, new, averank, sumranks = 0;

    PyObject *z = PyArray_ArgSort(a, axis, NPY_QUICKSORT);
    PyObject *y = PyArray_EMPTY(PyArray_NDIM(a),
                                PyArray_SHAPE(a), NPY_DTYPE1, 0);

    iter3 it;
    init_iter3(&it, a, y, z, axis);

    BN_BEGIN_ALLOW_THREADS
    if (LENGTH == 0) {
        Py_ssize_t size = PyArray_SIZE((PyArrayObject *)y);
        npy_DTYPE1 *py = (npy_DTYPE1 *)PyArray_DATA(a);
        for (i = 0; i < size; i++) YPP = BN_NAN;
    }
    else {
        WHILE {
            idx = ZX(DTYPE2, 0);
            old = AX(DTYPE0, idx);
            sumranks = 0;
            dupcount = 0;
            for (i = 0; i < LENGTH - 1; i++) {
                sumranks += i;
                dupcount++;
                k = i + 1;
                idx = ZX(DTYPE2, k);
                new = AX(DTYPE0, idx);
                if (old != new) {
                    averank = sumranks / dupcount + 1;
                    for (j = k - dupcount; j < k; j++) {
                        idx = ZX(DTYPE2, j);
                        YX(DTYPE1, idx) = averank;
                    }
                    sumranks = 0;
                    dupcount = 0;
                }
                old = new;
            }
            sumranks += (LENGTH - 1);
            dupcount++;
            averank = sumranks / dupcount + 1;
            for (j = LENGTH - dupcount; j < LENGTH; j++) {
                idx = ZX(DTYPE2, j);
                YX(DTYPE1, idx) = averank;
            }
            NEXT3
        }
    }
    BN_END_ALLOW_THREADS

    Py_DECREF(z);
    return y;
}
/* dtype end */

NRA_MAIN(rankdata, PARSE_RANKDATA)


/* nanrankdata ----------------------------------------------------------- */

/* dtype = [['float64', 'float64', 'intp'], ['float32', 'float64', 'intp']] */
NRA(nanrankdata, DTYPE0)
{
    Py_ssize_t j=0, k, idx, dupcount=0, i;
    npy_DTYPE1 old, new, averank, sumranks = 0;

    PyObject *z = PyArray_ArgSort(a, axis, NPY_QUICKSORT);
    PyObject *y = PyArray_EMPTY(PyArray_NDIM(a),
                                PyArray_SHAPE(a), NPY_DTYPE1, 0);

    iter3 it;
    init_iter3(&it, a, y, z, axis);

    BN_BEGIN_ALLOW_THREADS
    if (LENGTH == 0) {
        Py_ssize_t size = PyArray_SIZE((PyArrayObject *)y);
        npy_DTYPE1 *py = (npy_DTYPE1 *)PyArray_DATA(a);
        for (i = 0; i < size; i++) YPP = BN_NAN;
    }
    else {
        WHILE {
            idx = ZX(DTYPE2, 0);
            old = AX(DTYPE0, idx);
            sumranks = 0;
            dupcount = 0;
            for (i = 0; i < LENGTH - 1; i++) {
                sumranks += i;
                dupcount++;
                k = i + 1;
                idx = ZX(DTYPE2, k);
                new = AX(DTYPE0, idx);
                if (old != new) {
                    if (old == old) {
                        averank = sumranks / dupcount + 1;
                        for (j = k - dupcount; j < k; j++) {
                            idx = ZX(DTYPE2, j);
                            YX(DTYPE1, idx) = averank;
                        }
                    }
                    else {
                        idx = ZX(DTYPE2, i);
                        YX(DTYPE1, idx) = BN_NAN;
                    }
                    sumranks = 0;
                    dupcount = 0;
                }
                old = new;
            }
            sumranks += (LENGTH - 1);
            dupcount++;
            averank = sumranks / dupcount + 1;
            if (old == old) {
                for (j = LENGTH - dupcount; j < LENGTH; j++) {
                    idx = ZX(DTYPE2, j);
                    YX(DTYPE1, idx) = averank;
                }
            }
            else {
                idx = ZX(DTYPE2, LENGTH - 1);
                YX(DTYPE1, idx) = BN_NAN;
            }
            NEXT3
        }
    }
    BN_END_ALLOW_THREADS

    Py_DECREF(z);
    return y;
}
/* dtype end */

static PyObject *
nanrankdata(PyObject *self, PyObject *args, PyObject *kwds)
{
    return nonreducer_axis("nanrankdata",
                           args,
                           kwds,
                           nanrankdata_float64,
                           nanrankdata_float32,
                           rankdata_int64,
                           rankdata_int32,
                           PARSE_RANKDATA);
}


/* push ------------------------------------------------------------------ */

/* dtype = [['float64'], ['float32']] */
NRA(push, DTYPE0)
{
    npy_intp index;
    npy_DTYPE0 ai, ai_last, n_float;
    PyObject *y = PyArray_Copy(a);
    iter it;
    init_iter_one(&it, (PyArrayObject *)y, axis);
    if (LENGTH == 0 || NDIM == 0) {
        return y;
    }
    n_float = n < 0 ? BN_INFINITY : (npy_DTYPE0)n;
    BN_BEGIN_ALLOW_THREADS
    WHILE {
        index = 0;
        ai_last = BN_NAN;
        FOR {
            ai = AI(DTYPE0);
            if (ai == ai) {
                ai_last = ai;
                index = INDEX;
            }
            else {
                if (INDEX - index <= n_float) {
                    AI(DTYPE0) = ai_last;
                }
            }
        }
        NEXT
    }
    BN_END_ALLOW_THREADS
    return y;
}
/* dtype end */

/* dtype = [['int64'], ['int32']] */
NRA(push, DTYPE0)
{
    PyObject *y = PyArray_Copy(a);
    return y;
}
/* dtype end */

NRA_MAIN(push, PARSE_PUSH)


/* python strings -------------------------------------------------------- */

PyObject *pystr_arr = NULL;
PyObject *pystr_n = NULL;
PyObject *pystr_axis = NULL;

static int
intern_strings(void) {
    pystr_arr = PyString_InternFromString("arr");
    pystr_n = PyString_InternFromString("n");
    pystr_axis = PyString_InternFromString("axis");
    return pystr_arr && pystr_n && pystr_axis;
}

/* nonreducer_axis ------------------------------------------------------- */

static BN_INLINE int
parse_partsort(PyObject *args,
               PyObject *kwds,
               PyObject **arr,
               PyObject **n,
               PyObject **axis)
{
    const Py_ssize_t nargs = PyTuple_GET_SIZE(args);
    const Py_ssize_t nkwds = kwds == NULL ? 0 : PyDict_Size(kwds);
    if (nkwds) {
        int nkwds_found = 0;
        PyObject *tmp;
        switch (nargs) {
            case 2: *n = PyTuple_GET_ITEM(args, 1);
            case 1: *arr = PyTuple_GET_ITEM(args, 0);
            case 0: break;
            default:
                TYPE_ERR("wrong number of arguments");
                return 0;
        }
        switch (nargs) {
            case 0:
                *arr = PyDict_GetItem(kwds, pystr_arr);
                if (*arr == NULL) {
                    TYPE_ERR("Cannot find `arr` keyword input");
                    return 0;
                }
                nkwds_found += 1;
            case 1:
                *n = PyDict_GetItem(kwds, pystr_n);
                if (*n == NULL) {
                    TYPE_ERR("Cannot find `n` keyword input");
                    return 0;
                }
                nkwds_found++;
            case 2:
                tmp = PyDict_GetItem(kwds, pystr_axis);
                if (tmp != NULL) {
                    *axis = tmp;
                    nkwds_found++;
                }
                break;
            default:
                TYPE_ERR("wrong number of arguments");
                return 0;
        }
        if (nkwds_found != nkwds) {
            TYPE_ERR("wrong number of keyword arguments");
            return 0;
        }
        if (nargs + nkwds_found > 3) {
            TYPE_ERR("too many arguments");
            return 0;
        }
    }
    else {
        switch (nargs) {
            case 3:
                *axis = PyTuple_GET_ITEM(args, 2);
            case 2:
                *n = PyTuple_GET_ITEM(args, 1);
                *arr = PyTuple_GET_ITEM(args, 0);
                break;
            default:
                TYPE_ERR("wrong number of arguments");
                return 0;
        }
    }

    return 1;

}

static BN_INLINE int
parse_rankdata(PyObject *args,
               PyObject *kwds,
               PyObject **arr,
               PyObject **axis)
{
    const Py_ssize_t nargs = PyTuple_GET_SIZE(args);
    const Py_ssize_t nkwds = kwds == NULL ? 0 : PyDict_Size(kwds);
    if (nkwds) {
        int nkwds_found = 0;
        PyObject *tmp;
        switch (nargs) {
            case 1: *arr = PyTuple_GET_ITEM(args, 0);
            case 0: break;
            default:
                TYPE_ERR("wrong number of arguments");
                return 0;
        }
        switch (nargs) {
            case 0:
                *arr = PyDict_GetItem(kwds, pystr_arr);
                if (*arr == NULL) {
                    TYPE_ERR("Cannot find `arr` keyword input");
                    return 0;
                }
                nkwds_found += 1;
            case 1:
                tmp = PyDict_GetItem(kwds, pystr_axis);
                if (tmp != NULL) {
                    *axis = tmp;
                    nkwds_found++;
                }
                break;
            default:
                TYPE_ERR("wrong number of arguments");
                return 0;
        }
        if (nkwds_found != nkwds) {
            TYPE_ERR("wrong number of keyword arguments");
            return 0;
        }
        if (nargs + nkwds_found > 2) {
            TYPE_ERR("too many arguments");
            return 0;
        }
    }
    else {
        switch (nargs) {
            case 2:
                *axis = PyTuple_GET_ITEM(args, 1);
            case 1:
                *arr = PyTuple_GET_ITEM(args, 0);
                break;
            default:
                TYPE_ERR("wrong number of arguments");
                return 0;
        }
    }

    return 1;

}

static BN_INLINE int
parse_push(PyObject *args,
           PyObject *kwds,
           PyObject **arr,
           PyObject **n,
           PyObject **axis)
{
    const Py_ssize_t nargs = PyTuple_GET_SIZE(args);
    const Py_ssize_t nkwds = kwds == NULL ? 0 : PyDict_Size(kwds);
    if (nkwds) {
        int nkwds_found = 0;
        PyObject *tmp;
        switch (nargs) {
            case 2: *n = PyTuple_GET_ITEM(args, 1);
            case 1: *arr = PyTuple_GET_ITEM(args, 0);
            case 0: break;
            default:
                TYPE_ERR("wrong number of arguments");
                return 0;
        }
        switch (nargs) {
            case 0:
                *arr = PyDict_GetItem(kwds, pystr_arr);
                if (*arr == NULL) {
                    TYPE_ERR("Cannot find `arr` keyword input");
                    return 0;
                }
                nkwds_found += 1;
            case 1:
                tmp = PyDict_GetItem(kwds, pystr_n);
                if (tmp != NULL) {
                    *n = tmp;
                    nkwds_found++;
                }
            case 2:
                tmp = PyDict_GetItem(kwds, pystr_axis);
                if (tmp != NULL) {
                    *axis = tmp;
                    nkwds_found++;
                }
                break;
            default:
                TYPE_ERR("wrong number of arguments");
                return 0;
        }
        if (nkwds_found != nkwds) {
            TYPE_ERR("wrong number of keyword arguments");
            return 0;
        }
        if (nargs + nkwds_found > 3) {
            TYPE_ERR("too many arguments");
            return 0;
        }
    }
    else {
        switch (nargs) {
            case 3:
                *axis = PyTuple_GET_ITEM(args, 2);
            case 2:
                *n = PyTuple_GET_ITEM(args, 1);
            case 1:
                *arr = PyTuple_GET_ITEM(args, 0);
                break;
            default:
                TYPE_ERR("wrong number of arguments");
                return 0;
        }
    }

    return 1;

}

static PyObject *
nonreducer_axis(char *name,
                PyObject *args,
                PyObject *kwds,
                nra_t nra_float64,
                nra_t nra_float32,
                nra_t nra_int64,
                nra_t nra_int32,
                parse_type parse)
{

    int n;
    int axis;
    int dtype;

    PyArrayObject *a;

    PyObject *arr_obj = NULL;
    PyObject *n_obj = NULL;
    PyObject *axis_obj = NULL;

    if (parse == PARSE_PARTSORT) {
        if (!parse_partsort(args, kwds, &arr_obj, &n_obj, &axis_obj)) {
            return NULL;
        }
    }
    else if (parse == PARSE_RANKDATA) {
        if (!parse_rankdata(args, kwds, &arr_obj, &axis_obj)) {
            return NULL;
        }
    }
    else if (parse == PARSE_PUSH) {
        if (!parse_push(args, kwds, &arr_obj, &n_obj, &axis_obj)) {
            return NULL;
        }
    }
    else {
        RUNTIME_ERR("Unknown parse type; please report error.");
    }

    /* convert to array if necessary */
    if PyArray_Check(arr_obj) {
        a = (PyArrayObject *)arr_obj;
    }
    else {
        a = (PyArrayObject *)PyArray_FROM_O(arr_obj);
        if (a == NULL) {
            return NULL;
        }
    }

    /* check for byte swapped input array */
    if PyArray_ISBYTESWAPPED(a) {
        return slow(name, args, kwds);
    }

    /* defend against the axis of negativity */
    if (axis_obj == NULL) {
        if (parse == PARSE_PARTSORT || parse == PARSE_PUSH) {
            axis = PyArray_NDIM(a) - 1;
            if (axis < 0) {
                PyErr_Format(PyExc_ValueError,
                             "axis(=%d) out of bounds", axis);
                return NULL;
            }
        }
        else {
            if (PyArray_NDIM(a) != 1) {
                a = (PyArrayObject *)PyArray_Ravel(a, NPY_ANYORDER);
            }
            axis = 0;
        }
    }
    else if (axis_obj == Py_None) {
        if (parse == PARSE_PUSH) {
            VALUE_ERR("`axis` cannot be None");
            return NULL;
        }
        if (PyArray_NDIM(a) != 1) {
            a = (PyArrayObject *)PyArray_Ravel(a, NPY_ANYORDER);
        }
        axis = 0;
    }
    else {
        axis = PyArray_PyIntAsInt(axis_obj);
        if (error_converting(axis)) {
            TYPE_ERR("`axis` must be an integer");
            return NULL;
        }
        if (axis < 0) {
            axis += PyArray_NDIM(a);
            if (axis < 0) {
                PyErr_Format(PyExc_ValueError,
                             "axis(=%d) out of bounds", axis);
                return NULL;
            }
        }
        else if (axis >= PyArray_NDIM(a)) {
            PyErr_Format(PyExc_ValueError, "axis(=%d) out of bounds", axis);
            return NULL;
        }
    }

    /* ddof */
    if (n_obj == NULL) {
        n = -1;
    }
    else {
        n = PyArray_PyIntAsInt(n_obj);
        if (error_converting(n)) {
            TYPE_ERR("`n` must be an integer");
            return NULL;
        }
        if (n < 0 && parse == PARSE_PUSH) {
            VALUE_ERR("`n` must be nonnegative");
            return NULL;
        }
    }

    dtype = PyArray_TYPE(a);
    if      (dtype == NPY_float64) return nra_float64(a, axis, n);
    else if (dtype == NPY_float32) return nra_float32(a, axis, n);
    else if (dtype == NPY_int64)   return nra_int64(a, axis, n);
    else if (dtype == NPY_int32)   return nra_int32(a, axis, n);
    else                           return slow(name, args, kwds);

}

/* docstrings ------------------------------------------------------------- */

static char nra_doc[] =
"Bottleneck non-reducing functions that operate along an axis.";

static char partsort_doc[] =
/* MULTILINE STRING BEGIN
partsort(arr, n, axis=-1)

Partial sorting of array elements along given axis.

A partially sorted array is one in which the `n` smallest values appear
(in any order) in the first `n` elements. The remaining largest elements
are also unordered. Due to the algorithm used (Wirth's method), the nth
smallest element is in its sorted position (at index `n-1`).

Shuffling the input array may change the output. The only guarantee is
that the first `n` elements will be the `n` smallest and the remaining
element will appear in the remainder of the output.

This functions is not protected against NaN. Therefore, you may get
unexpected results if the input contains NaN.

Parameters
----------
arr : array_like
    Input array. If `arr` is not an array, a conversion is attempted.
n : int
    The `n` smallest elements will appear (unordered) in the first `n`
    elements of the output array.
axis : {int, None}, optional
    Axis along which the partial sort is performed. The default (axis=-1)
    is to sort along the last axis.

Returns
-------
y : ndarray
    A partially sorted copy of the input array where the `n` smallest
    elements will appear (unordered) in the first `n` elements.

See Also
--------
bottleneck.argpartsort: Indices that would partially sort an array

Notes
-----
Unexpected results may occur if the input array contains NaN.

Examples
--------
Create a numpy array:

>>> a = np.array([1, 0, 3, 4, 2])

Partially sort array so that the first 3 elements are the smallest 3
elements (note, as in this example, that the smallest 3 elements may not
be sorted):

>>> bn.partsort(a, n=3)
array([1, 0, 2, 4, 3])

Now partially sort array so that the last 2 elements are the largest 2
elements:

>>> bn.partsort(a, n=a.shape[0]-2)
array([1, 0, 2, 3, 4])

MULTILINE STRING END */

static char argpartsort_doc[] =
/* MULTILINE STRING BEGIN
argpartsort(arr, n, axis=-1)

Return indices that would partially sort an array.

A partially sorted array is one in which the `n` smallest values appear
(in any order) in the first `n` elements. The remaining largest elements
are also unordered. Due to the algorithm used (Wirth's method), the nth
smallest element is in its sorted position (at index `n-1`).

Shuffling the input array may change the output. The only guarantee is
that the first `n` elements will be the `n` smallest and the remaining
element will appear in the remainder of the output.

This functions is not protected against NaN. Therefore, you may get
unexpected results if the input contains NaN.

Parameters
----------
arr : array_like
    Input array. If `arr` is not an array, a conversion is attempted.
n : int
    The indices of the `n` smallest elements will appear in the first `n`
    elements of the output array along the given `axis`.
axis : {int, None}, optional
    Axis along which the partial sort is performed. The default (axis=-1)
    is to sort along the last axis.

Returns
-------
y : ndarray
    An array the same shape as the input array containing the indices
    that partially sort `arr` such that the `n` smallest elements will
    appear (unordered) in the first `n` elements.

See Also
--------
bottleneck.partsort: Partial sorting of array elements along given axis.

Notes
-----
Unexpected results may occur if the input array contains NaN.

Examples
--------
Create a numpy array:

>>> a = np.array([1, 0, 3, 4, 2])

Find the indices that partially sort that array so that the first 3
elements are the smallest 3 elements:

>>> index = bn.argpartsort(a, n=3)
>>> index
array([0, 1, 4, 3, 2])

Let's use the indices to partially sort the array (note, as in this
example, that the smallest 3 elements may not be in order):

>>> a[index]
array([1, 0, 2, 4, 3])

MULTILINE STRING END */

static char rankdata_doc[] =
/* MULTILINE STRING BEGIN
rankdata(arr, axis=None)

Ranks the data, dealing with ties appropriately.

Equal values are assigned a rank that is the average of the ranks that
would have been otherwise assigned to all of the values within that set.
Ranks begin at 1, not 0.

Parameters
----------
arr : array_like
    Input array. If `arr` is not an array, a conversion is attempted.
axis : {int, None}, optional
    Axis along which the elements of the array are ranked. The default
    (axis=None) is to rank the elements of the flattened array.

Returns
-------
y : ndarray
    An array with the same shape as `arr`. The dtype is 'float64'.

See also
--------
bottleneck.nanrankdata: Ranks the data dealing with ties and NaNs.

Examples
--------
>>> bn.rankdata([0, 2, 2, 3])
array([ 1. ,  2.5,  2.5,  4. ])
>>> bn.rankdata([[0, 2], [2, 3]])
array([ 1. ,  2.5,  2.5,  4. ])
>>> bn.rankdata([[0, 2], [2, 3]], axis=0)
array([[ 1.,  1.],
       [ 2.,  2.]])
>>> bn.rankdata([[0, 2], [2, 3]], axis=1)
array([[ 1.,  2.],
       [ 1.,  2.]])

MULTILINE STRING END */

static char nanrankdata_doc[] =
/* MULTILINE STRING BEGIN
nanrankdata(arr, axis=None)

Ranks the data, dealing with ties and NaNs appropriately.

Equal values are assigned a rank that is the average of the ranks that
would have been otherwise assigned to all of the values within that set.
Ranks begin at 1, not 0.

NaNs in the input array are returned as NaNs.

Parameters
----------
arr : array_like
    Input array. If `arr` is not an array, a conversion is attempted.
axis : {int, None}, optional
    Axis along which the elements of the array are ranked. The default
    (axis=None) is to rank the elements of the flattened array.

Returns
-------
y : ndarray
    An array with the same shape as `arr`. The dtype is 'float64'.

See also
--------
bottleneck.rankdata: Ranks the data, dealing with ties and appropriately.

Examples
--------
>>> bn.nanrankdata([np.nan, 2, 2, 3])
array([ nan,  1.5,  1.5,  3. ])
>>> bn.nanrankdata([[np.nan, 2], [2, 3]])
array([ nan,  1.5,  1.5,  3. ])
>>> bn.nanrankdata([[np.nan, 2], [2, 3]], axis=0)
array([[ nan,   1.],
       [  1.,   2.]])
>>> bn.nanrankdata([[np.nan, 2], [2, 3]], axis=1)
array([[ nan,   1.],
       [  1.,   2.]])

MULTILINE STRING END */

static char push_doc[] =
/* MULTILINE STRING BEGIN
push(arr, n=None, axis=-1)

Fill missing values (NaNs) with most recent non-missing values.

Filling proceeds along the specified axis from small index values to large
index values.

Parameters
----------
arr : array_like
    Input array. If `arr` is not an array, a conversion is attempted.
n : {int, None}, optional
    How far to push values. If the most recent non-NaN array element is
    more than `n` index positions away, than a NaN is returned. The default
    (n = None) is to push the entire length of the slice. If `n` is an integer
    it must be nonnegative.
axis : int, optional
    Axis along which the elements of the array are pushed. The default
    (axis=-1) is to push along the last axis of the input array.

Returns
-------
y : ndarray
    An array with the same shape and dtype as `arr`.

See also
--------
bottleneck.replace: Replace specified value of an array with new value.

Examples
--------
>>> arr = np.array([5, np.nan, np.nan, 6, np.nan])
>>> bn.push(arr)
    array([ 5.,  5.,  5.,  6.,  6.])
>>> bn.push(arr, n=1)
    array([  5.,   5.,  nan,   6.,   6.])
>>> bn.push(arr, n=2)
    array([ 5.,  5.,  5.,  6.,  6.])

MULTILINE STRING END */

/* python wrapper -------------------------------------------------------- */

static PyMethodDef
nra_methods[] = {
    {"partsort",    (PyCFunction)partsort,    VARKEY, partsort_doc},
    {"argpartsort", (PyCFunction)argpartsort, VARKEY, argpartsort_doc},
    {"rankdata",    (PyCFunction)rankdata,    VARKEY, rankdata_doc},
    {"nanrankdata", (PyCFunction)nanrankdata, VARKEY, nanrankdata_doc},
    {"push",        (PyCFunction)push,        VARKEY, push_doc},
    {NULL, NULL, 0, NULL}
};


#if PY_MAJOR_VERSION >= 3
static struct PyModuleDef
nra_def = {
   PyModuleDef_HEAD_INIT,
   "nonreduce_axis",
   nra_doc,
   -1,
   nra_methods
};
#endif


PyMODINIT_FUNC
#if PY_MAJOR_VERSION >= 3
#define RETVAL m
PyInit_nonreduce_axis(void)
#else
#define RETVAL
initnonreduce_axis(void)
#endif
{
    #if PY_MAJOR_VERSION >=3
        PyObject *m = PyModule_Create(&nra_def);
    #else
        PyObject *m = Py_InitModule3("nonreduce_axis", nra_methods, nra_doc);
    #endif
    if (m == NULL) return RETVAL;
    import_array();
    if (!intern_strings()) {
        return RETVAL;
    }
    return RETVAL;
}