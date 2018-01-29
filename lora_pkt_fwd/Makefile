### Application-specific constants

APP_NAME := lora_pkt_fwd

### Environment constants 

LGW_PATH ?= ../../lora_gateway_shared/libloragw
ARCH ?=
CROSS_COMPILE ?=

OBJDIR = obj
INCLUDES = $(wildcard inc/*.h)

### External constant definitions
# must get library build option to know if mpsse must be linked or not

include $(LGW_PATH)/library.cfg
RELEASE_VERSION := `cat ../VERSION`

### Constant symbols

CC := $(CROSS_COMPILE)gcc
AR := $(CROSS_COMPILE)ar

CCFLAGS := -O2 -Wall -Wextra -Iinc -I. -fPIC
CFLAGS := $(CCFLAGS) -std=c99 \
          -Dmain=$(APP_NAME)_main \
          -Dsocket=mem_socket \
          -Dconnect=mem_connect \
          -Dsetsockopt=mem_setsockopt \
          -Dsend=mem_send \
          -Drecv=mem_recv \
          -Dshutdown=mem_shutdown \
          -Dexit=mem_exit \
          -Dsigaction=mem_sigaction
VFLAG := -D VERSION_STRING="\"$(RELEASE_VERSION)\""

### Constants for Lora concentrator HAL library
# List the library sub-modules that are used by the application

LGW_INC =
ifneq ($(wildcard $(LGW_PATH)/inc/config.h),)
  # only for HAL version 1.3 and beyond
  LGW_INC += $(LGW_PATH)/inc/config.h
endif
LGW_INC += $(LGW_PATH)/inc/loragw_hal.h
LGW_INC += $(LGW_PATH)/inc/loragw_gps.h

### Linking options

LIBS := -lloragw -lrt -lpthread -lm -lstdc++

### General build targets

all: lib$(APP_NAME).so

clean:
	rm -f $(OBJDIR)/*.o
	rm -f lib$(APP_NAME).so

### Sub-modules compilation

$(OBJDIR):
	mkdir -p $(OBJDIR)

$(OBJDIR)/%.o: src/%.c $(INCLUDES) | $(OBJDIR)
	$(CC) -c $(CFLAGS) -I$(LGW_PATH)/inc $< -o $@

$(OBJDIR)/%.o: src/%.cc $(INCLUDES) | $(OBJDIR)
	$(CC) -c $(CCFLAGS) -I$(LGW_PATH)/inc $< -o $@

### Main program compilation and assembly

$(OBJDIR)/$(APP_NAME).o: src/$(APP_NAME).c $(LGW_INC) $(INCLUDES) | $(OBJDIR)
	$(CC) -c $(CFLAGS) $(VFLAG) -I$(LGW_PATH)/inc $< -o $@

lib$(APP_NAME).so: $(OBJDIR)/$(APP_NAME).o $(LGW_PATH)/libloragw.so $(OBJDIR)/parson.o $(OBJDIR)/base64.o $(OBJDIR)/jitqueue.o $(OBJDIR)/timersync.o $(OBJDIR)/lora_comms.o
	$(CC) -L$(LGW_PATH) -Wl,-rpath,\$$ORIGIN/$(LGW_PATH) $< $(OBJDIR)/parson.o $(OBJDIR)/base64.o $(OBJDIR)/jitqueue.o $(OBJDIR)/timersync.o $(OBJDIR)/lora_comms.o -shared -o $@ $(LIBS)

### EOF
