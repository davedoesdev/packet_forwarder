### Application-specific constants

APP_NAME := example

### Constant symbols

CC := $(CROSS_COMPILE)gcc
AR := $(CROSS_COMPILE)ar

CFLAGS := -O2 -Wall -Wextra -std=gnu99 -Iinc -I. -I../lora_pkt_fwd/inc

OBJDIR = .

### General build targets

all: $(APP_NAME)

clean:
	rm -f $(OBJDIR)/*.o
	rm -f $(APP_NAME)

### Main program compilation and assembly

$(OBJDIR):
	mkdir -p $(OBJDIR)

$(OBJDIR)/%.o: src/%.c | $(OBJDIR)
	$(CC) -c $(CFLAGS) $< -o $@

$(APP_NAME): $(OBJDIR)/$(APP_NAME).o ../lora_pkt_fwd/liblora_pkt_fwd.so
	$(CC) $< -o $@ -L../lora_pkt_fwd -Wl,-rpath,\$$ORIGIN/../lora_pkt_fwd -llora_pkt_fwd -lpthread

### EOF
