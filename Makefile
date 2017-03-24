PROGRAM = timebox


$(PROGRAM) : $(PROGRAM).c
	$(CC) $(CFLAGS) -o $@ $^

clean :
	rm -f $(PROGRAM)


.PHONY : clean

.DELETE_ON_ERROR :

