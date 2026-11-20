#
#  This program and the accompanying materials are
#  made available under the terms of the Eclipse Public License v2.0
#  which accompanies this distribution, and is available at
#  https://www.eclipse.org/legal/epl-v20.html
#
#  SPDX-License-Identifier: EPL-2.0
#
#  Copyright Contributors to the Zowe Project.
#

OUT_DIR=build-out

.INCLUDE: ../examples_toolchain.mk

all: $(OUT_DIR) run

# TODO(zFernand0): S0C4 Abend when unloading plug-ins
# NOTE(zFernand0): Make sure to run `make clean` to remove the plugin from the build-out directory
run: $(OUT_DIR) $(OUT_DIR)/sample_plugin.so
	@echo 'Running ./$(OUT_DIR)/sample_plugin.so'
	mkdir -p ../../build-out/plugins
	cp ./$(OUT_DIR)/sample_plugin.so ../../build-out/plugins/sample_plugin.so
	cd ../../build-out && ZO_PLUGINS_DIR=./plugins ./zo plugins list
	cd ../../build-out && ZO_PLUGINS_DIR=./plugins ./zo sample hello World

$(OUT_DIR):
	@echo 'Creating $(OUT_DIR)'
	mkdir -p $(OUT_DIR)

$(OUT_DIR)/sample_plugin.o: sample_plugin.cpp
	@echo 'Building $(OUT_DIR)/sample_plugin.o'
	$(CXX) $(CPP_FLAGS) -c -o $@ $^

$(OUT_DIR)/sample_plugin.so: $(OUT_DIR)/sample_plugin.o
	@echo 'Building $(OUT_DIR)/sample_plugin.so'
	$(CXX) $(DLL_BND_BASE_FLAGS) -o $@ $^

clean:
	rm -f *.o
	rm -f *.dbg
	rm -f *.so
	rm -f *.a
	rm -f *.x
	rm -f *.lst
	rm -f CEEDUMP.*
	rm -f ../../build-out/plugins/sample_plugin.so
	rm -rf $(OUT_DIR)
