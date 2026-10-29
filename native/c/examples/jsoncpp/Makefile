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

run: $(OUT_DIR)/json
	@echo 'Running ./$(OUT_DIR)/json'
	./$(OUT_DIR)/json

copy-mtl:
	cp ../../zjsonm.c zjsonm.c
	cp ../../zam.c zam.c
	cp ../../zam24.s zam24.s
	cp ../../zutm.c zutm.c
	cp ../../zutm31.c zutm31.c
	cp ../../zlogger_metal.c zlogger_metal.c

$(OUT_DIR)/zjsonm.s: zjsonm.c
	@echo 'Building $(OUT_DIR)/zjsonm.s'
	$(CC) $(MTL_FLAGS64) -qlist=$*.mtl.lst $(MTL_HEADERS) -o $@ $^

$(OUT_DIR)/zjsonm.o: $(OUT_DIR)/zjsonm.s
	@echo 'Building $(OUT_DIR)/zjsonm.o'
	$(ASM) $(ASM_FLAGS) -a=$*.s.lst $(MACLIBS) -o $@ $^

$(OUT_DIR)/zam.s: zam.c
	@echo 'Building $(OUT_DIR)/zam.s'
	$(CC) $(MTL_FLAGS) -qlist=$*.mtl.lst $(MTL_HEADERS) -o $@ $^

$(OUT_DIR)/zam.o: $(OUT_DIR)/zam.s
	@echo 'Building $(OUT_DIR)/zam.o'
	$(ASM) $(ASM_FLAGS) -a=$*.s.lst $(MACLIBS) -o $@ $^

$(OUT_DIR)/zam24.s: zam24.s
	@echo 'Copying $(OUT_DIR)/zam24.s'
	cp ../../zam24.s $(OUT_DIR)/zam24.s

$(OUT_DIR)/zam24.o: $(OUT_DIR)/zam24.s
	@echo 'Building $(OUT_DIR)/zam24.o'
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

$(OUT_DIR)/json.o: json.cpp
	@echo 'Building $(OUT_DIR)/json.o'
	$(CXX) $(CPP_FLAGS) -c $^ -o $@

$(OUT_DIR)/json: $(OUT_DIR)/zjsonm.o $(OUT_DIR)/zam.o $(OUT_DIR)/zam24.o $(OUT_DIR)/zutm.o $(OUT_DIR)/zutm31.o $(METAL_LOGGER_OBJ) $(OUT_DIR)/json.o
	@echo 'Building json'
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
