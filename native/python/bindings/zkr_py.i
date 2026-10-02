%module zkr_py

%{
#include "zkr_py.hpp"
%}

// compactdefaultargs collapses a function with std::string-typed default arguments
// (format="pem", label="", ...) into one C++-side wrapper instead of SWIG's usual
// per-omitted-default overload set; without it, every multi-default function here
// (list_rings, list_certificates, connect_certificate, export_certificate*) gets a
// std::string default, only POD (bool/int) defaults like delete_certificate's
// database=False survive as a single overload. kwargs then makes the *generated
// Python* function take real named parameters (PyArg_ParseTupleAndKeywords) instead of
// a bare `def foo(*args)` dispatcher -- needed for both plain positional calls to have
// named-looking signatures and for the keyword-style calls the docstrings below
// document (e.g. format='p12').
%feature("compactdefaultargs");
%feature("kwargs");

%include "exception.i"
%include "std_string.i"
%include "std_vector.i"

// ZkrError (zkr_py.hpp) is a C++-only carrier for structured SAF/ESM/GSK codes, thrown and
// caught entirely on the C++ side of %exception below. Without this, SWIG's automatic class
// wrapping for this %include'd struct silently shadows the *real* zkr_py.ZkrError exception
// type that %init creates via PyErr_NewException, leaving a same-named plain-object class
// (bases=(object,)) that Python's `except zkr_py.ZkrError` then rejects.
%ignore ZkrError;

// ZkrBytes -- PKCS#12/PEM payloads as Python `bytes`, not `str`. Must be declared before
// %include "zkr_py.hpp" so it wins the typemap match over std_string.i's std::string
// typemaps (SWIG matches on the type as written, before reducing typedefs).
%{ static PyObject *g_zkr_error = nullptr; %}

%typemap(out) ZkrBytes {
  $result = PyBytes_FromStringAndSize($1.data(), static_cast<Py_ssize_t>($1.size()));
}

%typemap(in) const ZkrBytes & (std::string temp) {
  char *buf = nullptr;
  Py_ssize_t len = 0;
  if (PyBytes_AsStringAndSize($input, &buf, &len) != 0) {
    SWIG_exception_fail(SWIG_TypeError, "expected bytes for argument $argnum");
  }
  temp.assign(buf, static_cast<size_t>(len));
  $1 = &temp;
}

// The "in" typemap above points at a local temp with no SWIG_NEWOBJ bookkeeping, so
// there is nothing to free -- override (rather than inherit std_string.i's) freearg,
// which otherwise references a `resN` variable this typemap never declares.
%typemap(freearg) const ZkrBytes & ""

%typemap(typecheck, precedence=SWIG_TYPECHECK_STRING) const ZkrBytes & {
  $1 = PyBytes_Check($input) ? 1 : 0;
}

%init %{
  g_zkr_error = PyErr_NewException("zkr_py.ZkrError", PyExc_RuntimeError, nullptr);
  Py_INCREF(g_zkr_error);
  PyModule_AddObject(m, "ZkrError", g_zkr_error);
%}

// zkr_py.py only forwards symbols SWIG generated a proxy for (it does `from . import _zkr_py`,
// not a wildcard import), so ZkrError -- added straight into the _zkr_py C module above --
// needs an explicit forward to be reachable as zkr_py.ZkrError.
%pythoncode %{
ZkrError = _zkr_py.ZkrError
%}

