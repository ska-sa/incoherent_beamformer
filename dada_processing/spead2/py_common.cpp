/* Copyright 2015 SKA South Africa
 *
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License as published by the Free
 * Software Foundation, either version 3 of the License, or (at your option) any
 * later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License for more
 * details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#define NPY_NO_DEPRECATED_API NPY_1_7_API_VERSION
#define PY_ARRAY_UNIQUE_SYMBOL spead2_ARRAY_API
#include <boost/python.hpp>
#include <boost/system/system_error.hpp>
#include <numpy/arrayobject.h>
#include <memory>
#include "py_common.h"
#include "common_ringbuffer.h"
#include "common_defines.h"
#include "common_flavour.h"
#include "common_logging.h"
#include "common_memory_pool.h"
#include "common_thread_pool.h"

namespace py = boost::python;

namespace spead2
{

class log_function_python
{
private:
    py::object logger;
public:
    typedef void result_type;

    explicit log_function_python(const py::object &logger) : logger(logger) {}

    void operator()(log_level level, const std::string &msg)
    {
        acquire_gil gil;

        static const char *const level_methods[] =
        {
            "warning",
            "info",
            "debug"
        };
        unsigned int level_idx = static_cast<unsigned int>(level);
        assert(level_idx < sizeof(level_methods) / sizeof(level_methods[0]));
        logger.attr(level_methods[level_idx])("%s", msg);
    }
};

static PyObject *ringbuffer_stopped_type;
static PyObject *ringbuffer_empty_type;

static void translate_exception(const ringbuffer_stopped &e)
{
    PyErr_SetString(ringbuffer_stopped_type, e.what());
}

static void translate_exception(const ringbuffer_empty &e)
{
    PyErr_SetString(ringbuffer_empty_type, e.what());
}

static void translate_exception_stop_iteration(const stop_iteration &e)
{
    PyErr_SetString(PyExc_StopIteration, e.what());
}

static void translate_exception_boost_io_error(const boost_io_error &e)
{
    py::tuple args = py::make_tuple(e.code().value(), e.what());
    PyErr_SetObject(PyExc_IOError, args.ptr());
}

static py::object descriptor_get_shape(const descriptor &d)
{
    py::list out;
    for (const auto &size : d.shape)
    {
        if (size >= 0)
            out.append(size);
        else
            out.append(py::object());
    }
    return out;
}

static void descriptor_set_shape(descriptor &d, py::object shape)
{
    std::vector<std::int64_t> out;
    out.reserve(len(shape));
    for (long i = 0; i < len(shape); i++)
    {
        py::object value = shape[i];
        if (value.is_none())
            out.push_back(-1);
        else
        {
            std::int64_t v = py::extract<int64_t>(value);
            // TODO: verify range (particularly, >= 0)
            out.push_back(v);
        }
    }
    d.shape = std::move(out);
}

static py::object descriptor_get_format(const descriptor &d)
{
    py::list out;
    for (const auto &item : d.format)
    {
        out.append(py::make_tuple(item.first, item.second));
    }
    return out;
}

static void descriptor_set_format(descriptor &d, py::object format)
{
    std::vector<std::pair<char, std::int64_t> > out;
    out.reserve(len(format));
    for (long i = 0; i < len(format); i++)
    {
        py::object item = format[i];
        if (len(item) != 2)
            throw std::length_error("expected 2 arguments in format");
        char code = py::extract<char>(item[0]);
        std::int64_t type = py::extract<std::int64_t>(item[1]);
        out.emplace_back(code, type);
    }
    d.format = std::move(out);
}

static py::object int_to_object(long ival)
{
#if PY_MAJOR_VERSION >= 3
    PyObject *obj = PyLong_FromLong(ival);
#else
    PyObject *obj = PyInt_FromLong(ival);
#endif
    if (!obj)
        py::throw_error_already_set();
    return py::object(py::handle<>(obj));
}

thread_pool_wrapper::~thread_pool_wrapper()
{
    stop();
}

void thread_pool_wrapper::stop()
{
    release_gil gil;
    thread_pool::stop();
}

template<typename T>
static void create_exception(PyObject *&type, const char *name, const char *basename)
{
    type = PyErr_NewException(const_cast<char *>(name), NULL, NULL);
    if (type == NULL)
        py::throw_error_already_set();
    py::scope().attr(basename) = py::handle<>(py::borrowed(type));
    py::register_exception_translator<T>((void (*)(const T &)) &translate_exception);
}

class bytestring_to_python
{
public:
    static PyObject *convert(const bytestring &s)
    {
#if PY_MAJOR_VERSION >= 3
        return PyBytes_FromStringAndSize(s.data(), s.size());
#else
        return PyString_FromStringAndSize(s.data(), s.size());
#endif
    }
};

class bytestring_from_python
{
public:
    bytestring_from_python()
    {
    }

    static void *convertible(PyObject *obj_ptr)
    {
#if PY_MAJOR_VERSION >= 3
        if (!PyBytes_Check(obj_ptr))
            return 0;
#else
        if (!PyString_Check(obj_ptr))
            return 0;
#endif
        return obj_ptr;
    }

    static void construct(
        PyObject *obj_ptr, py::converter::rvalue_from_python_stage1_data *data)
    {
        char *value;
        Py_ssize_t length;
#if PY_MAJOR_VERSION >= 3
        PyBytes_AsStringAndSize(obj_ptr, &value, &length);
#else
        PyString_AsStringAndSize(obj_ptr, &value, &length);
#endif
        if (PyErr_Occurred())
            throw py::error_already_set();
        void *storage = reinterpret_cast<py::converter::rvalue_from_python_storage<bytestring> *>(
            data)->storage.bytes;
        new (storage) bytestring(value, length);
        data->convertible = storage;
    }
};

static void register_module()
{
    using namespace boost::python;
    using namespace spead2;

    create_exception<ringbuffer_stopped>(ringbuffer_stopped_type, "spead2.Stopped", "Stopped");
    create_exception<ringbuffer_empty>(ringbuffer_empty_type, "spead2.Empty", "Empty");
    register_exception_translator<stop_iteration>(&translate_exception_stop_iteration);
    register_exception_translator<boost_io_error>(&translate_exception_boost_io_error);
    to_python_converter<bytestring, bytestring_to_python>();
    py::converter::registry::push_back(
        &bytestring_from_python::convertible,
        &bytestring_from_python::construct,
        py::type_id<bytestring>());

#define EXPORT_ENUM(x) (py::setattr(scope(), #x, int_to_object(long(x))))
    EXPORT_ENUM(BUG_COMPAT_DESCRIPTOR_WIDTHS);
    EXPORT_ENUM(BUG_COMPAT_SHAPE_BIT_1);
    EXPORT_ENUM(BUG_COMPAT_SWAP_ENDIAN);
    EXPORT_ENUM(BUG_COMPAT_PYSPEAD_0_5_2);

    EXPORT_ENUM(NULL_ID);
    EXPORT_ENUM(HEAP_CNT_ID);
    EXPORT_ENUM(HEAP_LENGTH_ID);
    EXPORT_ENUM(PAYLOAD_OFFSET_ID);
    EXPORT_ENUM(PAYLOAD_LENGTH_ID);
    EXPORT_ENUM(DESCRIPTOR_ID);
    EXPORT_ENUM(STREAM_CTRL_ID);

    EXPORT_ENUM(DESCRIPTOR_NAME_ID);
    EXPORT_ENUM(DESCRIPTOR_DESCRIPTION_ID);
    EXPORT_ENUM(DESCRIPTOR_SHAPE_ID);
    EXPORT_ENUM(DESCRIPTOR_FORMAT_ID);
    EXPORT_ENUM(DESCRIPTOR_ID_ID);
    EXPORT_ENUM(DESCRIPTOR_DTYPE_ID);

    EXPORT_ENUM(CTRL_STREAM_START);
    EXPORT_ENUM(CTRL_DESCRIPTOR_REISSUE);
    EXPORT_ENUM(CTRL_STREAM_STOP);
    EXPORT_ENUM(CTRL_DESCRIPTOR_UPDATE);

    EXPORT_ENUM(MEMCPY_STD);
    EXPORT_ENUM(MEMCPY_NONTEMPORAL);
#undef EXPORT_ENUM

    class_<flavour>("Flavour",
        init<int, int, int, bug_compat_mask>(
            (arg("version"), arg("item_pointer_bits"),
             arg("heap_address_bits"), arg("bug_compat")=0)))
        .def(init<>())
        .def(self == self)
        .def(self != self)
        .add_property("version", &flavour::get_version)
        .add_property("item_pointer_bits", &flavour::get_item_pointer_bits)
        .add_property("heap_address_bits", &flavour::get_heap_address_bits)
        .add_property("bug_compat", &flavour::get_bug_compat);

    class_<memory_pool, std::shared_ptr<memory_pool>, boost::noncopyable>(
        "MemoryPool",
        init<std::size_t, std::size_t, std::size_t, std::size_t>(
            (arg("lower"), arg("upper"), arg("max_free"), arg("initial"))));

    class_<thread_pool_wrapper, boost::noncopyable>("ThreadPool", init<int>(
            (arg("threads") = 1)))
        .def("stop", &thread_pool_wrapper::stop);

    class_<descriptor>("RawDescriptor")
        .def_readwrite("id", &descriptor::id)
        .add_property("name", make_bytestring_getter(&descriptor::name), make_bytestring_setter(&descriptor::name))
        .add_property("description", make_bytestring_getter(&descriptor::description), make_bytestring_setter(&descriptor::description))
        .add_property("shape", &descriptor_get_shape, &descriptor_set_shape)
        .add_property("format", &descriptor_get_format, &descriptor_set_format)
        .add_property("numpy_header", make_bytestring_getter(&descriptor::numpy_header), make_bytestring_setter(&descriptor::numpy_header))
    ;

    object logging_module = import("logging");
    object logger = logging_module.attr("getLogger")("spead2");
    set_log_function(log_function_python(logger));
}

} // namespace spead2

#include "py_recv.h"
#include "py_send.h"

/* Wrapper to deal with import_array returning nothing in Python 2, NULL in
 * Python 3.
 */
#if PY_MAJOR_VERSION >= 3
static void *call_import_array(bool &success)
#else
static void call_import_array(bool &success)
#endif
{
    success = false;
    import_array(); // This is a macro that might return
    success = true;
#if PY_MAJOR_VERSION >= 3
    return NULL;
#endif
}

BOOST_PYTHON_MODULE(_spead2)
{
    // Needed to make NumPy functions work
    bool numpy_imported = false;
    call_import_array(numpy_imported);
    if (!numpy_imported)
        py::throw_error_already_set();

    spead2::register_module();
    spead2::recv::register_module();
    spead2::send::register_module();
}
