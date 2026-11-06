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

all: copy-mtl $(OUT_DIR) run

#
# Output directories
#
$(OUT_DIR):
	@echo 'Creating $(OUT_DIR)'
	mkdir -p $(OUT_DIR)

run: $(OUT_DIR)/ams_driver
	@echo 'Running ./$(OUT_DIR)/ams_driver'
	./$(OUT_DIR)/ams_driver

copy-mtl:
	cp ../../zdsm.c zdsm.c
	cp ../../zam.c zam.c
	cp ../../zutm.c zutm.c
	cp ../../zutm31.c zutm31.c
	cp ../../zlogger_metal.c zlogger_metal.c
	
$(OUT_DIR)/zam24.s: ../../zam24.s
	@echo 'Building $(OUT_DIR)/zam24.s'
	cp ../../zam24.s $(OUT_DIR)/zam24.s

$(OUT_DIR)/zam24.o: $(OUT_DIR)/zam24.s
	@echo 'Building $(OUT_DIR)/zam24.o'
	$(ASM) $(ASM_FLAGS) -a=$*.s.lst $(MACLIBS) -o $@ $^

$(OUT_DIR)/ams_driver.o: ams_driver.cpp
	@echo 'Building $(OUT_DIR)/ams_driver.o'
	$(CXX) $(CPP_FLAGS) -c $^ -o $@

$(OUT_DIR)/zds.o: ../../zds.cpp
	@echo 'Building $(OUT_DIR)/zds.o'
	$(CXX) $(CPP_FLAGS) -c $^ -o $@

$(OUT_DIR)/zut.o: ../../zut.cpp
	@echo 'Building $(OUT_DIR)/zut.o'
	$(CXX) $(CPP_FLAGS) -c $^ -o $@

$(OUT_DIR)/zdsm.s: zdsm.c
	@echo 'Building $(OUT_DIR)/zdsm.s'
	$(CC) $(MTL_FLAGS64) -qlist=$*.mtl.lst $(MTL_HEADERS) -o $@ $^

$(OUT_DIR)/zdsm.o: $(OUT_DIR)/zdsm.s
	@echo 'Building $(OUT_DIR)/zdsm.o'
	$(ASM) $(ASM_FLAGS) -a=$*.s.lst $(MACLIBS) -o $@ $^

$(OUT_DIR)/zam.s: zam.c
	@echo 'Building $(OUT_DIR)/zam.s'
	$(CC) $(MTL_FLAGS) -qlist=$*.mtl.lst $(MTL_HEADERS) -o $@ $^ 

$(OUT_DIR)/zam.o: $(OUT_DIR)/zam.s
	@echo 'Building $(OUT_DIR)/zam.o'
	$(ASM) $(ASM_FLAGS) -a=$*.s.lst $(MACLIBS) -o $@ $^

$(OUT_DIR)/zutm.s: zutm.c
	@echo 'Building $(OUT_DIR)/zutm.s'
	$(CC) $(MTL_FLAGS64) -qlist=$*.mtl.lst $(MTL_HEADERS) -o $@ $^

$(OUT_DIR)/zutm.o: $(OUT_DIR)/zutm.s
	@echo 'Building $(OUT_DIR)/zutm.o'
	$(ASM) $(ASM_FLAGS) -a=$*.s.lst $(MACLIBS) -o $@ $^

$(OUT_DIR)/zutm31.s: zutm31.c
	@echo 'Building $(OUT_DIR)/zutm31.s'
	$(CC) $(MTL_FLAGS) -qlist=$*.mtl.lst $(MTL_HEADERS) -o $@ $^

$(OUT_DIR)/zutm31.o: $(OUT_DIR)/zutm31.s
	@echo 'Building $(OUT_DIR)/zutm31.o'
	$(ASM) $(ASM_FLAGS) -a=$*.s.lst $(MACLIBS) -o $@ $^

.IF $(BuildType) == DEBUG
$(OUT_DIR)/zlogger_metal.s: zlogger_metal.c
	@echo 'Building $(OUT_DIR)/zlogger_metal.s'
	$(CC) $(MTL_FLAGS64) -qlist=$*.mtl.lst $(MTL_HEADERS) -o $@ $^

$(OUT_DIR)/zlogger_metal.o: $(OUT_DIR)/zlogger_metal.s
	@echo 'Building $(OUT_DIR)/zlogger_metal.o'
	$(ASM) $(ASM_FLAGS) -a=$*.s.lst $(MACLIBS) -o $@ $^
.END

$(OUT_DIR)/ams_driver: $(OUT_DIR)/zds.o $(OUT_DIR)/zdsm.o $(OUT_DIR)/zut.o $(OUT_DIR)/zam.o $(OUT_DIR)/zam24.o $(OUT_DIR)/zutm.o $(OUT_DIR)/zutm31.o $(METAL_LOGGER_OBJ) $(OUT_DIR)/ams_driver.o
	@echo 'Building ams_driver'
	$(CXX) $(CPP_BND_FLAGS) -o $@ $^ > $*.bind.lst

clean:
	rm -f *.o
	rm -f *.dbg
	rm -f *.so
	rm -f *.a
	rm -f *.u
	rm -f *.x
	rm -f *.lst
	rm -f CEEDUMP.*
	rm -rf $(OUT_DIR)
