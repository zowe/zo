import pytest
import os
import yaml
import sys
sys.path.insert(0, '../')
import zds_py as ds

FIXTURES_PATH = os.path.join(os.path.dirname(os.path.abspath(__file__)), "fixtures")
ENV_FIXTURE_PATH = os.path.join(FIXTURES_PATH, "env.yml")

class TestDatasetFunctions:
    """Combined tests for all dataset functions."""
    
    def setup_method(self):
        """Setup test fixtures before each test method."""
        # Load environment variables from fixture
        with open(ENV_FIXTURE_PATH, "r") as env_yml:
            env_parsed = yaml.safe_load(env_yml)
            self.DSN_PREFIX = env_parsed["DSN_PREFIX"]

        # Set up test dataset base name
        self.test_dsn_base = self.DSN_PREFIX
        self.created_datasets = []

    def teardown_method(self):
        """Cleanup datasets created during tests."""
        for dsn in self.created_datasets:
            try:
                ds.delete_data_set(dsn)
            except:
                pass

    def _create_ps_attributes(self, dsn):
        """Helper to create PS (Sequential) dataset attributes."""
        attributes = ds.DS_ATTRIBUTES()
        attributes.alcunit = 'TRACKS'
        attributes.blksize = 6160
        attributes.dirblk = 0
        attributes.dsorg = 'PS'
        attributes.primary = 3
        attributes.recfm = 'FB'
        attributes.lrecl = 80
        attributes.dataclass = ''
        attributes.unit = 'SYSDA'
        attributes.dsntype = 'BASIC'
        attributes.mgntclass = ''
        attributes.dsname = dsn
        attributes.avgblk = 0
        attributes.secondary = 1
        attributes.size = 0
        attributes.storclass = ''
        attributes.vol = ''
        return attributes

    def _create_po_attributes(self, dsn):
        """Helper to create PO (Partitioned) dataset attributes."""
        attributes = ds.DS_ATTRIBUTES()
        attributes.alcunit = 'TRACKS'
        attributes.blksize = 6160
        attributes.dirblk = 10
        attributes.dsorg = 'PO'
        attributes.primary = 5
        attributes.recfm = 'FB'
        attributes.lrecl = 80
        attributes.dataclass = ''
        attributes.unit = 'SYSDA'
        attributes.dsntype = 'BASIC'
        attributes.mgntclass = ''
        attributes.dsname = dsn
        attributes.avgblk = 0
        attributes.secondary = 1
        attributes.size = 0
        attributes.storclass = ''
        attributes.vol = ''
        return attributes

    def _find_dataset_in_list(self, datasets, dsn):
        """Helper to find a specific dataset in a list of datasets."""
        for dataset in datasets:
            if dataset.name.strip() == dsn:
                return dataset
        return None

    def _find_member_in_list(self, members, member_name):
        """Helper to find a specific member in a list of members."""
        for member in members:
            if member.name.strip() == member_name:
                return member
        return None

    # CREATE DATASET TESTS
    def test_create_partitioned_dataset_success(self):
        """Test successful creation of a partitioned dataset (PO)."""
        dsn = f"{self.test_dsn_base}.CREATE.PO"
        po_attributes = self._create_po_attributes(dsn)
        
        # Create partitioned dataset
        ds.create_data_set(dsn, po_attributes)
        self.created_datasets.append(dsn)
        
        # Verify creation by listing datasets
        datasets = ds.list_data_sets(dsn, True)
        # assert isinstance(datasets, (list, ds.ZDSEntryVector))
        assert len(datasets) > 0
        
        found_dataset = self._find_dataset_in_list(datasets, dsn)
        assert found_dataset is not None, f"Dataset {dsn} not found"
        assert found_dataset.dsorg.strip() == 'PO'

    def test_create_sequential_dataset_success(self):
        """Test successful creation of a sequential dataset (PS)."""
        dsn = f"{self.test_dsn_base}.CREATE.PS"
        ps_attributes = self._create_ps_attributes(dsn)
        
        # Create sequential dataset
        ds.create_data_set(dsn, ps_attributes)
        self.created_datasets.append(dsn)
        
        # Verify creation by listing datasets
        datasets = ds.list_data_sets(dsn, True)
        # assert isinstance(datasets, (list, ds.ZDSEntryVector))
        assert len(datasets) > 0
        
        found_dataset = self._find_dataset_in_list(datasets, dsn)
        assert found_dataset is not None, f"Dataset {dsn} not found"
        assert found_dataset.dsorg.strip() == 'PS'

    def test_create_dataset_fail_existing_name(self):
        """Test that creating a dataset with an existing name fails."""
        dsn = f"{self.test_dsn_base}.CREATE.DUP"
        duplicate_attributes = self._create_ps_attributes(dsn)
        
        # Create the dataset first time - should succeed
        ds.create_data_set(dsn, duplicate_attributes)
        self.created_datasets.append(dsn)
        
        # Try to create the same dataset again - should raise an exception
        try:
            ds.create_data_set(dsn, duplicate_attributes)
            assert False, "Expected RuntimeError when creating duplicate dataset"
        except RuntimeError as e:
            # Expected - duplicate creation should fail
            pass
        except Exception as e:
            # Some other exception type was raised
            print(f"Unexpected exception type: {type(e).__name__}: {e}")
            raise

    # LIST DATASETS TESTS
    def test_list_datasets_success(self):
        """Test successful listing of datasets."""
        dsn = f"{self.test_dsn_base}.LIST.SUCCESS"
        attributes = self._create_ps_attributes(dsn)
        
        # Create test dataset
        ds.create_data_set(dsn, attributes)
        self.created_datasets.append(dsn)
        
        # List datasets
        datasets = ds.list_data_sets(dsn, True)
        
        # Verify response
        # assert isinstance(datasets, (list, ds.ZDSEntryVector))
        assert len(datasets) > 0
        
        # Verify our dataset is in results
        found_dataset = self._find_dataset_in_list(datasets, dsn)
        assert found_dataset is not None
        assert found_dataset.dsorg.strip() == 'PS'
        assert found_dataset.recfm.strip() == 'FB'
        assert hasattr(found_dataset, 'volser')
        assert hasattr(found_dataset, 'migrated')

    # CREATE MEMBER TESTS
    def test_create_member_success(self):
        """Test successful creation of a member in a PDS."""
        pds_dsn = f"{self.test_dsn_base}.MEMBER.PDS"
        member_name = "TESTMEM1"
        member_dsn = f"{pds_dsn}({member_name})"
        
        # Create PDS first
        pds_attributes = self._create_po_attributes(pds_dsn)
        ds.create_data_set(pds_dsn, pds_attributes)
        self.created_datasets.append(pds_dsn)
        
        # Create member
        ds.create_member(member_dsn)
        
        # Verify creation by listing members
        members = ds.list_members(pds_dsn)
        # assert isinstance(members, (list, ds.ZDSMemVector))
        assert len(members) > 0
        
        found_member = self._find_member_in_list(members, member_name)
        assert found_member is not None, f"Member {member_name} not found"

    # LIST MEMBERS TESTS
    def test_list_members_success(self):
        """Test successful listing of members in a PDS."""
        pds_dsn = f"{self.test_dsn_base}.LIST.MEMBERS"
        member_name = "LISTTEST"
        member_dsn = f"{pds_dsn}({member_name})"
        
        # Create PDS
        pds_attributes = self._create_po_attributes(pds_dsn)
        ds.create_data_set(pds_dsn, pds_attributes)
        self.created_datasets.append(pds_dsn)
        
        # Create a member
        ds.create_member(member_dsn)
        
        # List members
        members = ds.list_members(pds_dsn)
        
        # Verify response
        # assert isinstance(members, (list, ds.ZDSMemVector))
        assert len(members) > 0
        
        # Verify our member is in results
        found_member = self._find_member_in_list(members, member_name)
        assert found_member is not None
        assert hasattr(found_member, 'name')

    # READ DATASET TESTS
    def test_read_dataset_success(self):
        """Test successful reading of dataset content."""
        dsn = f"{self.test_dsn_base}.READ.SUCCESS"
        test_data = "Hello, World!\nThis is test data."
        
        # Create dataset
        attributes = self._create_ps_attributes(dsn)
        ds.create_data_set(dsn, attributes)
        self.created_datasets.append(dsn)
        
        # Write test data
        etag = ds.write_data_set(dsn, test_data, "", "")
        assert isinstance(etag, str)
        assert len(etag) > 0
        
        # Read data back
        content = ds.read_data_set(dsn, "")
        assert isinstance(content, str)
        assert content == test_data  + "\n"

    def test_read_dataset_without_codepage(self):
        """Test reading dataset without specifying codepage."""
        dsn = f"{self.test_dsn_base}.READ.NOCODE"
        test_data = "Test data without codepage"
        
        # Create dataset
        attributes = self._create_ps_attributes(dsn)
        ds.create_data_set(dsn, attributes)
        self.created_datasets.append(dsn)
        
        # Write test data
        ds.write_data_set(dsn, test_data, "", "")
        
        # Read data back without codepage
        content = ds.read_data_set(dsn, "")
        assert isinstance(content, str)
        assert content == test_data  + "\n"

    def test_read_dataset_binary_mode(self):
        """Test reading dataset in binary mode."""
        dsn = f"{self.test_dsn_base}.READ.BINARY"
        test_data = "Binary test data"
        
        # Create dataset
        attributes = self._create_ps_attributes(dsn)
        ds.create_data_set(dsn, attributes)
        self.created_datasets.append(dsn)
        
        # Write test data in binary mode
        ds.write_data_set(dsn, test_data, "binary", "")
        
        # Read data back in binary mode
        content = ds.read_data_set(dsn, "binary") 
        assert isinstance(content, str)
        assert content.startswith(test_data)

    # WRITE DATASET TESTS
    def test_write_dataset_success(self):
        """Test successful writing to dataset."""
        dsn = f"{self.test_dsn_base}.WRITE.SUCCESS"
        test_data = "This is test content for writing."
        
        # Create dataset
        attributes = self._create_ps_attributes(dsn)
        ds.create_data_set(dsn, attributes)
        self.created_datasets.append(dsn)
        
        # Write data
        etag = ds.write_data_set(dsn, test_data, "", "")
        
        # Verify write returned valid etag
        assert isinstance(etag, str)
        assert len(etag) > 0
        
        # Verify data was written correctly by reading it back
        content = ds.read_data_set(dsn, "")
        assert content == test_data   + "\n"

    def test_write_dataset_binary_mode(self):
        """Test writing to dataset in binary mode."""
        dsn = f"{self.test_dsn_base}.WRITE.BINARY"
        test_data = "Binary mode test data"
        
        # Create dataset
        attributes = self._create_ps_attributes(dsn)
        ds.create_data_set(dsn, attributes)
        self.created_datasets.append(dsn)
        
        # Write data in binary mode
        etag = ds.write_data_set(dsn, test_data, "binary", "")
        
        # Verify write succeeded
        assert isinstance(etag, str)
        assert len(etag) > 0
        
        # Verify data was written correctly
        content = ds.read_data_set(dsn, "binary")
        assert content.startswith(test_data)

    # KNOWN LIMITATION TESTS
    # These pin the two gaps called out under "Known limitation" in
    # native/python/bindings/README.md. They are strict xfails: today they fail and the suite stays
    # green, but the moment the underlying conversion is fixed pytest reports XPASS as a failure,
    # which is the signal to drop the marker here and the note in the README together.
    @pytest.mark.xfail(
        strict=True,
        reason="Explicit codepage double-converts data set content (zds converts, then the binding "
               "converts again), so the round-trip hands back an empty string. See "
               "native/python/bindings/README.md.",
    )
    def test_read_write_dataset_with_explicit_codepage(self):
        """Explicit (non-default, non-binary) codepage should round-trip."""
        dsn = f"{self.test_dsn_base}.CODEPG"
        test_data = "Explicit codepage round-trip"

        # Create dataset
        attributes = self._create_ps_attributes(dsn)
        ds.create_data_set(dsn, attributes)
        self.created_datasets.append(dsn)

        # Write and read back using an explicit codepage
        ds.write_data_set(dsn, test_data, "IBM-1047", "")
        content = ds.read_data_set(dsn, "IBM-1047")
        assert content == test_data + "\n"

    @pytest.mark.xfail(
        strict=True,
        reason="Etags are converted on the way out but not on the way in, so an etag handed back to "
               "write_data_set never matches. See native/python/bindings/README.md.",
    )
    def test_write_dataset_etag_round_trip(self):
        """An etag returned by write_data_set should be accepted by the next write."""
        dsn = f"{self.test_dsn_base}.ETAG"

        # Create dataset
        attributes = self._create_ps_attributes(dsn)
        ds.create_data_set(dsn, attributes)
        self.created_datasets.append(dsn)

        # First write returns the etag for the content we just stored
        etag = ds.write_data_set(dsn, "first revision", "", "")
        assert len(etag) > 0

        # Handing that same etag back should pass the conditional-write check
        new_etag = ds.write_data_set(dsn, "second revision", "", etag)
        assert isinstance(new_etag, str)
        assert len(new_etag) > 0

    # DELETE DATASET TESTS
    def test_delete_dataset_success(self):
        """Test successful deletion of dataset."""
        dsn = f"{self.test_dsn_base}.DELETE.SUCCESS"
        
        # Create dataset
        attributes = self._create_ps_attributes(dsn)
        ds.create_data_set(dsn, attributes)
        
        # Verify dataset exists
        datasets = ds.list_data_sets(dsn)
        found_dataset = self._find_dataset_in_list(datasets, dsn)
        assert found_dataset is not None, "Dataset should exist before deletion"
        
        # Delete dataset - should not raise exception
        ds.delete_data_set(dsn)
        
        # Verify dataset no longer exists
        try:
            datasets = ds.list_data_sets(dsn)
            found_dataset = self._find_dataset_in_list(datasets, dsn)
            assert found_dataset is None, "Dataset should not exist after deletion"
        except RuntimeError:
            pass

if __name__ == "__main__":
    pytest.main([__file__, "-v"])