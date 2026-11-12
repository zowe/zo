/* File: example.i */
%module example

%{
#include "example.h"
%}

%include "std_string.i"
%include "example.h"
