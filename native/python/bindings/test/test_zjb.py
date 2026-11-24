import pytest
import sys
import os
import yaml
import time

# Add parent directory to path for importing job module
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
import zjb_py as jb

FIXTURES_PATH = os.path.join(os.path.dirname(os.path.abspath(__file__)), "fixtures")
ENV_FIXTURE_PATH = os.path.join(FIXTURES_PATH, "env.yml")

class TestJobFunctions:
    """Tests for z/OS job management functions."""
    
    def setup_method(self):
        """Setup test fixtures before each test method."""
        # Load environment variables from fixture
        with open(ENV_FIXTURE_PATH, "r") as env_yml:
            env_parsed = yaml.safe_load(env_yml)
            self.OWNER = env_parsed["OWNER"]

        self.submitted_jobs = []  # Track jobs we submit for cleanup

    def teardown_method(self):
        """Cleanup jobs submitted during tests."""
        for jobid in self.submitted_jobs:
            try:
                jb.delete_job(jobid)
            except:
                # Ignore errors during cleanup
                pass

    def test_list_jobs_by_owner_success(self):
        """Test successful listing of jobs by owner."""
        # List jobs for the test owner
        jobs = jb.list_jobs_by_owner(self.OWNER)
        
        # Verify response structure
        # assert isinstance(jobs, (list, jb.ZJobVector))
        
        # If jobs exist, verify structure
        if len(jobs) > 0:
            job = jobs[0]
            assert hasattr(job, 'jobname')
            assert hasattr(job, 'jobid')
            assert hasattr(job, 'owner')
            assert hasattr(job, 'status')
            assert hasattr(job, 'full_status')
            assert hasattr(job, 'retcode')
            assert hasattr(job, 'correlator')
            
            # Verify data types
            assert isinstance(job.jobname, str)
            assert isinstance(job.jobid, str)
            assert isinstance(job.owner, str)
            assert len(job.jobid) > 0

    def test_list_jobs_by_owner_with_prefix_and_status_success(self):
        """Test successful listing of jobs by owner with prefix filter."""
        prefix = "TSO"  # Common job prefix
        status = "ACTIVE"
        
        # List jobs with prefix filter
        jobs = jb.list_jobs_by_owner(self.OWNER, prefix, status)
        
        # Verify response structure
        # assert isinstance(jobs, (list, jb.ZJobVector))
        
        # If jobs exist, verify they match the prefix
        for job in jobs:
            assert hasattr(job, 'jobname')
            assert isinstance(job.jobname, str)
            # Job name should start with the prefix (case insensitive)
            if len(job.jobname.strip()) > 0:
                assert job.jobname.strip().upper().startswith(prefix.upper())
                assert job.status.strip().upper() == status.upper()

    def test_submit_job_success(self):
        """Test successful job submission."""
        # Simple test JCL that should run successfully
        test_jcl = """//TESTJOB JOB CLASS=A,MSGCLASS=H
//STEP1   EXEC PGM=IEFBR14
"""
        
        # Submit job
        jobid = jb.submit_job(test_jcl)
        
        # Verify job submission
        assert isinstance(jobid, str)
        assert len(jobid.strip()) > 0
        
        # Track for cleanup
        self.submitted_jobs.append(jobid)
        
        print(f"Submitted job with ID: {jobid}")

    def test_get_job_status_success(self):
        """Test successful retrieval of job status."""
        # First submit a job to get a valid job ID
        test_jcl = """//TESTJOB JOB CLASS=A,MSGCLASS=H
//STEP1   EXEC PGM=IEFBR14
"""
        
        jobid = jb.submit_job(test_jcl)
        self.submitted_jobs.append(jobid)
        
        # Get job status
        job = jb.get_job_status(jobid)
        
        # Verify job object structure
        assert hasattr(job, 'jobname')
        assert hasattr(job, 'jobid')
        assert hasattr(job, 'owner')
        assert hasattr(job, 'status')
        assert hasattr(job, 'full_status')
        assert hasattr(job, 'retcode')
        assert hasattr(job, 'correlator')
        
        # Verify job ID matches
        assert job.jobid.strip() == jobid.strip()
        
        # Verify data types
        assert isinstance(job.jobname, str)
        assert isinstance(job.status, str)
        assert len(job.status.strip()) > 0

    def test_list_spool_files_success(self):
        """Test successful listing of spool files for a job."""
        # Submit a job first
        test_jcl = """//TESTJOB JOB CLASS=A,MSGCLASS=H
//STEP1   EXEC PGM=IEFBR14
"""
        
        jobid = jb.submit_job(test_jcl)
        self.submitted_jobs.append(jobid)
        
        # Wait a moment for job to process (in real usage, you'd check job status)
        time.sleep(2)
        
        # List spool files
        spool_files = jb.list_spool_files(jobid)
        
        # Verify response structure
        # assert isinstance(spool_files, (list, jb.ZJobDDVector))
        
        # If spool files exist, verify structure
        if len(spool_files) > 0:
            spool_file = spool_files[0]
            assert hasattr(spool_file, 'jobid')
            assert hasattr(spool_file, 'ddn')
            assert hasattr(spool_file, 'dsn')
            assert hasattr(spool_file, 'stepname')
            assert hasattr(spool_file, 'procstep')
            assert hasattr(spool_file, 'key')
            
            # Verify data types
            assert isinstance(spool_file.jobid, str)
            assert isinstance(spool_file.ddn, str)
            assert isinstance(spool_file.key, int)
            
            # Job ID should match
            assert spool_file.jobid.strip() == jobid.strip()

    def test_read_spool_file_success(self):
        """Test successful reading of spool file content."""
        # Submit a job that generates output
        test_jcl = """//TESTJOB JOB CLASS=A,MSGCLASS=H
//STEP1   EXEC PGM=IEBGENER
//SYSPRINT DD SYSOUT=*
//SYSUT1   DD *
Hello World from Test Job
/*
//SYSUT2   DD SYSOUT=*
//SYSIN    DD DUMMY
"""
        
        jobid = jb.submit_job(test_jcl)
        self.submitted_jobs.append(jobid)
        
        # Wait for job to complete
        
        time.sleep(2)
        
        # Get spool files
        spool_files = jb.list_spool_files(jobid)
        
        if len(spool_files) > 0:
            # Read the first spool file
            key = spool_files[0].key
            content = jb.read_spool_file(jobid, key)
            
            # Verify content
            assert isinstance(content, str)
            assert len(content) > 0
            
            print(f"Spool file content preview: {content[:100]}...")

    def test_get_job_jcl_success(self):
        """Test successful retrieval of job JCL."""
        # Submit a job first
        test_jcl = """//TESTJOB JOB CLASS=A,MSGCLASS=H
//STEP1   EXEC PGM=IEFBR14
"""
        
        jobid = jb.submit_job(test_jcl)
        self.submitted_jobs.append(jobid)
        
        # Wait for job to complete
        time.sleep(2)

        # Get job JCL
        jcl_content = jb.get_job_jcl(jobid)
        
        # Verify JCL content
        assert isinstance(jcl_content, str)
        assert len(jcl_content.strip()) > 0
        
        # Should contain job name
        assert "TESTJOB" in jcl_content.upper()
        
        print(f"Retrieved JCL preview: {jcl_content[:100]}...")

    def test_delete_job_success(self):
        """Test successful job deletion."""
        # Submit a job first
        test_jcl = """//TESTJOB JOB CLASS=A,MSGCLASS=H
//STEP1   EXEC PGM=IEFBR14
"""
        
        jobid = jb.submit_job(test_jcl)
        
        # Wait for job to complete
        time.sleep(2)

        # Delete the job
        result = jb.delete_job(jobid)
        
        # Verify deletion
        assert isinstance(result, bool)
        assert result == True
        
        # Try to get job status - should fail or show job as deleted
        try:
            job = jb.get_job_status(jobid)
            print(f"Job status after deletion: {job.status}")
        except RuntimeError as e:
            print(f"Job correctly deleted - get_job_status failed: {e}")

    def test_job_workflow_integration(self):
        """Test complete job workflow: submit -> status -> spool -> delete."""
        # Submit job
        test_jcl = """//TESTJOB JOB CLASS=A,MSGCLASS=H
//STEP1   EXEC PGM=IEFBR14
"""
        
        jobid = jb.submit_job(test_jcl)
        assert len(jobid.strip()) > 0
        
        # Wait for job to complete
        time.sleep(2)

        # Check status
        job = jb.get_job_status(jobid)
        assert job.jobid.strip() == jobid.strip()
        
        # List spool files
        spool_files = jb.list_spool_files(jobid)
        # assert isinstance(spool_files, (list, jb.ZJobDDVector))
        
        # Get JCL
        jcl = jb.get_job_jcl(jobid)
        assert isinstance(jcl, str)
        assert len(jcl) > 0
        
        # Delete job
        result = jb.delete_job(jobid)
        assert result == True
        
        print(f"Complete workflow test successful for job {jobid}")

if __name__ == "__main__":
    pytest.main([__file__, "-v"])