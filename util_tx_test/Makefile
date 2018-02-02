### Application-specific constants

APP_NAME := util_tx_test

### Constant symbols

CC := $(CROSS_COMPILE)gcc
AR := $(CROSS_COMPILE)ar

CFLAGS := -O2 -Wall -Wextra -std=c99 -Iinc -I. -I../lora_pkt_fwd/inc

OBJDIR = obj
INCLUDES = $(wildcard inc/*.h)

### General build targets

all: $(APP_NAME)

clean:
	rm -f $(OBJDIR)/*.o
	rm -f $(APP_NAME)

### Sub-modules compilation

$(OBJDIR):
	mkdir -p $(OBJDIR)

$(OBJDIR)/%.o: src/%.c $(INCLUDES) | $(OBJDIR)
	$(CC) -c $(CFLAGS) $< -o $@

### Main program assembly

$(APP_NAME): $(OBJDIR)/$(APP_NAME).o $(OBJDIR)/base64.o ../lora_pkt_fwd/liblora_pkt_fwd.so
	$(CC) $< $(OBJDIR)/base64.o -o $@ -L../lora_pkt_fwd -Wl,-rpath,\$$ORIGIN/../lora_pkt_fwd -llora_pkt_fwd -lpthread

### EOF
