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

OBJS = $(OUT_DIR)/zo_rpc_c_api.o $(OUT_DIR)/zo_content_c_api.o

# zo RPC dispatcher + command handlers (same objects linked into the zo binary, minus zo.o)
ZO_RPC_OBJS = $(C_OUT_DIR)/json_output.o \
              $(C_OUT_DIR)/extend/plugin.o \
              $(C_OUT_DIR)/zjsonm.o \
              $(C_OUT_DIR)/commands/certificates.o \
              $(C_OUT_DIR)/commands/core.o \
              $(C_OUT_DIR)/commands/ds.o \
              $(C_OUT_DIR)/commands/job.o \
              $(C_OUT_DIR)/commands/server.o \
              $(C_OUT_DIR)/commands/system.o \
              $(C_OUT_DIR)/commands/tso.o \
              $(C_OUT_DIR)/commands/uss.o \
              $(C_OUT_DIR)/commands/tool.o \
              $(C_OUT_DIR)/server/builder.o \
              $(C_OUT_DIR)/server/rpc_commands.o \
              $(C_OUT_DIR)/server/dispatcher.o \
              $(C_OUT_DIR)/server/plugin_bridge.o \
              $(C_OUT_DIR)/server/rpcio.o \
              $(C_OUT_DIR)/server/rpc_server.o \
              $(C_OUT_DIR)/server/validator.o \
              $(C_OUT_DIR)/server/worker.o

C_LIBS = $(C_OUT_DIR)/libzds.a \
         $(C_OUT_DIR)/libzjb.a \
         $(C_OUT_DIR)/libzusf.a \
         $(C_OUT_DIR)/libztso.a \
         $(C_OUT_DIR)/libzkr.a \
         $(C_OUT_DIR)/libzut.a

GSKCMS_SIDEDECK=/usr/lib/GSKCMS64.x

all: $(OUT_DIR)/libzo_java.so

$(OUT_DIR):
	mkdir -p $(OUT_DIR)

$(OUT_DIR)/%.o: %.cpp
	mkdir -p $(OUT_DIR)
	@echo 'Building $@'
	$(CXX) $(CPP_FLAGS) -o $@ $<

$(OUT_DIR)/libzo_java.so: $(OBJS) $(ZO_RPC_OBJS) $(C_LIBS)
	@echo 'Building $(OUT_DIR)/libzo_java.so'
	$(CXX) $(DLL_BND_FLAGS) -o $@ $^ $(GSKCMS_SIDEDECK)
	extattr +p $@

clean:
	rm -rf $(OUT_DIR)
