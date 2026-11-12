OUT_DIR=build-out
C_PATH=../../c
C_OUT_DIR=$(C_PATH)/build-out

.INCLUDE: $(C_PATH)/toolchain.mk

CPP_FLAGS=$(CPP_FLAGS_BASE) -I$(C_PATH) -I$(C_PATH)/chdsect -I../../python/bindings

# make -DBuildType=DEBUG or -DBuildType=RELEASE
.IF $(BuildType) == DEBUG
CPP_FLAGS+=$(DEBUGGER_FLAGS) $(OTHER_C_FLAGS)
DLL_BND_FLAGS=$(DLL_BND_DEBUG_FLAGS) $(DEBUGGER_FLAGS)
.ELSIF $(BuildType) == RELEASE
CPP_FLAGS+=$(RELEASE_FLAGS)
DLL_BND_FLAGS=$(DLL_BND_BASE_FLAGS)
.ELSE
CPP_FLAGS+=-g1
DLL_BND_FLAGS=$(DLL_BND_BASE_FLAGS)
.END

.IF DEFINED(LOG_FLAGS)
CPP_FLAGS+=$(LOG_FLAGS)
.END

OBJS = $(OUT_DIR)/zds_c_api.o $(OUT_DIR)/zusf_c_api.o $(OUT_DIR)/zjb_c_api.o

C_LIBS = $(C_OUT_DIR)/libzds.a \
         $(C_OUT_DIR)/libzjb.a \
         $(C_OUT_DIR)/libzusf.a \
         $(C_OUT_DIR)/libzut.a

all: $(OUT_DIR)/libzo_java.so

$(OUT_DIR):
	mkdir -p $(OUT_DIR)

$(OUT_DIR)/%.o: %.cpp
	mkdir -p $(OUT_DIR)
	@echo 'Building $@'
	$(CXX) $(CPP_FLAGS) -o $@ $<

$(OUT_DIR)/libzo_java.so: $(OBJS) $(C_LIBS)
	@echo 'Building $(OUT_DIR)/libzo_java.so'
	$(CXX) $(DLL_BND_FLAGS) -o $@ $^
	extattr +p $@

clean:
	rm -rf $(OUT_DIR)
