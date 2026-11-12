%module zjb_py

%{
#include "zjb_py.hpp"
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

%include "std_string.i"
%include "std_vector.i"

%feature("docstring") list_jobs_by_owner "List all jobs owned by the specified user.";
%feature("docstring") get_job_status "Get the current status of a job by job ID.";
%feature("docstring") list_spool_files "List all spool files (DD statements) for a job.";
%feature("docstring") read_spool_file "Read the content of a specific spool file by job ID and key.";
%feature("docstring") get_job_jcl "Retrieve the JCL content for a job.";
%feature("docstring") submit_job "Submit JCL content and return the assigned job ID.";
%feature("docstring") delete_job "Delete a job from the system and return success status.";

%include "zjb_py.hpp"

%template(ZJobVector) std::vector<ZJob>;
%template(ZJobDDVector) std::vector<ZJobDD>;

struct ZJob {
    std::string jobname;
    std::string jobid;
    std::string owner;
    std::string status;
    std::string full_status;
    std::string retcode;
    std::string correlator;
    std::string exec_system;
    std::string exec_member;
    std::string exec_submitted;
    std::string exec_started;
    std::string exec_ended;
};

struct ZJobDD
{
  std::string jobid;
  std::string ddn;
  std::string dsn;
  std::string stepname;
  std::string procstep;
  int key;
};