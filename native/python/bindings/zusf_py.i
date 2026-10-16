%module zusf_py

%{
#include "zusf_py.hpp"
%}

%include "exception.i"

%exception {
    try {
        $action
    } catch (const std::runtime_error& e) {
        SWIG_exception(SWIG_RuntimeError, e.what());
    } catch (const std::exception& e) {
        SWIG_exception(SWIG_RuntimeError, e.what());
    } catch (...) {
        SWIG_exception(SWIG_RuntimeError, "Unknown exception");
    }
}

%feature("docstring") create_uss_file "Create a new USS file with specified permissions.";
%feature("docstring") create_uss_dir "Create a new USS directory with specified permissions.";
%feature("docstring") list_uss_dir "List contents of a USS directory.";
%feature("docstring") read_uss_file "Read content from a USS file with optional encoding.";
%feature("docstring") move_uss_file_or_dir "Move a USS file or directory.";
%feature("docstring") read_uss_file_streamed "Read USS file content to a pipe in streaming mode.";
%feature("docstring") write_uss_file "Write data to a USS file with optional encoding and etag validation.";
%feature("docstring") write_uss_file_streamed "Write data from a pipe to a USS file in streaming mode.";
%feature("docstring") chmod_uss_item "Change permissions of a USS file or directory.";
%feature("docstring") delete_uss_item "Delete a USS file or directory with optional recursion.";
%feature("docstring") chown_uss_item "Change ownership of a USS file or directory.";
%feature("docstring") chtag_uss_item "Change file tag of a USS file or directory.";

%include "std_string.i"

struct ListOptions {
    bool all_files;
    bool long_format;
    int max_depth;
    explicit ListOptions(bool all_files = false, bool long_format = false, int max_depth = 1);
};

%include "zusf_py.hpp"
