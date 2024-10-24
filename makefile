# Compiler
CC = gcc

# Compiler flags
CFLAGS = -Wall -g

# Source files
CAR_SRC = car.c
CONTROLLER_SRC = controller.c
CALL_SRC = call.c
INTERNAL_SRC = internal.c
SAFETY_SRC = safety.c

# Header files
HEADERS = car_shared_mem.h

# Object files
CAR_OBJ = $(CAR_SRC:.c=.o)
CONTROLLER_OBJ = $(CONTROLLER_SRC:.c=.o)
CALL_OBJ = $(CALL_SRC:.c=.o)
INTERNAL_OBJ = $(INTERNAL_SRC:.c=.o)
SAFETY_OBJ = $(SAFETY_SRC:.c=.o)

# Executable files
CAR_EXEC = car
CONTROLLER_EXEC = controller
CALL_EXEC = call
INTERNAL_EXEC = internal
SAFETY_EXEC = safety

# Default target
all: $(CAR_EXEC) $(CONTROLLER_EXEC) $(CALL_EXEC) $(INTERNAL_EXEC) $(SAFETY_EXEC)

# Individual targets
car: $(CAR_EXEC)
controller: $(CONTROLLER_EXEC)
call: $(CALL_EXEC)
internal: $(INTERNAL_EXEC)
safety: $(SAFETY_EXEC)

# Linking
$(CAR_EXEC): $(CAR_OBJ)
	$(CC) $(CFLAGS) -o $@ $^

$(CONTROLLER_EXEC): $(CONTROLLER_OBJ)
	$(CC) $(CFLAGS) -o $@ $^

$(CALL_EXEC): $(CALL_OBJ)
	$(CC) $(CFLAGS) -o $@ $^

$(INTERNAL_EXEC): $(INTERNAL_OBJ)
	$(CC) $(CFLAGS) -o $@ $^

$(SAFETY_EXEC): $(SAFETY_OBJ)
	$(CC) $(CFLAGS) -o $@ $^

# Compilation
%.o: %.c $(HEADERS)
	$(CC) $(CFLAGS) -c $< -o $@

# Clean up
clean:
	rm -f $(CAR_OBJ) $(CONTROLLER_OBJ) $(CALL_OBJ) $(INTERNAL_OBJ) $(SAFETY_OBJ) $(CAR_EXEC) $(CONTROLLER_EXEC) $(CALL_EXEC) $(INTERNAL_EXEC) $(SAFETY_EXEC)