RESULTS_FILE := test_results.txt
TEST_ZDS := test_zds.py
TEST_ZJB := test_zjb.py
TEST_ZUSF := test_zusf.py
TEST_ZKR := test_zkr.py
TEST_ZDS_UTF8 := test_zds_utf8.py
TEST_ZJB_UTF8 := test_zjb_utf8.py
TEST_ZUSF_UTF8 := test_zusf_utf8.py
TEST_ZKR_UTF8 := test_zkr_utf8.py
JUNIT_ZDS := results-zds.xml
JUNIT_ZJB := results-zjb.xml
JUNIT_ZUSF := results-zusf.xml
JUNIT_ZKR := results-zkr.xml
JUNIT_ALL := pybi_results.xml
PY_ENV := export PYTHONIOENCODING=utf-8 && ./venv
PIP := $(PY_ENV)/bin/pip
PYTEST := $(PY_ENV)/bin/pytest

all: test

# Runs every suite and always tears down, even if one fails, so a single suite's failure doesn't
# skip the others or leave the venv behind - but the target still fails afterward so CI catches it.
test: setup
	@echo "=== Test Results - $$(date) ===" | tee $(RESULTS_FILE)
	@echo | tee -a $(RESULTS_FILE)
	@rm -f .tests_failed
	@$(MAKE) test-zds 2>&1 | tee -a $(RESULTS_FILE) || touch .tests_failed
	@$(MAKE) test-zjb 2>&1 | tee -a $(RESULTS_FILE) || touch .tests_failed
	@$(MAKE) test-zusf 2>&1 | tee -a $(RESULTS_FILE) || touch .tests_failed
	@$(MAKE) test-zkr 2>&1 | tee -a $(RESULTS_FILE) || touch .tests_failed
	@$(MAKE) merge-results || touch .tests_failed
	@echo | tee -a $(RESULTS_FILE)
	@echo "=== Test Completed - $$(date) ===" | tee -a $(RESULTS_FILE)
	@echo "Results saved to: $(PWD)/$(RESULTS_FILE)"
	@echo "JUnit report saved to: $(PWD)/$(JUNIT_ALL)"
	@$(MAKE) teardown
	@if [ -f .tests_failed ]; then rm -f .tests_failed; exit 1; fi

# Pattern rule to convert EBCDIC to UTF-8
%_utf8.py: %.py
	@iconv -f IBM-1047 -t UTF-8 $< > $@
	@chtag -t -c UTF-8 $@

# Individual test targets
test-zds: $(TEST_ZDS_UTF8)
	@echo "Running dataset tests..."
	@$(PYTEST) $(TEST_ZDS_UTF8) -v --junitxml=$(JUNIT_ZDS) -o junit_suite_name=TestDatasetFunctions

test-zjb: $(TEST_ZJB_UTF8)
	@echo "Running job tests..."
	@$(PYTEST) $(TEST_ZJB_UTF8) -v --junitxml=$(JUNIT_ZJB) -o junit_suite_name=TestJobFunctions

test-zusf: $(TEST_ZUSF_UTF8)
	@echo "Running USS tests..."
	@$(PYTEST) $(TEST_ZUSF_UTF8) -v --junitxml=$(JUNIT_ZUSF) -o junit_suite_name=TestUSSFunctions

test-zkr: $(TEST_ZKR_UTF8)
	@echo "Running certificate/key ring tests..."
	@$(PYTEST) $(TEST_ZKR_UTF8) -v --junitxml=$(JUNIT_ZKR) -o junit_suite_name=TestCertificates

# Combines the per-suite JUnit reports into a single file so CI only needs to retrieve/publish one report
merge-results:
	@$(PY_ENV)/bin/python -c "import sys, os, xml.etree.ElementTree as ET; root = ET.Element('testsuites'); [root.append(s) for f in sys.argv[2:] if os.path.exists(f) for s in ET.parse(f).getroot()]; ET.ElementTree(root).write(sys.argv[1], xml_declaration=True, encoding='UTF-8')" $(JUNIT_ALL) $(JUNIT_ZDS) $(JUNIT_ZJB) $(JUNIT_ZUSF) $(JUNIT_ZKR)

# Utility targets
setup:
	@iconv -f IBM-1047 -t UTF-8 requirements.txt > requirements_utf8.txt
	@chtag -t -c UTF-8 requirements_utf8.txt
	@python -m venv venv
	@$(PIP) install -r requirements_utf8.txt

teardown:
	@rm -rf __pycache__ venv requirements_utf8.txt

clean: teardown
	@rm -f test_results*.txt $(JUNIT_ZDS) $(JUNIT_ZJB) $(JUNIT_ZUSF) $(JUNIT_ZKR) $(JUNIT_ALL) $(TEST_ZDS_UTF8) $(TEST_ZJB_UTF8) $(TEST_ZUSF_UTF8) $(TEST_ZKR_UTF8)

help:
	@echo "Available targets:"
	@echo "  all         - Run all tests with results saved to file"
	@echo "  test-zds    - Run dataset tests only"
	@echo "  test-zjb    - Run job tests only"
	@echo "  test-zusf   - Run USS tests only"
	@echo "  test-zkr    - Run certificate/key ring tests only"
	@echo "  clean       - Remove all test result files"
	@echo "  setup       - Setup the test environment"
	@echo "  teardown    - Teardown the test environment"
	@echo "  help        - Show this help message"

.PHONY: all test-zds test-zjb test-zusf test-zkr merge-results clean-results help clean setup teardown
