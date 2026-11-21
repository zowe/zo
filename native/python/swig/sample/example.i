/* File: example.i */
%define MODULEIMPORT
"
from _example_runtime import load_native as _load_native
$module = _load_native()
del _load_native
"
%enddef

%module(moduleimport=MODULEIMPORT) example

%{
#include "example.h"
%}

%include "std_string.i"
%include "example.h"
