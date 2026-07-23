PROG = my_program
SRCS = opkg.c
LIBS = -larchive

$(PROG): $(SRCS)
	gcc -o $(PROG) $(SRCS) $(LIBS)
