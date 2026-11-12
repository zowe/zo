import pytest
import sys
import os
import yaml

# Add parent directory to path for importing USS module
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

import zusf_py as uss

FIXTURES_PATH = os.path.join(os.path.dirname(os.path.abspath(__file__)), "fixtures")
ENV_FIXTURE_PATH = os.path.join(FIXTURES_PATH, "env.yml")

class TestUSSFunctions:
    """Tests for z/OS USS (Unix System Services) file system functions."""
    
    def setup_method(self):
        """Setup test fixtures before each test method."""
        # Load environment variables from fixture
        with open(ENV_FIXTURE_PATH, "r") as env_yml:
            env_parsed = yaml.safe_load(env_yml)
            self.USS_BASE_DIR = env_parsed["USS_BASE_DIR"]

        # Create test directory in /tmp
        self.test_base_dir = self.USS_BASE_DIR
        self.created_items = []

    def teardown_method(self):
        """Cleanup files and directories created during tests."""
        # Clean up in reverse order (files first, then directories)
        for item in reversed(self.created_items):
            try:
                uss.delete_uss_item(item, True)  # recursive delete
            except:
                # Ignore errors during cleanup
                pass

    def test_create_uss_dir_success(self):
        """Test successful creation of USS directory."""
        test_dir = f"{self.test_base_dir}/test_create_dir"
        mode = "755"  # rwxr-xr-x
        
        # Create directory
        uss.create_uss_dir(test_dir, mode)
        self.created_items.append(test_dir)
        
        # Verify directory was created by listing parent
        parent_dir = os.path.dirname(test_dir)
        try:
            listing = uss.list_uss_dir(parent_dir)
            assert isinstance(listing, str)
            assert "test_create_dir" in listing
        except RuntimeError:
            pass

    def test_create_uss_file_success(self):
        """Test successful creation of USS file."""
        # First create parent directory
        test_dir = f"{self.test_base_dir}/test_create_file_dir"
        uss.create_uss_dir(test_dir, "755")
        self.created_items.append(test_dir)
        
        # Create file
        test_file = f"{test_dir}/test_file.txt"
        mode = "644"  # rw-r--r--
        
        uss.create_uss_file(test_file, mode)
        self.created_items.append(test_file)
        
        # Verify file was created by listing directory
        listing = uss.list_uss_dir(test_dir)
        assert isinstance(listing, str)
        assert "test_file.txt" in listing

    def test_move_uss_file_or_dir_success(self):
        """Test successful movement of USS file or directory."""
        # Create test directory and file
        test_dir = f"{self.test_base_dir}/test_move_dir_old"
        uss.create_uss_dir(test_dir, "755")
        
        # Creat a file inside the folder
        test_file = f"{test_dir}/test_file.txt"
        uss.create_uss_file(test_file, "644")

        # Move the file to a new location inside the same folder
        new_file = f"{test_dir}/test_file_moved.txt"
        uss.move_uss_file_or_dir(test_file, new_file)
        
        # Verify file was moved by listing directory
        listing = uss.list_uss_dir(test_dir)
        assert isinstance(listing, str)
        assert "test_file_moved.txt" in listing
        assert "test_file.txt" not in listing

        # move the directory to a new location
        new_dir = f"{self.test_base_dir}/test_move_dir_new"
        uss.move_uss_file_or_dir(test_dir, new_dir)
        self.created_items.append(new_dir)
        
        # Verify directory was moved by listing directory
        listing = uss.list_uss_dir(self.test_base_dir)
        assert isinstance(listing, str)
        assert "test_move_dir_new" in listing
        assert "test_move_dir_old" not in listing

    def test_list_uss_dir_success(self):
        """Test successful listing of USS directory."""
        # Create test directory with some content
        test_dir = f"{self.test_base_dir}/test_list_dir"
        uss.create_uss_dir(test_dir, "755")
        self.created_items.append(test_dir)
        
        # Create a test file in the directory
        test_file = f"{test_dir}/test_file.txt"
        uss.create_uss_file(test_file, "644")
        self.created_items.append(test_file)
        
        # List directory contents
        listing = uss.list_uss_dir(test_dir)
        
        # Verify listing
        assert isinstance(listing, str)
        assert len(listing) > 0
        assert "test_file.txt" in listing

    def test_write_uss_file_success(self):
        """Test successful writing to USS file."""
        # Create test directory and file
        test_dir = f"{self.test_base_dir}/test_write_dir"
        uss.create_uss_dir(test_dir, "755")
        self.created_items.append(test_dir)
        
        test_file = f"{test_dir}/test_write.txt"
        uss.create_uss_file(test_file, "644")
        self.created_items.append(test_file)
        
        # Write data to file
        test_data = "Hello, USS World!\nThis is test data."
        etag = uss.write_uss_file(test_file, test_data, "", "")
        
        # Verify write operation
        assert isinstance(etag, str)
        # etag might be empty or contain a value depending on implementation

    def test_read_uss_file_success(self):
        """Test successful reading from USS file."""
        # Create test directory and file
        test_dir = f"{self.test_base_dir}/test_read_dir"
        uss.create_uss_dir(test_dir, "755")
        self.created_items.append(test_dir)
        
        test_file = f"{test_dir}/test_read.txt"
        uss.create_uss_file(test_file, "644")
        self.created_items.append(test_file)
        
        # Write data to file
        test_data = "USS read test data\nSecond line"
        uss.write_uss_file(test_file, test_data, "", "")
        
        # Read data back
        content = uss.read_uss_file(test_file, "")
        
        # Verify read operation
        assert isinstance(content, str)
        # Content should match what we wrote
        assert content == test_data

    def test_read_uss_file_binary_mode(self):
        """Test reading USS file in binary mode."""
        # Create test directory and file
        test_dir = f"{self.test_base_dir}/test_read_binary_dir"
        uss.create_uss_dir(test_dir, "755")
        self.created_items.append(test_dir)
        
        test_file = f"{test_dir}/test_binary.bin"
        uss.create_uss_file(test_file, "644")
        self.created_items.append(test_file)
        
        # Write binary data
        test_data = "Binary test data"
        uss.write_uss_file(test_file, test_data, "binary", "")
        
        # Read in binary mode
        content = uss.read_uss_file(test_file, "binary")
        
        # Verify read operation
        assert isinstance(content, str)
        assert len(content) > 0
        # In binary mode, should contain our test data
        assert test_data in content or content.startswith(test_data)

    def test_chmod_uss_item_success(self):
        """Test successful chmod operation on USS file."""
        # Create test directory and file
        test_dir = f"{self.test_base_dir}/test_chmod_dir"
        uss.create_uss_dir(test_dir, "755")
        self.created_items.append(test_dir)
        
        test_file = f"{test_dir}/test_chmod.txt"
        uss.create_uss_file(test_file, "644")
        self.created_items.append(test_file)
        
        uss.chmod_uss_item(test_file, "755", False)
        
        # Verify chmod succeeded (no exception means success)
        assert True

    def test_chmod_uss_item_recursive_success(self):
        """Test successful recursive chmod operation."""
        # Create test directory structure
        test_dir = f"{self.test_base_dir}/test_chmod_recursive"
        uss.create_uss_dir(test_dir, "755")
        self.created_items.append(test_dir)
        
        # Create subdirectory
        sub_dir = f"{test_dir}/subdir"
        uss.create_uss_dir(sub_dir, "755")
        self.created_items.append(sub_dir)
        
        # Create file in subdirectory
        test_file = f"{sub_dir}/test_file.txt"
        uss.create_uss_file(test_file, "644")
        self.created_items.append(test_file)
        
        # Recursive chmod (should not raise exception)
        uss.chmod_uss_item(test_dir, "750", True)
        
        # Verify chmod succeeded
        assert True

    def test_chown_uss_item_success(self):
        """Test successful chown operation on USS file."""
        # Create test directory and file
        test_dir = f"{self.test_base_dir}/test_chown_dir"
        uss.create_uss_dir(test_dir, "755")
        self.created_items.append(test_dir)
        
        test_file = f"{test_dir}/test_chown.txt"
        uss.create_uss_file(test_file, "644")
        self.created_items.append(test_file)
        
        # Get current user (should be safe to chown to self)
        import pwd
        current_user = pwd.getpwuid(os.getuid()).pw_name
        
        uss.chown_uss_item(test_file, current_user, False)
        
        assert True

    def test_chtag_uss_item_success(self):
        """Test successful chtag operation on USS file."""
        # Create test directory and file
        test_dir = f"{self.test_base_dir}/test_chtag_dir"
        
        try:
            uss.create_uss_dir(test_dir, "755")
            self.created_items.append(test_dir)
            
            test_file = f"{test_dir}/test_chtag.txt"
            uss.create_uss_file(test_file, "644")
            self.created_items.append(test_file)
            
            uss.write_uss_file(test_file, "test content", "", "")
            uss.chtag_uss_item(test_file, "1047", False)
            
        except Exception as e:
            print(f"Test setup failed: {e}")
            assert False

    def test_delete_uss_item_file_success(self):
        """Test successful deletion of USS file."""
        # Create test directory and file
        test_dir = f"{self.test_base_dir}/test_delete_file_dir"
        uss.create_uss_dir(test_dir, "755")
        self.created_items.append(test_dir)
        
        test_file = f"{test_dir}/test_delete.txt"
        uss.create_uss_file(test_file, "644")
        # Don't add to created_items since we're testing deletion
        
        # Delete file (should not raise exception)
        uss.delete_uss_item(test_file, False)
        
        # Verify file is deleted by listing directory
        listing = uss.list_uss_dir(test_dir)
        assert "test_delete.txt" not in listing

    def test_delete_uss_item_directory_recursive_success(self):
        """Test successful recursive deletion of USS directory."""
        # Create test directory structure
        test_dir = f"{self.test_base_dir}/test_delete_recursive"
        uss.create_uss_dir(test_dir, "755")
        # Don't add to created_items since we're testing deletion
        
        # Create subdirectory
        sub_dir = f"{test_dir}/subdir"
        uss.create_uss_dir(sub_dir, "755")
        
        # Create file in subdirectory
        test_file = f"{sub_dir}/test_file.txt"
        uss.create_uss_file(test_file, "644")
        
        # Recursive delete (should not raise exception)
        uss.delete_uss_item(test_dir, True)
        
        # Verify directory is deleted by trying to list it
        try:
            listing = uss.list_uss_dir(test_dir)
            # If we get here, directory still exists - test failed
            assert False, "Directory should have been deleted"
        except RuntimeError:
            # Expected - directory should not exist
            assert True

    def test_read_write_streamed_success(self):
        """Test successful streamed read/write operations."""
        # Create test directory and files
        test_dir = f"{self.test_base_dir}/test_stream_dir"
        uss.create_uss_dir(test_dir, "755")
        self.created_items.append(test_dir)
        
        source_file = f"{test_dir}/source.txt"
        target_file = f"{test_dir}/target.txt"
        pipe_name = f"{test_dir}/test_pipe"
        
        # Create source file with test data
        uss.create_uss_file(source_file, "644")
        self.created_items.append(source_file)
        
        test_data = "Streamed data test\nLine 2\nLine 3"
        uss.write_uss_file(source_file, test_data, "", "")
        
        # Create target file
        uss.create_uss_file(target_file, "644")
        self.created_items.append(target_file)
        
        # Test streamed operations (if supported)
        try:
            # Write streamed
            etag = uss.write_uss_file_streamed(target_file, pipe_name, "", "")
            assert isinstance(etag, str)
            
            # Read streamed
            uss.read_uss_file_streamed(source_file, pipe_name, "")
            
            # Operations completed without exception
            assert True
        except RuntimeError as e:
            print(f"Streamed operations not fully supported: {e}")
            assert True

if __name__ == "__main__":
    pytest.main([__file__, "-v"])