%exception {
  try {
    $action
  } catch (const ZkrError &e) {
    PyObject *exc = PyObject_CallFunction(g_zkr_error, "s", e.what());
    if (exc) {
      PyObject_SetAttrString(exc, "service", PyUnicode_FromString(e.service.c_str()));
      PyObject_SetAttrString(exc, "function_code", PyLong_FromLong(e.function_code));
      PyObject_SetAttrString(exc, "saf_rc", PyLong_FromLong(e.saf_rc));
      PyObject_SetAttrString(exc, "esm_rc", PyLong_FromLong(e.esm_rc));
      PyObject_SetAttrString(exc, "esm_rsn", PyLong_FromLong(e.esm_rsn));
      PyObject_SetAttrString(exc, "gsk_rc", PyLong_FromLong(e.gsk_rc));
      PyErr_SetObject(g_zkr_error, exc);
      Py_DECREF(exc);
    }
    SWIG_fail;
  } catch (const std::invalid_argument &e) {
    SWIG_exception(SWIG_ValueError, e.what());
  } catch (const std::exception &e) {
    SWIG_exception(SWIG_RuntimeError, e.what());
  } catch (...) {
    SWIG_exception(SWIG_RuntimeError, "Unknown exception");
  }
}

%feature("docstring") create_keyring "Create a new key ring (R_datalib NEWRING). Returns a non-fatal SAF warning, or \"\".";
%feature("docstring") delete_keyring "Delete a key ring (R_datalib DELRING).";
%feature("docstring") list_rings "Enumerate a user's key rings and the certificates connected to each. keyring=\"\" (or \"*\") returns all of the owner's rings.";
%feature("docstring") count_ring "Count the certificates connected to a key ring, or the owner's whole virtual key ring (keyring=\"*\").";
%feature("docstring") refresh_digtcert "Refresh the DIGTCERT class (R_datalib REFRESH).";
%feature("docstring") list_certificates "List the certificates connected to a key ring, with RACDCERT LABEL-style exact filtering.";
%feature("docstring") show_certificate "Retrieve detailed information (subject, serial number, validity) for a single certificate.";
%feature("docstring") set_default_certificate "Make a certificate the default certificate of a key ring.";
%feature("docstring") connect_certificate "Connect a certificate that already exists in the ESM database to a key ring.";
%feature("docstring") delete_certificate "Remove/disconnect a certificate from a key ring or the ESM database (database=True).";
%feature("docstring") trust_certificate "Change a certificate's trust status (TRUST, HIGHTRUST, or NOTRUST).";
%feature("docstring") rename_certificate "Rename a certificate's label.";
%feature("docstring") export_certificate "Export a certificate as bytes: PEM text (ISO8859-1) by default, or a PKCS#12 bundle (format='p12', password required).";
%feature("docstring") export_certificate_to_file "Export a certificate to a private (0600) file; returns the number of bytes written. Bytes on disk stay EBCDIC (PEM) or binary (PKCS#12), byte-identical to keyring-util.";
%feature("docstring") export_certificate_to_dsn "Export a certificate to a sequential data set or PDS/E member; returns the number of bytes written.";
%feature("docstring") import_certificate "Import a certificate (and private key, when present) from PKCS#12 bytes into a key ring.";
%feature("docstring") import_certificate_from_file "Import a certificate from a PKCS#12 file into a key ring.";
%feature("docstring") import_certificate_from_dsn "Import a certificate from a PKCS#12 data set or PDS/E member into a key ring.";

%include "zkr_py.hpp"

%template(ZKRCertInfoVector) std::vector<ZKRCertInfo>;
%template(ZKRRingCertVector) std::vector<ZKRRingCert>;
%template(ZKRRingEntryVector) std::vector<ZKRRingEntry>;

struct ZKRCertInfo
{
  std::string label;
  std::string owner;
  std::string usage;
  std::string status;
  bool is_default;
};

struct ZKRCertDetail
{
  std::string label;
  std::string owner;
  std::string usage;
  std::string status;
  bool is_default;
  std::string subject;
  std::string record_id;
  int key_type;
  int key_size;
  std::string serial_number;
  std::string not_before;
  std::string not_after;
  bool has_validity;
};

struct ZKRRingCert
{
  std::string owner;
  std::string label;
};

struct ZKRRingEntry
{
  std::string owner;
  std::string name;
  std::vector<ZKRRingCert> certs;
};

// ZkrCertList / ZkrRingList are defined directly in zkr_py.hpp (not merely #included
// like the ZKR* structs above), so %include "zkr_py.hpp" already declared them --
// redeclaring here would just be a duplicate (SWIG warning 302).
